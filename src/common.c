// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "common.h"

#include <tlv/size.h>

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
