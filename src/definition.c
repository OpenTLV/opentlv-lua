// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "definition.h"
#include "common.h"
#include "error.h"

#include <tlv/definition.h>
#include <limits.h>
#include <string.h>

#define DEFINITION_REGISTRY_MT "opentlv.DefinitionRegistry"

/* Snapshot of the definition array; Core looks entries up in place. The
 * userdata environment roots the entry block and every Tag and name string. */
typedef struct lua_definition_registry {
    tlv_definition_registry_t registry;
} lua_definition_registry_t;

/* opentlv.definition_registry{{tag = ..., name = ...}, ...} */
static int registry_new(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 1);
#if LUA_VERSION_NUM >= 502
    size_t count = lua_rawlen(L, 1);
#else
    size_t count = lua_objlen(L, 1);
#endif
    if (count > INT_MAX / 2 || count > SIZE_MAX / sizeof(tlv_definition_t))
        return luaL_error(L, "definition table is too large");
    lua_definition_registry_t* self = lua_newuserdata(L, sizeof(*self));
    memset(self, 0, sizeof(*self));
    luaL_getmetatable(L, DEFINITION_REGISTRY_MT);
    lua_setmetatable(L, -2);
    lua_newtable(L);
    tlv_definition_t* entries = NULL;
    if (count) {
        entries = lua_newuserdata(L, count * sizeof(*entries));
        memset(entries, 0, count * sizeof(*entries));
        lua_setfield(L, 3, "entries");
    }
    for (size_t i = 0; i < count; ++i) {
        lua_rawgeti(L, 1, (int)i + 1);
        if (!lua_istable(L, 4)) return luaL_error(L, "definition %d must be a table", (int)i + 1);
        lua_getfield(L, 4, "tag");
        if (lua_type(L, -1) != LUA_TSTRING)
            return luaL_error(L, "definition %d tag must be a string", (int)i + 1);
        size_t      size;
        const char* tag = lua_tolstring(L, -1, &size);
        entries[i].tag = tlv_tag((const uint8_t*)tag, size);
        lua_rawseti(L, 3, 2 * (int)i + 1);
        lua_getfield(L, 4, "name");
        if (!lua_isnil(L, -1)) {
            if (lua_type(L, -1) != LUA_TSTRING)
                return luaL_error(L, "definition %d name must be a string or nil", (int)i + 1);
            entries[i].name = lua_tolstring(L, -1, &size);
            /* Core names are NUL-terminated. */
            if (memchr(entries[i].name, 0, size))
                return opentlv_lua_raise(L, TLV_ERR_INVALID_ARG, 0, 0);
            lua_rawseti(L, 3, 2 * (int)i + 2);
        } else
            lua_pop(L, 1);
        lua_pop(L, 1);
    }
    self->registry.entries = entries;
    self->registry.count = count;
#if LUA_VERSION_NUM == 501
    lua_setfenv(L, 2);
#else
    lua_setuservalue(L, 2);
#endif
    return 1;
}

static lua_definition_registry_t* check_registry(lua_State* L) {
    return (lua_definition_registry_t*)luaL_checkudata(L, 1, DEFINITION_REGISTRY_MT);
}

/* Pushes an owned {tag = ..., name = ...} table for one entry. */
static void push_definition(lua_State* L, const tlv_definition_t* definition) {
    lua_newtable(L);
    lua_pushlstring(L, (const char*)definition->tag.data, definition->tag.size);
    lua_setfield(L, -2, "tag");
    if (definition->name) {
        lua_pushstring(L, definition->name);
        lua_setfield(L, -2, "name");
    }
}

/* find(tag) -> definition, index for the first entry with an equal Tag, or nil. */
static int registry_find(lua_State* L) {
    const tlv_definition_registry_t* registry = &check_registry(L)->registry;
    size_t                           size;
    const char*                      data = luaL_checklstring(L, 2, &size);
    tlv_tag_t                        tag = tlv_tag((const uint8_t*)data, size);
    const tlv_definition_t*          found = tlv_definition_find(registry, &tag);
    if (!found) return 0;
    push_definition(L, found);
    lua_pushinteger(L, (lua_Integer)(found - registry->entries) + 1);
    return 2;
}

/* definitions() -> owned array of the entries in registry order. */
static int registry_definitions(lua_State* L) {
    const tlv_definition_registry_t* registry = &check_registry(L)->registry;
    lua_createtable(L, (int)registry->count, 0);
    for (size_t i = 0; i < registry->count; ++i) {
        push_definition(L, &registry->entries[i]);
        lua_rawseti(L, -2, (int)i + 1);
    }
    return 1;
}

void opentlv_lua_open_definition(lua_State* L, int module_table_index) {
    static const opentlv_lua_method_t methods[] = {
        {"find", registry_find}, {"definitions", registry_definitions}, {NULL, NULL}};
    opentlv_lua_new_type(L, DEFINITION_REGISTRY_MT, methods, NULL, NULL, NULL);
    lua_pushcfunction(L, registry_new);
    lua_setfield(L, module_table_index, "definition_registry");
}
