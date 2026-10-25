// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "common.h"

#include <tlv/size.h>

#include <stdint.h>

void opentlv_lua_new_type(lua_State* L, const char* name, const opentlv_lua_method_t* methods,
                          lua_CFunction gc, lua_CFunction tostring, lua_CFunction call) {
    luaL_newmetatable(L, name);

    if (methods != NULL) {
        lua_newtable(L);
        for (const opentlv_lua_method_t* method = methods; method->name != NULL; method++) {
            lua_pushcfunction(L, method->fn);
            lua_setfield(L, -2, method->name);
        }
        lua_setfield(L, -2, "__index");
    }
    if (gc != NULL) {
        lua_pushcfunction(L, gc);
        lua_setfield(L, -2, "__gc");
    }
    if (tostring != NULL) {
        lua_pushcfunction(L, tostring);
        lua_setfield(L, -2, "__tostring");
    }
    if (call != NULL) {
        lua_pushcfunction(L, call);
        lua_setfield(L, -2, "__call");
    }
    lua_pop(L, 1);
}

int opentlv_lua_push_element(lua_State* L, const tlv_element_t* element, size_t offset) {
    size_t       value_length;
    tlv_result_t code = tlv_size_to_native(element->value.size, &value_length);
    if (code != TLV_OK) {
        return (int)code;
    }

    lua_newtable(L);
    lua_pushlstring(L, (const char*)element->tag.data, element->tag.size);
    lua_setfield(L, -2, "tag");
    lua_pushinteger(L, (lua_Integer)value_length);
    lua_setfield(L, -2, "length");
    lua_pushlstring(L, value_length > 0 ? (const char*)element->value.data : "", value_length);
    lua_setfield(L, -2, "value");
    lua_pushinteger(L, (lua_Integer)offset);
    lua_setfield(L, -2, "offset");
    return (int)TLV_OK;
}

size_t opentlv_lua_check_size(lua_State* L, int index, const char* name) {
    luaL_checktype(L, index, LUA_TNUMBER);
#if LUA_VERSION_NUM >= 503
    if (lua_isinteger(L, index)) {
        lua_Integer value = lua_tointeger(L, index);
        if (value < 0 || (uintmax_t)value > SIZE_MAX)
            luaL_error(L, "%s must be a nonnegative native integer", name);
        return (size_t)value;
    }
#endif
    lua_Number value = lua_tonumber(L, index);
    /* Strict upper bound avoids floating-point rounding of SIZE_MAX. */
    if (!(value >= 0 && value < (lua_Number)SIZE_MAX))
        luaL_error(L, "%s must be a nonnegative native integer", name);
    size_t result = (size_t)value;
    if ((lua_Number)result != value) luaL_error(L, "%s must be an integer", name);
    return result;
}

size_t opentlv_lua_size_option(lua_State* L, int options_index, const char* name, size_t fallback) {
    if (lua_isnoneornil(L, options_index)) return fallback;
    lua_getfield(L, options_index, name);
    size_t result = lua_isnil(L, -1) ? fallback : opentlv_lua_check_size(L, -1, name);
    lua_pop(L, 1);
    return result;
}

int opentlv_lua_boolean_option(lua_State* L, int options_index, const char* name, int fallback) {
    if (lua_isnoneornil(L, options_index)) return fallback;
    lua_getfield(L, options_index, name);
    int result = fallback;
    if (!lua_isnil(L, -1)) {
        if (!lua_isboolean(L, -1)) luaL_error(L, "%s must be a boolean", name);
        result = lua_toboolean(L, -1);
    }
    lua_pop(L, 1);
    return result;
}

void opentlv_lua_check_element(lua_State* L, int index, tlv_element_t* element) {
    luaL_checktype(L, index, LUA_TTABLE);
    lua_getfield(L, index, "tag");
    lua_getfield(L, index, "value");
    size_t size = 0;
    element->tag = tlv_tag(NULL, 0);
    if (!lua_isnil(L, -2)) {
        if (lua_type(L, -2) != LUA_TSTRING) luaL_error(L, "element tag must be a string or nil");
        const char* tag = lua_tolstring(L, -2, &size);
        element->tag = tlv_tag((const uint8_t*)tag, size);
    }
    if (lua_type(L, -1) != LUA_TSTRING) luaL_error(L, "element value must be a string");
    element->value.data = (const uint8_t*)lua_tolstring(L, -1, &size);
    element->value.size = (tlv_size_t)size;
}
