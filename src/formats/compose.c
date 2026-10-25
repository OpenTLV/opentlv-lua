// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

/*
 * opentlv.formats.compose() and opentlv.formats.packed() build custom Formats
 * from Core's field encodings (tlv/field/) through Core's own compositions
 * (tlv/formats/); opentlv.fields reads and writes packed bit fields. Lua only
 * reads the configuration tables: Core validates the configuration and
 * implements every wire rule.
 */
#include "../format.h"
#include "../codec.h"
#include "../error.h"

#include <tlv/field/packed.h>
#include <tlv/formats/escaped.h>
#include <tlv/formats/fixed.h>
#include <tlv/formats/packed.h>
#include <tlv/formats/variable.h>

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

/* Field encoding kinds, in the order of field_kinds. */
enum { FIELD_FIXED = 0, FIELD_VARIABLE = 1, FIELD_ESCAPED = 2 };
static const char* const field_kinds[] = {"fixed", "variable", "escaped", NULL};
static const char* const byte_orders[] = {"big", "little", NULL};
static const char* const element_orders[] = {"tlv", "ltv", NULL};
static const char* const length_scopes[] = {"value", "tag_and_value", NULL};

/* Packed identifier tables hold 2^width entries; wider fields would need
 * multi-megabyte tables. */
#define PACKED_TAG_MAX_WIDTH 16

/* Stack slots anchoring borrowed configuration strings until they are copied
 * into the Format userdata. */
#define FORBIDDEN_SLOT 2
#define TAG_ONLY_SLOT 3

/*
 * One composed Format. Core borrows the configuration, field encodings,
 * policies and tables for the descriptor's lifetime, so all of them live in
 * this one userdata block: identifier tables follow the struct in the same
 * allocation (common.h's embedding convention), and no finalizer is needed.
 */
typedef struct composed {
    tlv_lua_format_t base; /* First, so every Format entry point accepts it. */
    union {
        tlv_binary_composition_t        binary;
        tlv_tagged_binary_composition_t tagged_binary;
        tlv_variable_format_t           variable;
        tlv_escaped_format_t            escaped;
        tlv_field_composition_t         fields;
        tlv_tagged_fields_composition_t tagged_fields;
        tlv_packed_layout_t             packed;
    } config;
    int                       tag_kind, length_kind;
    tlv_fixed_identifier_t    fixed_tag;
    tlv_variable_identifier_t variable_tag;
    tlv_identifier_policy_t   tag_policy;
    int                       has_tag_policy;
    tlv_fixed_length_t        fixed_length;
    tlv_variable_length_t     variable_length;
    tlv_length_policy_t       length_policy;
    int                       has_length_policy;
    tlv_escaped_length_t      escaped_length;
    tlv_variable_format_t     variable; /* Field source for tagged variable framing. */
    tlv_constructed_bit_t     constructed;
    int                       has_constructed;
    tlv_element_order_t       element_order;
    tlv_length_scope_t        length_scope;
    tlv_tag_t*                tag_only;
    size_t                    count;
} composed_t;

/* Every composition stores its descriptor context in config. */
static int is_constructed(const void* context, const tlv_tag_t* tag) {
    const composed_t* self =
        (const composed_t*)(const void*)((const char*)context - offsetof(composed_t, config));
    return tlv_constructed_bit_predicate(&self->constructed, tag);
}

/* Field adapters for pairs without a dedicated Core composition; context is the owner. */
static tlv_result_t read_tag(const void* context, const uint8_t* data, size_t size, tlv_tag_t* tag,
                             size_t* consumed) {
    const composed_t* self = context;
    return self->tag_kind == FIELD_FIXED
               ? tlv_fixed_identifier_read(&self->fixed_tag, data, size, tag, consumed)
               : tlv_variable_identifier_read(&self->variable_tag, data, size, tag, consumed);
}

static tlv_result_t write_tag(const void* context, const tlv_tag_t* tag, uint8_t* data,
                              size_t capacity, size_t* written) {
    const composed_t* self = context;
    return self->tag_kind == FIELD_FIXED
               ? tlv_fixed_identifier_write(&self->fixed_tag, tag, data, capacity, written)
               : tlv_variable_identifier_write(&self->variable_tag, tag, data, capacity, written);
}

static tlv_result_t read_length(const void* context, const uint8_t* data, size_t size,
                                tlv_size_t* length, size_t* consumed) {
    const composed_t* self = context;
    switch (self->length_kind) {
        case FIELD_FIXED:
            return tlv_fixed_length_read(&self->fixed_length, data, size, length, consumed);
        case FIELD_VARIABLE:
            return tlv_variable_length_read(&self->variable_length, data, size, length, consumed);
        default:
            return tlv_escaped_length_read(&self->escaped_length, data, size, length, consumed);
    }
}

static tlv_result_t write_length(const void* context, tlv_size_t length, uint8_t* data,
                                 size_t capacity, size_t* written) {
    const composed_t* self = context;
    switch (self->length_kind) {
        case FIELD_FIXED:
            return tlv_fixed_length_write(&self->fixed_length, length, data, capacity, written);
        case FIELD_VARIABLE:
            return tlv_variable_length_write(&self->variable_length, length, data, capacity,
                                             written);
        default:
            return tlv_escaped_length_write(&self->escaped_length, length, data, capacity, written);
    }
}

/* Generic field composition validates configuration only when a field is
 * used. A sizing write checks configuration first, so only argument and
 * configuration errors reject the encoding here. */
static tlv_result_t probe_fields(const composed_t* self) {
    static const uint8_t probe = 0;
    tlv_tag_t            tag = tlv_tag(&probe, 1);
    size_t               written;
    tlv_result_t         rc = write_tag(self, &tag, NULL, 0, &written);
    if (rc == TLV_ERR_INVALID_ARG || rc == TLV_ERR_NULL_ARG) return rc;
    rc = write_length(self, 0, NULL, 0, &written);
    return rc == TLV_ERR_INVALID_ARG || rc == TLV_ERR_NULL_ARG ? rc : TLV_OK;
}

/* Pushes t[name] and returns nonzero, or returns zero without pushing when it is absent. */
static int get(lua_State* L, int t, const char* name) {
    lua_getfield(L, t, name);
    if (!lua_isnil(L, -1)) return 1;
    lua_pop(L, 1);
    return 0;
}

/* Reads an unsigned integer option with the codec integer rules; math.huge
 * selects the maximum of a 64-bit count. */
static uint64_t number(lua_State* L, int t, const char* name, uint64_t fallback, uint64_t maximum,
                       int required) {
    if (!get(L, t, name)) {
        if (required) luaL_error(L, "%s is required", name);
        return fallback;
    }
    uint64_t value = maximum == UINT64_MAX && lua_type(L, -1) == LUA_TNUMBER &&
                             lua_tonumber(L, -1) == (lua_Number)HUGE_VAL
                         ? UINT64_MAX
                         : opentlv_lua_codec_uint(L, lua_gettop(L), maximum);
    lua_pop(L, 1);
    return value;
}

static uint8_t byte(lua_State* L, int t, const char* name) {
    return (uint8_t)number(L, t, name, 0, UINT8_MAX, 1);
}

static int flag(lua_State* L, int t, const char* name, int fallback) {
    if (!get(L, t, name)) return fallback;
    if (!lua_isboolean(L, -1)) luaL_error(L, "%s must be a boolean", name);
    int value = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return value;
}

/* Returns the index of t[name] in names, or fallback when absent; a negative
 * fallback makes the option required. */
static int choice(lua_State* L, int t, const char* name, int fallback, const char* const names[]) {
    if (!get(L, t, name)) {
        if (fallback < 0) luaL_error(L, "%s is required", name);
        return fallback;
    }
    const char* text = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : NULL;
    for (int i = 0; text && names[i]; ++i) {
        if (!strcmp(text, names[i])) {
            lua_pop(L, 1);
            return i;
        }
    }
    return luaL_error(L, "invalid %s", name);
}

/* Pushes the table t[name] and returns its index, or zero when it is absent and optional. */
static int table(lua_State* L, int t, const char* name, int required) {
    if (!get(L, t, name)) {
        if (required) luaL_error(L, "%s is required", name);
        return 0;
    }
    if (!lua_istable(L, -1)) luaL_error(L, "%s must be a table", name);
    return lua_gettop(L);
}

static tlv_byte_order_t byte_order(lua_State* L, int t) {
    return choice(L, t, "byte_order", 0, byte_orders) ? TLV_BYTE_ORDER_LITTLE_ENDIAN
                                                      : TLV_BYTE_ORDER_BIG_ENDIAN;
}

static void parse_tag(lua_State* L, composed_t* self, int t) {
    self->tag_kind = choice(L, t, "kind", -1, field_kinds);
    if (self->tag_kind == FIELD_FIXED) {
        self->fixed_tag.size = (size_t)number(L, t, "size", 0, SIZE_MAX, 1);
        return;
    }
    if (self->tag_kind != FIELD_VARIABLE) luaL_error(L, "identifiers have no escaped encoding");
    tlv_variable_identifier_t* tag = &self->variable_tag;
    tag->inline_mask = byte(L, t, "inline_mask");
    tag->escape = byte(L, t, "escape");
    tag->continuation_bit = byte(L, t, "continuation_bit");
    tag->payload_mask = byte(L, t, "payload_mask");
    tag->max_size = (size_t)number(L, t, "max_size", 0, SIZE_MAX, 1);
    int policy = table(L, t, "policy", 0);
    if (!policy) return;
    tlv_identifier_policy_t* rules = &self->tag_policy;
    if (get(L, policy, "forbidden_leading")) {
        if (lua_type(L, -1) != LUA_TSTRING) luaL_error(L, "forbidden_leading must be a string");
        rules->forbidden_leading =
            (const uint8_t*)lua_tolstring(L, -1, &rules->forbidden_leading_count);
        lua_replace(L, FORBIDDEN_SLOT);
    }
    rules->reject_zero_first_payload = (unsigned)flag(L, policy, "reject_zero_first_payload", 0);
    rules->require_minimal = (unsigned)flag(L, policy, "require_minimal", 0);
    self->has_tag_policy = 1;
    lua_pop(L, 1);
}

static void parse_length(lua_State* L, composed_t* self, int t) {
    self->length_kind = choice(L, t, "kind", -1, field_kinds);
    switch (self->length_kind) {
        case FIELD_FIXED:
            self->fixed_length.size = (size_t)number(L, t, "size", 0, SIZE_MAX, 1);
            self->fixed_length.byte_order = byte_order(L, t);
            break;
        case FIELD_VARIABLE: {
            tlv_variable_length_t* length = &self->variable_length;
            length->long_form_bit = byte(L, t, "long_form_bit");
            length->payload_mask = byte(L, t, "payload_mask");
            length->byte_order = byte_order(L, t);
            int policy = table(L, t, "policy", 0);
            if (!policy) break;
            tlv_length_policy_t* limits = &self->length_policy;
            limits->allow_short = (unsigned)flag(L, policy, "allow_short", 1);
            limits->allow_long = (unsigned)flag(L, policy, "allow_long", 1);
            limits->require_minimal = (unsigned)flag(L, policy, "require_minimal", 0);
            limits->max_long_octets = (size_t)number(L, policy, "max_long_octets", 8, SIZE_MAX, 0);
            limits->max_value = number(L, policy, "max_value", UINT64_MAX, UINT64_MAX, 0);
            self->has_length_policy = 1;
            lua_pop(L, 1);
            break;
        }
        default: {
            tlv_escaped_length_t* length = &self->escaped_length;
            length->escape = byte(L, t, "escape");
            length->extended_size = (size_t)number(L, t, "extended_size", 0, SIZE_MAX, 1);
            length->byte_order = byte_order(L, t);
            length->min_extended = number(L, t, "min_extended", 0, UINT64_MAX, 0);
            /* By default every count the extended field can hold. */
            size_t   size = length->extended_size;
            uint64_t largest = size && size < 8 ? (UINT64_C(1) << (8 * size)) - 1 : UINT64_MAX;
            length->max_length = number(L, t, "max_length", largest, UINT64_MAX, 0);
        }
    }
}

static void parse_constructed(lua_State* L, composed_t* self) {
    int bit = table(L, 1, "constructed", 0);
    if (!bit) return;
    self->constructed.byte_index = (size_t)number(L, bit, "byte_index", 0, SIZE_MAX, 0);
    self->constructed.mask = byte(L, bit, "mask");
    self->constructed.value = byte(L, bit, "value");
    self->has_constructed = 1;
    lua_pop(L, 1);
}

/* Counts the tag-only identifiers and returns their total size. */
static size_t parse_tag_only(lua_State* L, composed_t* self) {
    if (!get(L, 1, "tag_only")) return 0;
    if (!lua_istable(L, -1)) luaL_error(L, "tag_only must be an array of strings");
    lua_replace(L, TAG_ONLY_SLOT);
#if LUA_VERSION_NUM == 501
    size_t count = lua_objlen(L, TAG_ONLY_SLOT);
#else
    size_t count = lua_rawlen(L, TAG_ONLY_SLOT);
#endif
    size_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        lua_rawgeti(L, TAG_ONLY_SLOT, (int)i + 1);
        if (lua_type(L, -1) != LUA_TSTRING) luaL_error(L, "tag_only must be an array of strings");
        size_t size;
        lua_tolstring(L, -1, &size);
        if (size > SIZE_MAX - total) luaL_error(L, "tag_only too large");
        total += size;
        lua_pop(L, 1);
    }
    if (count > (SIZE_MAX - total) / sizeof(tlv_tag_t)) luaL_error(L, "tag_only too large");
    self->count = count;
    return total;
}

static composed_t* new_composed(lua_State* L, const composed_t* spec, size_t extra,
                                const char* name) {
    if (extra > SIZE_MAX - sizeof(composed_t)) luaL_error(L, "format configuration too large");
    composed_t* self = lua_newuserdata(L, sizeof(composed_t) + extra);
    *self = *spec;
    self->base.use_der_validation = 0;
    self->base.name = name;
    return self;
}

/* Copies the snapshotted policy bytes and tag-only identifiers behind the
 * struct and points the field encodings at storage inside this userdata. */
static void link_tables(lua_State* L, composed_t* self) {
    uint8_t* cursor = (uint8_t*)(self + 1);
    if (self->count) {
        self->tag_only = (tlv_tag_t*)(void*)cursor;
        cursor += self->count * sizeof(tlv_tag_t);
        for (size_t i = 0; i < self->count; ++i) {
            lua_rawgeti(L, TAG_ONLY_SLOT, (int)i + 1);
            size_t      size;
            const char* bytes = lua_tolstring(L, -1, &size);
            if (size) memcpy(cursor, bytes, size);
            self->tag_only[i] = tlv_tag(cursor, size);
            cursor += size;
            lua_pop(L, 1);
        }
    }
    if (self->has_tag_policy) {
        tlv_identifier_policy_t* rules = &self->tag_policy;
        if (rules->forbidden_leading_count) {
            memcpy(cursor, rules->forbidden_leading, rules->forbidden_leading_count);
            rules->forbidden_leading = cursor;
        } else {
            rules->forbidden_leading = NULL;
        }
        self->variable_tag.policy = rules;
    }
    if (self->has_length_policy) self->variable_length.policy = &self->length_policy;
}

/* Selects the dedicated Core composition for an encoding pair, or composes
 * the field primitives through the generic field composition. */
static tlv_result_t compose_fields(composed_t* self) {
    tlv_element_order_t          order = self->element_order;
    tlv_length_scope_t           scope = self->length_scope;
    tlv_format_t*                format = &self->base.format;
    const tlv_constructed_bit_t* constructed = self->has_constructed ? &self->constructed : NULL;
    if (self->tag_kind == FIELD_FIXED && self->length_kind == FIELD_FIXED) {
        tlv_binary_composition_t binary = {self->fixed_tag, self->fixed_length, order, scope};
        if (!self->count) {
            self->config.binary = binary;
            return tlv_fixed_format_init(format, &self->config.binary);
        }
        self->config.tagged_binary =
            (tlv_tagged_binary_composition_t){binary, self->tag_only, self->count};
        return tlv_tagged_binary_format_init(format, &self->config.tagged_binary);
    }
    if (self->tag_kind == FIELD_VARIABLE && self->length_kind == FIELD_VARIABLE) {
        self->variable = (tlv_variable_format_t){self->variable_tag, self->variable_length, order,
                                                 scope, constructed};
        if (!self->count) {
            self->config.variable = self->variable;
            return tlv_variable_format_init(format, &self->config.variable);
        }
        tlv_result_t rc =
            tlv_variable_fields_init(&self->config.tagged_fields.fields, &self->variable);
        if (rc != TLV_OK) return rc;
        self->config.tagged_fields.tag_only = self->tag_only;
        self->config.tagged_fields.count = self->count;
        return tlv_tagged_fields_format_init(format, &self->config.tagged_fields);
    }
    if (self->tag_kind == FIELD_FIXED && self->length_kind == FIELD_ESCAPED) {
        self->config.escaped = (tlv_escaped_format_t){
            self->fixed_tag, self->escaped_length, order, scope, self->tag_only, self->count};
        return tlv_escaped_format_init(format, &self->config.escaped);
    }
    tlv_result_t rc = probe_fields(self);
    if (rc != TLV_OK) return rc;
    tlv_field_composition_t fields = {self,      read_tag,     read_length, NULL,
                                      write_tag, write_length, order,       scope};
    if (!self->count) {
        self->config.fields = fields;
        return tlv_fields_format_init(format, &self->config.fields);
    }
    self->config.tagged_fields =
        (tlv_tagged_fields_composition_t){fields, self->tag_only, self->count};
    return tlv_tagged_fields_format_init(format, &self->config.tagged_fields);
}

/* Installs the constructed predicate where Core did not, then publishes the
 * userdata on top of the stack as an opentlv.Format. */
static int finish(lua_State* L, composed_t* self, tlv_result_t rc) {
    if (rc == TLV_OK && self->has_constructed) {
        const tlv_constructed_bit_t* bit = &self->constructed;
        /* Core's predicate treats an invalid bit as no match; reject it as
         * tlv_variable_format_init() does. */
        if (!bit->mask || (bit->value & ~bit->mask))
            rc = TLV_ERR_INVALID_ARG;
        else if (!self->base.format.is_constructed)
            self->base.format.is_constructed = is_constructed;
    }
    if (rc != TLV_OK) return opentlv_lua_raise(L, rc, 0, 0);
    luaL_getmetatable(L, OPENTLV_LUA_FORMAT_MT);
    lua_setmetatable(L, -2);
    return 1;
}

/* opentlv.formats.compose(config) -> Format
 *
 * config.tag is a fixed or variable identifier encoding and config.length a
 * fixed, variable or escaped length encoding; see docs/api.md. */
static int l_compose(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 1);
    lua_pushnil(L); /* FORBIDDEN_SLOT */
    lua_pushnil(L); /* TAG_ONLY_SLOT */
    composed_t spec;
    memset(&spec, 0, sizeof spec);
    parse_tag(L, &spec, table(L, 1, "tag", 1));
    lua_pop(L, 1);
    parse_length(L, &spec, table(L, 1, "length", 1));
    lua_pop(L, 1);
    spec.element_order = choice(L, 1, "element_order", 0, element_orders) ? TLV_ELEMENT_ORDER_LTV
                                                                          : TLV_ELEMENT_ORDER_TLV;
    spec.length_scope = choice(L, 1, "length_scope", 0, length_scopes)
                            ? TLV_LENGTH_SCOPE_TAG_AND_VALUE
                            : TLV_LENGTH_SCOPE_VALUE;
    parse_constructed(L, &spec);
    size_t extra = parse_tag_only(L, &spec) + spec.count * sizeof(tlv_tag_t);
    size_t forbidden = spec.has_tag_policy ? spec.tag_policy.forbidden_leading_count : 0;
    if (forbidden > SIZE_MAX - extra) luaL_error(L, "format configuration too large");
    composed_t* self = new_composed(L, &spec, extra + forbidden, "composed");
    link_tables(L, self);
    return finish(L, self, compose_fields(self));
}

/* Reads one {bit_offset, bit_width} field of a storage_size-byte integer. */
static tlv_packed_field_t packed_bits(lua_State* L, int t, size_t storage_size,
                                      tlv_byte_order_t order) {
    tlv_packed_field_t field;
    field.storage_size = storage_size;
    field.bit_offset = (unsigned int)number(L, t, "bit_offset", 0, UINT_MAX, 1);
    field.bit_width = (unsigned int)number(L, t, "bit_width", 0, UINT_MAX, 1);
    field.byte_order = order;
    return field;
}

/* opentlv.formats.packed(config) -> Format
 *
 * Builds the canonical identifier table Core borrows: entry i is i in
 * tag_size big-endian bytes. */
static int l_packed(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 1);
    composed_t spec;
    memset(&spec, 0, sizeof spec);
    tlv_packed_layout_t* layout = &spec.config.packed;
    layout->header_size = (size_t)number(L, 1, "header_size", 0, SIZE_MAX, 1);
    tlv_byte_order_t order = byte_order(L, 1);
    layout->tag = packed_bits(L, table(L, 1, "tag", 1), layout->header_size, order);
    lua_pop(L, 1);
    layout->length = packed_bits(L, table(L, 1, "length", 1), layout->header_size, order);
    lua_pop(L, 1);
    layout->length_scope = choice(L, 1, "length_scope", 0, length_scopes)
                               ? TLV_LENGTH_SCOPE_TAG_AND_VALUE
                               : TLV_LENGTH_SCOPE_VALUE;
    unsigned int width = layout->tag.bit_width;
    if (width > PACKED_TAG_MAX_WIDTH)
        return luaL_error(L, "packed tag fields wider than 16 bits are unsupported");
    layout->tag_size = (size_t)number(L, 1, "tag_size", width ? (width + 7) / 8 : 1, SIZE_MAX, 0);
    parse_constructed(L, &spec);
    /* Without a table Core would report the missing storage, not the width. */
    if (!width || !layout->tag_size || layout->tag_size > 8)
        return opentlv_lua_raise(L, TLV_ERR_INVALID_ARG, 0, 0);
    size_t      entries = (size_t)1 << width;
    composed_t* self = new_composed(L, &spec, entries * spec.config.packed.tag_size, "packed");
    layout = &self->config.packed;
    uint8_t* storage = (uint8_t*)(self + 1);
    for (size_t i = 0; i < entries; ++i)
        for (size_t b = 0; b < layout->tag_size; ++b)
            storage[i * layout->tag_size + b] =
                (uint8_t)((uint64_t)i >> (8 * (layout->tag_size - 1 - b)));
    layout->tag_storage = storage;
    layout->tag_storage_size = entries * layout->tag_size;
    tlv_result_t rc = tlv_packed_layout_validate(layout);
    if (rc == TLV_OK)
        rc = tlv_format_init(&self->base.format, layout, tlv_packed_decode, tlv_packed_measure,
                             tlv_packed_encode);
    return finish(L, self, rc);
}

static tlv_packed_field_t packed_field(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    size_t storage_size = (size_t)number(L, 1, "header_size", 0, SIZE_MAX, 1);
    return packed_bits(L, 1, storage_size, byte_order(L, 1));
}

/* opentlv.fields.read_packed(field, data) -> integer */
static int l_read_packed(lua_State* L) {
    tlv_packed_field_t field = packed_field(L);
    luaL_checktype(L, 2, LUA_TSTRING);
    size_t       size;
    const char*  data = lua_tolstring(L, 2, &size);
    uint64_t     value = 0;
    tlv_result_t rc = tlv_packed_field_read(&field, (const uint8_t*)data, size, &value);
    if (rc != TLV_OK) return opentlv_lua_raise(L, rc, 0, 0);
    opentlv_lua_codec_push_uint(L, value);
    return 1;
}

/* opentlv.fields.write_packed(field, data, value) -> string
 *
 * Returns a copy of data with the field replaced; Core preserves every bit
 * outside it. */
static int l_write_packed(lua_State* L) {
    tlv_packed_field_t field = packed_field(L);
    luaL_checktype(L, 2, LUA_TSTRING);
    size_t      size;
    const char* data = lua_tolstring(L, 2, &size);
    uint64_t    value = opentlv_lua_codec_uint(L, 3, UINT64_MAX);
    uint8_t*    bytes = lua_newuserdata(L, size ? size : 1);
    if (size) memcpy(bytes, data, size);
    tlv_result_t rc = tlv_packed_field_write(&field, bytes, size, value);
    if (rc != TLV_OK) return opentlv_lua_raise(L, rc, 0, 0);
    lua_pushlstring(L, (const char*)bytes, size);
    return 1;
}

void opentlv_lua_register_compose(lua_State* L) {
    lua_pushcfunction(L, l_compose);
    lua_setfield(L, -2, "compose");
    lua_pushcfunction(L, l_packed);
    lua_setfield(L, -2, "packed");
}

void opentlv_lua_open_fields(lua_State* L, int module_table_index) {
    lua_newtable(L);
    lua_pushcfunction(L, l_read_packed);
    lua_setfield(L, -2, "read_packed");
    lua_pushcfunction(L, l_write_packed);
    lua_setfield(L, -2, "write_packed");
    lua_setfield(L, module_table_index, "fields");
}
