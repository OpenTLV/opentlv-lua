// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "schema.h"
#include "common.h"
#include "error.h"
#include "format.h"

#include <tlv/schema/schema.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define SCHEMA_MT "opentlv.Schema"

/* One registry table owns the native arrays (Lua userdata), immutable strings
 * and child Schema objects. Install __gc before acquiring any references so
 * constructor failures, including Lua allocation failures, are collectable. */
typedef struct lua_schema {
    tlv_structure_schema_t schema;
    int                    references;
} lua_schema_t;

static lua_schema_t* check_schema(lua_State* L, int index) {
    lua_schema_t* self = (lua_schema_t*)luaL_checkudata(L, index, SCHEMA_MT);
    if (self->references == LUA_NOREF) luaL_error(L, "schema has been collected");
    return self;
}

static int schema_gc(lua_State* L) {
    lua_schema_t* self = (lua_schema_t*)luaL_checkudata(L, 1, SCHEMA_MT);
    luaL_unref(L, LUA_REGISTRYINDEX, self->references);
    self->references = LUA_NOREF;
    return 0;
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

static const char* name_option(lua_State* L, int table, int references) {
    field(L, table, "name");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return NULL;
    }
    luaL_checktype(L, -1, LUA_TSTRING);
    size_t      size;
    const char* name = lua_tolstring(L, -1, &size);
    if (memchr(name, 0, size)) luaL_error(L, "schema names cannot contain NUL bytes");
    luaL_ref(L, references);
    return name;
}

static int schema_new(lua_State* L) {
    static const char* const orders[] = {"any", "sequence", NULL};
    static const char* const kinds[] = {"any", "primitive", "constructed", NULL};
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_schema_t* self = (lua_schema_t*)lua_newuserdata(L, sizeof(*self));
    memset(self, 0, sizeof(*self));
    self->references = LUA_NOREF;
    int result = lua_gettop(L);
    luaL_getmetatable(L, SCHEMA_MT);
    lua_setmetatable(L, result);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    self->references = luaL_ref(L, LUA_REGISTRYINDEX);
    int references = lua_gettop(L);
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
            field(L, rule, "tag");
            luaL_checktype(L, -1, LUA_TSTRING);
            size_t      size;
            const char* tag = lua_tolstring(L, -1, &size);
            entries[i].tag = tlv_tag((const uint8_t*)tag, size);
            luaL_ref(L, references);
            entries[i].name = name_option(L, rule, references);
            entries[i].min_length = number_option(L, rule, "min_length", 0, 0);
            entries[i].max_length = number_option(L, rule, "max_length", SIZE_MAX, 1);
            entries[i].length_multiple = number_option(L, rule, "length_multiple", 0, 0);
            entries[i].flags = uint_option(L, rule, "flags");
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

static int schema_validate(lua_State* L) {
    static const char* const policies[] = {"by_schema", "allow", "reject", NULL};
    lua_schema_t*            self = check_schema(L, 1);
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
    if (capacity > INT_MAX || capacity > SIZE_MAX / sizeof(tlv_schema_diagnostic_t))
        return luaL_error(L, "diagnostic capacity is too large");
    /* Temporary report storage stays rooted on the Lua stack until conversion
     * completes. Lua allocation errors cannot leak native allocations. */
    tlv_schema_diagnostic_report_t report;
    report.diagnostics =
        capacity ? lua_newuserdata(L, capacity * sizeof(*report.diagnostics)) : NULL;
    report.capacity = capacity;
    report.count = 0;
    tlv_schema_diagnostic_t offset = {0};
    tlv_result_t            code =
        tlv_schema_validate_all_diag((const uint8_t*)data, size, &format->format, &self->schema,
                                     max_depth, max_elements, unknown, &report, &offset);
    lua_newtable(L);
    lua_pushboolean(L, code == TLV_OK);
    lua_setfield(L, -2, "ok");
    lua_pushinteger(L, (lua_Integer)code);
    lua_setfield(L, -2, "code");
    size_field(L, "total_count", code == TLV_OK || code == TLV_ERR_SCHEMA ? report.count : 1, 0);
    lua_pushboolean(L, report.count > capacity);
    lua_setfield(L, -2, "truncated");
    lua_newtable(L);
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

void opentlv_lua_open_schema(lua_State* L, int module_table_index) {
    static const opentlv_lua_method_t methods[] = {{"validate", schema_validate}, {NULL, NULL}};
    opentlv_lua_new_type(L, SCHEMA_MT, methods, schema_gc, NULL, NULL);
    lua_pushcfunction(L, schema_new);
    lua_setfield(L, module_table_index, "schema");
    lua_pushinteger(L, TLV_SCHEMA_LENGTH_ENDPOINTS);
    lua_setfield(L, module_table_index, "SCHEMA_LENGTH_ENDPOINTS");
}
