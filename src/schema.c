// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "schema.h"
#include "codec.h"
#include "common.h"
#include "error.h"
#include "format.h"

#include <tlv/schema/constraint.h>
#include <tlv/schema/schema.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define SCHEMA_MT "opentlv.Schema"
#define CHECKED_SCHEMA_MT "opentlv.CheckedSchema"
#define LENGTH_SCHEMA_MT "opentlv.LengthSchema"
#define VALUE_CONSTRAINT_MT "opentlv.ValueConstraint"

/* One registry table owns the native arrays (Lua userdata), immutable strings
 * and child Schema objects. Install __gc before acquiring any references so
 * constructor failures, including Lua allocation failures, are collectable.
 * Every snapshot type starts with that table's reference. */
typedef struct lua_schema {
    int                    references;
    tlv_structure_schema_t schema;
} lua_schema_t;

/* A definition checked once by tlv_schema_prepare(). It borrows the immutable
 * snapshot of the Schema it roots, so the check stays valid. */
typedef struct lua_checked_schema {
    tlv_schema_checked_t checked;
    int                  schema_ref;
} lua_checked_schema_t;

/* Length table snapshot with the same ownership as lua_schema_t. */
typedef struct lua_length_schema {
    int          references;
    tlv_schema_t schema;
} lua_length_schema_t;

typedef struct lua_value_constraint {
    int                    references;
    tlv_value_constraint_t constraint;
} lua_value_constraint_t;

static lua_schema_t* check_schema(lua_State* L, int index) {
    lua_schema_t* self = (lua_schema_t*)luaL_checkudata(L, index, SCHEMA_MT);
    if (self->references == LUA_NOREF) luaL_error(L, "schema has been collected");
    return self;
}

/* Every snapshot type releases its one reference table the same way. */
static int references_gc(lua_State* L, int* references) {
    luaL_unref(L, LUA_REGISTRYINDEX, *references);
    *references = LUA_NOREF;
    return 0;
}

static int schema_gc(lua_State* L) {
    return references_gc(L, &((lua_schema_t*)luaL_checkudata(L, 1, SCHEMA_MT))->references);
}

static int checked_gc(lua_State* L) {
    lua_checked_schema_t* self = (lua_checked_schema_t*)luaL_checkudata(L, 1, CHECKED_SCHEMA_MT);
    self->checked.schema = NULL;
    return references_gc(L, &self->schema_ref);
}

static int length_schema_gc(lua_State* L) {
    return references_gc(
        L, &((lua_length_schema_t*)luaL_checkudata(L, 1, LENGTH_SCHEMA_MT))->references);
}

static int constraint_gc(lua_State* L) {
    return references_gc(
        L, &((lua_value_constraint_t*)luaL_checkudata(L, 1, VALUE_CONSTRAINT_MT))->references);
}

/* Raw fields avoid executing user code while constructing borrowed C tables. */
static void field(lua_State* L, int table, const char* name) {
    lua_pushstring(L, name);
    lua_rawget(L, table);
}

static size_t number_option(lua_State* L, int table, const char* name, size_t fallback,
                            int unlimited) {
    field(L, table, name);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return fallback;
    }
    luaL_checktype(L, -1, LUA_TNUMBER);
#if LUA_VERSION_NUM >= 503
    if (lua_isinteger(L, -1)) {
        lua_Integer value = lua_tointeger(L, -1);
        if (value < 0 || (uintmax_t)value > SIZE_MAX)
            luaL_error(L, "%s must be a nonnegative native integer", name);
        lua_pop(L, 1);
        return (size_t)value;
    }
#endif
    lua_Number value = lua_tonumber(L, -1);
    if (unlimited && value == (lua_Number)HUGE_VAL) {
        lua_pop(L, 1);
        return SIZE_MAX;
    }
    if (!(value >= 0 && value < (lua_Number)SIZE_MAX))
        luaL_error(L, "%s must be a nonnegative native integer", name);
    size_t result = (size_t)value;
    if ((lua_Number)result != value) luaL_error(L, "%s must be an integer", name);
    lua_pop(L, 1);
    return result;
}

static uint32_t uint_option(lua_State* L, int table, const char* name) {
    size_t value = number_option(L, table, name, 0, 0);
    if (value > UINT32_MAX) luaL_error(L, "%s exceeds uint32 range", name);
    return (uint32_t)value;
}

static int enum_option(lua_State* L, int table, const char* name, const char* const* names) {
    field(L, table, name);
    int result = luaL_checkoption(L, -1, names[0], names);
    lua_pop(L, 1);
    return result;
}

static size_t array_count(lua_State* L, int index) {
    luaL_checktype(L, index, LUA_TTABLE);
#if LUA_VERSION_NUM >= 502
    size_t count = lua_rawlen(L, index);
#else
    size_t count = lua_objlen(L, index);
#endif
    if (count > INT_MAX) luaL_error(L, "schema array is too large");
    return count;
}

static void* storage(lua_State* L, int references, size_t count, size_t width) {
    if (!count) return NULL;
    if (count > SIZE_MAX / width) luaL_error(L, "schema storage size overflow");
    void* data = lua_newuserdata(L, count * width);
    memset(data, 0, count * width);
    luaL_ref(L, references);
    return data;
}

/* Retains the string on top of the stack, which must not contain NUL. */
static const char* retained_name(lua_State* L, int references) {
    luaL_checktype(L, -1, LUA_TSTRING);
    size_t      size;
    const char* name = lua_tolstring(L, -1, &size);
    if (memchr(name, 0, size)) luaL_error(L, "schema names cannot contain NUL bytes");
    luaL_ref(L, references);
    return name;
}

static const char* name_option(lua_State* L, int table, int references) {
    field(L, table, "name");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return NULL;
    }
    return retained_name(L, references);
}

/* Pushes a zeroed snapshot userdata of `size` bytes with its finalizer, then
 * its reference table, and returns the userdata; *references receives the
 * table's stack index, directly above the userdata. */
static void* new_snapshot(lua_State* L, size_t size, const char* metatable, int* references) {
    void* self = lua_newuserdata(L, size);
    memset(self, 0, size);
    int* reference = (int*)self;
    *reference = LUA_NOREF;
    luaL_getmetatable(L, metatable);
    lua_setmetatable(L, -2);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    *reference = luaL_ref(L, LUA_REGISTRYINDEX);
    *references = lua_gettop(L);
    return self;
}

/* Reads the length rule fields of the table at `rule` into `entry`. */
static void parse_entry(lua_State* L, int rule, int references, tlv_schema_entry_t* entry) {
    field(L, rule, "tag");
    luaL_checktype(L, -1, LUA_TSTRING);
    size_t      size;
    const char* tag = lua_tolstring(L, -1, &size);
    entry->tag = tlv_tag((const uint8_t*)tag, size);
    luaL_ref(L, references);
    entry->name = name_option(L, rule, references);
    entry->min_length = number_option(L, rule, "min_length", 0, 0);
    entry->max_length = number_option(L, rule, "max_length", SIZE_MAX, 1);
    entry->length_multiple = number_option(L, rule, "length_multiple", 0, 0);
    entry->flags = uint_option(L, rule, "flags");
}

static int schema_new(lua_State* L) {
    static const char* const orders[] = {"any", "sequence", NULL};
    static const char* const kinds[] = {"any", "primitive", "constructed", NULL};
    luaL_checktype(L, 1, LUA_TTABLE);
    int           references;
    lua_schema_t* self =
        (lua_schema_t*)new_snapshot(L, sizeof(lua_schema_t), SCHEMA_MT, &references);
    int result = references - 1;
    self->schema.order = (tlv_schema_order_t)enum_option(L, 1, "order", orders);
    field(L, 1, "allow_unknown");
    if (!lua_isnil(L, -1)) luaL_checktype(L, -1, LUA_TBOOLEAN);
    self->schema.allow_unknown = lua_toboolean(L, -1);
    lua_pop(L, 1);

    field(L, 1, "rules");
    if (!lua_isnil(L, -1)) {
        int list = lua_gettop(L);
        self->schema.count = array_count(L, list);
        tlv_structure_rule_t* rules = storage(L, references, self->schema.count, sizeof(*rules));
        tlv_schema_entry_t* entries = storage(L, references, self->schema.count, sizeof(*entries));
        self->schema.rules = rules;
        for (size_t i = 0; i < self->schema.count; ++i) {
            lua_rawgeti(L, list, (int)i + 1);
            luaL_checktype(L, -1, LUA_TTABLE);
            int rule = lua_gettop(L);
            parse_entry(L, rule, references, &entries[i]);
            rules[i].entry = &entries[i];
            rules[i].min_occurs = number_option(L, rule, "min_occurs", 0, 0);
            rules[i].max_occurs = number_option(L, rule, "max_occurs", SIZE_MAX, 1);
            rules[i].kind = (tlv_schema_kind_t)enum_option(L, rule, "kind", kinds);
            rules[i].group = uint_option(L, rule, "group");
            field(L, rule, "children");
            if (!lua_isnil(L, -1)) {
                rules[i].children = &check_schema(L, -1)->schema;
                luaL_ref(L, references);
            } else
                lua_pop(L, 1);
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    field(L, 1, "groups");
    if (!lua_isnil(L, -1)) {
        int list = lua_gettop(L);
        self->schema.group_count = array_count(L, list);
        tlv_structure_group_t* groups =
            storage(L, references, self->schema.group_count, sizeof(*groups));
        self->schema.groups = groups;
        for (size_t i = 0; i < self->schema.group_count; ++i) {
            lua_rawgeti(L, list, (int)i + 1);
            luaL_checktype(L, -1, LUA_TTABLE);
            int group = lua_gettop(L);
            groups[i].id = uint_option(L, group, "id");
            groups[i].min_occurs = number_option(L, group, "min_occurs", 0, 0);
            groups[i].max_occurs = number_option(L, group, "max_occurs", SIZE_MAX, 1);
            groups[i].name = name_option(L, group, references);
            lua_pop(L, 1);
        }
    }
    lua_settop(L, result);
    return 1;
}

static void size_field(lua_State* L, const char* name, size_t value, int bound) {
    if (bound && value == SIZE_MAX) lua_pushnumber(L, (lua_Number)HUGE_VAL);
#if LUA_VERSION_NUM >= 503
    else if ((uintmax_t)value <= (uintmax_t)LUA_MAXINTEGER)
        lua_pushinteger(L, (lua_Integer)value);
#endif
    else
        lua_pushnumber(L, (lua_Number)value);
    lua_setfield(L, -2, name);
}

static void bounds(lua_State* L, const char* name, size_t minimum, size_t maximum, size_t actual) {
    lua_newtable(L);
    size_field(L, "minimum", minimum, 0);
    size_field(L, "maximum", maximum, 1);
    size_field(L, "actual", actual, 0);
    lua_setfield(L, -2, name);
}

void opentlv_lua_push_schema_diagnostic(lua_State* L, const tlv_schema_diagnostic_t* value) {
    static const char* const kinds[] = {"any", "primitive", "constructed"};
    tlv_diagnostic_t         diagnostic = value->diagnostic;
    /* Reader and argument failures carry no Schema finding. */
    static const tlv_schema_detail_t none;
    const tlv_schema_detail_t*       detail =
        value->cause == TLV_SCHEMA_CAUSE_SCHEMA ? &value->detail.schema : &none;
    opentlv_lua_push_diagnostic(L, &diagnostic);
    lua_pushstring(L, tlv_schema_cause_string(value->cause));
    lua_setfield(L, -2, "cause");
    if (detail->kind != TLV_SCHEMA_ISSUE_NONE) {
        lua_pushstring(L, tlv_schema_issue_kind_string(detail->kind));
        lua_setfield(L, -2, "kind");
    }
    if (detail->tag.size) {
        lua_pushlstring(L, (const char*)detail->tag.data, detail->tag.size);
        lua_setfield(L, -2, "tag");
    }
    if (detail->definition.kind != TLV_SCHEMA_DEFINITION_UNKNOWN) {
        lua_pushinteger(L, detail->definition.kind);
        lua_setfield(L, -2, "definition_kind");
        size_field(L, "definition_index", detail->definition.index, 0);
    }
    if (detail->field) {
        lua_pushstring(L, detail->field);
        lua_setfield(L, -2, "field");
    }
    lua_pushboolean(L, detail->is_group);
    lua_setfield(L, -2, "is_group");
    if (detail->has_occurs)
        bounds(L, "occurrences", detail->min_occurs, detail->max_occurs, detail->occurs);
    if (detail->has_length) {
        bounds(L, "length", detail->min_length, detail->max_length, detail->actual_length);
        size_field(L, "length_multiple", detail->length_multiple, 0);
        size_field(L, "length_flags", detail->length_flags, 0);
    }
    if (detail->has_form) {
        lua_newtable(L);
        lua_pushstring(L, kinds[detail->expected_form]);
        lua_setfield(L, -2, "expected");
        lua_pushboolean(L, detail->actual_constructed);
        lua_setfield(L, -2, "actual_constructed");
        lua_setfield(L, -2, "form");
    }
}

static void push_result(lua_State* L, tlv_result_t code, size_t total, int truncated) {
    lua_newtable(L);
    lua_pushboolean(L, code == TLV_OK);
    lua_setfield(L, -2, "ok");
    lua_pushinteger(L, (lua_Integer)code);
    lua_setfield(L, -2, "code");
    size_field(L, "total_count", total, 0);
    lua_pushboolean(L, truncated);
    lua_setfield(L, -2, "truncated");
    lua_newtable(L);
}

/* validate(data, format, options) for a Schema (`checked` NULL) or a checked
 * Schema; `first` stops at the first failure through tlv_schema_validate() or
 * tlv_schema_validate_checked() instead of collecting every violation. */
static int validate(lua_State* L, const tlv_structure_schema_t* schema,
                    const tlv_schema_checked_t* checked) {
    static const char* const policies[] = {"by_schema", "allow", "reject", NULL};
    luaL_checktype(L, 2, LUA_TSTRING);
    size_t      size;
    const char* data = lua_tolstring(L, 2, &size);
    lua_settop(L, 4);
    if (lua_isnil(L, 3)) {
        lua_getfield(L, LUA_REGISTRYINDEX, OPENTLV_LUA_DEFAULT_FORMAT_KEY);
        if (lua_isnil(L, -1)) return luaL_argerror(L, 3, "format is required when BER is disabled");
        lua_replace(L, 3);
    }
    tlv_lua_format_t* format = opentlv_lua_check_format(L, 3);
    if (lua_isnil(L, 4)) {
        lua_newtable(L);
        lua_replace(L, 4);
    }
    luaL_checktype(L, 4, LUA_TTABLE);
    size_t                      capacity = number_option(L, 4, "capacity", 64, 0);
    size_t                      max_depth = number_option(L, 4, "max_depth", 32, 0);
    size_t                      max_elements = number_option(L, 4, "max_elements", 100000, 1);
    tlv_schema_unknown_policy_t unknown =
        (tlv_schema_unknown_policy_t)enum_option(L, 4, "unknown", policies);
    field(L, 4, "first");
    if (!lua_isnil(L, -1)) luaL_checktype(L, -1, LUA_TBOOLEAN);
    int first = lua_toboolean(L, -1);
    lua_pop(L, 1);
    tlv_schema_diagnostic_t offset;
    tlv_schema_diagnostic_init(&offset);
    if (first) {
        if (unknown != TLV_SCHEMA_UNKNOWN_BY_SCHEMA)
            return luaL_argerror(L, 4, "first validation follows each scope's allow_unknown");
        tlv_result_t code =
            checked ? tlv_schema_validate_checked(checked, (const uint8_t*)data, size,
                                                  &format->format, max_depth, max_elements, &offset)
                    : tlv_schema_validate((const uint8_t*)data, size, &format->format, schema,
                                          max_depth, max_elements, &offset);
        push_result(L, code, code != TLV_OK, 0);
        if (code != TLV_OK) {
            offset.diagnostic.code = code;
            opentlv_lua_push_schema_diagnostic(L, &offset);
            lua_rawseti(L, -2, 1);
        }
        lua_setfield(L, -2, "diagnostics");
        return 1;
    }
    if (capacity > INT_MAX || capacity > SIZE_MAX / sizeof(tlv_schema_diagnostic_t))
        return luaL_error(L, "diagnostic capacity is too large");
    /* Temporary report storage stays rooted on the Lua stack until conversion
     * completes. Lua allocation errors cannot leak native allocations. */
    tlv_schema_diagnostic_report_t report;
    report.diagnostics =
        capacity ? lua_newuserdata(L, capacity * sizeof(*report.diagnostics)) : NULL;
    report.capacity = capacity;
    report.count = 0;
    tlv_result_t code =
        checked
            ? tlv_schema_validate_all_checked(checked, (const uint8_t*)data, size, &format->format,
                                              max_depth, max_elements, unknown, &report, &offset)
            : tlv_schema_validate_all_diag((const uint8_t*)data, size, &format->format, schema,
                                           max_depth, max_elements, unknown, &report, &offset);
    push_result(L, code, code == TLV_OK || code == TLV_ERR_SCHEMA ? report.count : 1,
                report.count > capacity);
    if (code == TLV_OK || code == TLV_ERR_SCHEMA) {
        size_t count = report.count < capacity ? report.count : capacity;
        for (size_t i = 0; i < count; ++i) {
            opentlv_lua_push_schema_diagnostic(L, &report.diagnostics[i]);
            lua_rawseti(L, -2, (int)i + 1);
        }
    } else {
        offset.diagnostic.code = code;
        opentlv_lua_push_schema_diagnostic(L, &offset);
        lua_rawseti(L, -2, 1);
    }
    lua_setfield(L, -2, "diagnostics");
    return 1;
}

static int schema_validate(lua_State* L) {
    return validate(L, &check_schema(L, 1)->schema, NULL);
}

/* schema:check() -> CheckedSchema, or raises the definition failure. */
static int schema_check(lua_State* L) {
    lua_schema_t*         schema = check_schema(L, 1);
    lua_checked_schema_t* self = (lua_checked_schema_t*)lua_newuserdata(L, sizeof(*self));
    self->checked.schema = NULL;
    self->schema_ref = LUA_NOREF;
    luaL_getmetatable(L, CHECKED_SCHEMA_MT);
    lua_setmetatable(L, -2);
    lua_pushvalue(L, 1);
    self->schema_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    tlv_schema_diagnostic_t diagnostic;
    tlv_schema_diagnostic_init(&diagnostic);
    tlv_result_t code = tlv_schema_prepare(&self->checked, &schema->schema, &diagnostic);
    if (code != TLV_OK) {
        diagnostic.diagnostic.code = code;
        opentlv_lua_push_schema_diagnostic(L, &diagnostic);
        return lua_error(L);
    }
    return 1;
}

static int checked_validate(lua_State* L) {
    lua_checked_schema_t* self = (lua_checked_schema_t*)luaL_checkudata(L, 1, CHECKED_SCHEMA_MT);
    if (!self->checked.schema) return luaL_error(L, "schema has been collected");
    return validate(L, NULL, &self->checked);
}

static int checked_schema(lua_State* L) {
    lua_checked_schema_t* self = (lua_checked_schema_t*)luaL_checkudata(L, 1, CHECKED_SCHEMA_MT);
    lua_rawgeti(L, LUA_REGISTRYINDEX, self->schema_ref);
    return 1;
}

/* opentlv.length_schema(rules) snapshots per-tag length rules. */
static int length_schema_new(lua_State* L) {
    int                  references;
    size_t               count = array_count(L, 1);
    lua_length_schema_t* self = (lua_length_schema_t*)new_snapshot(L, sizeof(lua_length_schema_t),
                                                                   LENGTH_SCHEMA_MT, &references);
    int                  result = references - 1;
    tlv_schema_entry_t*  entries = storage(L, references, count, sizeof(*entries));
    for (size_t i = 0; i < count; ++i) {
        lua_rawgeti(L, 1, (int)i + 1);
        luaL_checktype(L, -1, LUA_TTABLE);
        parse_entry(L, lua_gettop(L), references, &entries[i]);
        lua_pop(L, 1);
    }
    self->schema.entries = entries;
    self->schema.count = count;
    lua_settop(L, result);
    return 1;
}

static const tlv_schema_entry_t* find_entry(lua_State* L, lua_length_schema_t** self) {
    *self = (lua_length_schema_t*)luaL_checkudata(L, 1, LENGTH_SCHEMA_MT);
    size_t      size;
    const char* data = luaL_checklstring(L, 2, &size);
    tlv_tag_t   tag = tlv_tag((const uint8_t*)data, size);
    return tlv_schema_find(&(*self)->schema, &tag);
}

/* find(tag) -> a copy of the first rule for `tag`, or nil. */
static int length_schema_find(lua_State* L) {
    lua_length_schema_t*      self;
    const tlv_schema_entry_t* entry = find_entry(L, &self);
    if (!entry) {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);
    lua_pushlstring(L, (const char*)entry->tag.data, entry->tag.size);
    lua_setfield(L, -2, "tag");
    if (entry->name) {
        lua_pushstring(L, entry->name);
        lua_setfield(L, -2, "name");
    }
    size_field(L, "min_length", entry->min_length, 0);
    size_field(L, "max_length", entry->max_length, 1);
    size_field(L, "length_multiple", entry->length_multiple, 0);
    size_field(L, "flags", entry->flags, 0);
    return 1;
}

/* validate_length(tag, length) -> boolean; an unknown tag is not permitted.
 * Invalid rule definitions raise. */
static int length_schema_validate(lua_State* L) {
    lua_length_schema_t*      self;
    const tlv_schema_entry_t* entry = find_entry(L, &self);
    size_t                    length = opentlv_lua_check_size(L, 3, "length");
    tlv_result_t code = entry ? tlv_schema_validate_length(entry, length) : TLV_ERR_SCHEMA;
    if (code != TLV_OK && code != TLV_ERR_SCHEMA) return opentlv_lua_raise(L, code, 0, 0);
    lua_pushboolean(L, code == TLV_OK);
    return 1;
}

/* opentlv.value_constraint(config) checks the definition on construction. */
static int constraint_new(lua_State* L) {
    static const char* const kinds[] = {"none", "range", "allowed_values", NULL};
    luaL_checktype(L, 1, LUA_TTABLE);
    int                     references;
    lua_value_constraint_t* self = (lua_value_constraint_t*)new_snapshot(
        L, sizeof(lua_value_constraint_t), VALUE_CONSTRAINT_MT, &references);
    int                     result = references - 1;
    tlv_value_constraint_t* constraint = &self->constraint;
    constraint->kind = (tlv_value_constraint_kind_t)enum_option(L, 1, "kind", kinds);
    if (constraint->kind == TLV_VALUE_CONSTRAINT_RANGE) {
        field(L, 1, "min_value");
        constraint->min_value = opentlv_lua_codec_int(L, lua_gettop(L), INT64_MIN, INT64_MAX);
        field(L, 1, "max_value");
        constraint->max_value = opentlv_lua_codec_int(L, lua_gettop(L), INT64_MIN, INT64_MAX);
        lua_pop(L, 2);
    } else if (constraint->kind == TLV_VALUE_CONSTRAINT_ALLOWED_VALUES) {
        field(L, 1, "allowed_values");
        int      list = lua_gettop(L);
        size_t   count = array_count(L, list);
        int64_t* values = storage(L, references, count, sizeof(*values));
        for (size_t i = 0; i < count; ++i) {
            lua_rawgeti(L, list, (int)i + 1);
            values[i] = opentlv_lua_codec_int(L, lua_gettop(L), INT64_MIN, INT64_MAX);
            lua_pop(L, 1);
        }
        constraint->allowed_values = values;
        constraint->allowed_values_count = count;
        field(L, 1, "allowed_value_names");
        if (!lua_isnil(L, -1)) {
            int names_list = lua_gettop(L);
            luaL_checktype(L, names_list, LUA_TTABLE);
            /* Parallel to allowed_values; a nil entry is a value without a name. */
            const char** names = storage(L, references, count, sizeof(*names));
            for (size_t i = 0; i < count; ++i) {
                lua_rawgeti(L, names_list, (int)i + 1);
                if (lua_isnil(L, -1))
                    lua_pop(L, 1);
                else
                    names[i] = retained_name(L, references);
            }
            constraint->allowed_value_names = names;
        }
        lua_pop(L, 2);
    }
    tlv_result_t code = tlv_value_constraint_check(constraint);
    if (code != TLV_OK) return opentlv_lua_raise(L, code, 0, 0);
    lua_settop(L, result);
    return 1;
}

static lua_value_constraint_t* check_constraint(lua_State* L, int64_t* value) {
    lua_value_constraint_t* self =
        (lua_value_constraint_t*)luaL_checkudata(L, 1, VALUE_CONSTRAINT_MT);
    *value = opentlv_lua_codec_int(L, 2, INT64_MIN, INT64_MAX);
    return self;
}

/* validate(value) -> boolean */
static int constraint_validate(lua_State* L) {
    int64_t                 value;
    lua_value_constraint_t* self = check_constraint(L, &value);
    tlv_result_t            code = tlv_value_constraint_validate(&self->constraint, value);
    if (code != TLV_OK && code != TLV_ERR_SCHEMA) return opentlv_lua_raise(L, code, 0, 0);
    lua_pushboolean(L, code == TLV_OK);
    return 1;
}

/* name(value) -> display name of an allowed value, or nil */
static int constraint_name(lua_State* L) {
    int64_t                 value;
    lua_value_constraint_t* self = check_constraint(L, &value);
    const char*             name = tlv_value_constraint_name(&self->constraint, value);
    if (name)
        lua_pushstring(L, name);
    else
        lua_pushnil(L);
    return 1;
}

void opentlv_lua_open_schema(lua_State* L, int module_table_index) {
    static const opentlv_lua_method_t methods[] = {
        {"validate", schema_validate}, {"check", schema_check}, {NULL, NULL}};
    static const opentlv_lua_method_t checked_methods[] = {
        {"validate", checked_validate}, {"schema", checked_schema}, {NULL, NULL}};
    static const opentlv_lua_method_t length_methods[] = {
        {"find", length_schema_find}, {"validate_length", length_schema_validate}, {NULL, NULL}};
    static const opentlv_lua_method_t constraint_methods[] = {
        {"validate", constraint_validate}, {"name", constraint_name}, {NULL, NULL}};
    opentlv_lua_new_type(L, SCHEMA_MT, methods, schema_gc, NULL, NULL);
    opentlv_lua_new_type(L, CHECKED_SCHEMA_MT, checked_methods, checked_gc, NULL, NULL);
    opentlv_lua_new_type(L, LENGTH_SCHEMA_MT, length_methods, length_schema_gc, NULL, NULL);
    opentlv_lua_new_type(L, VALUE_CONSTRAINT_MT, constraint_methods, constraint_gc, NULL, NULL);
    lua_pushcfunction(L, schema_new);
    lua_setfield(L, module_table_index, "schema");
    lua_pushcfunction(L, length_schema_new);
    lua_setfield(L, module_table_index, "length_schema");
    lua_pushcfunction(L, constraint_new);
    lua_setfield(L, module_table_index, "value_constraint");
    lua_pushinteger(L, TLV_SCHEMA_LENGTH_ENDPOINTS);
    lua_setfield(L, module_table_index, "SCHEMA_LENGTH_ENDPOINTS");
}
