// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "../format.h"
#include "../error.h"

#include <string.h>

/* opentlv.formats.fixed(tag_size, length_size, byte_order) -> Format
 *
 * byte_order is "big" or "little", matching tlv_byte_order_t. */
static int l_format_fixed(lua_State* L) {
    lua_Integer tag_size = luaL_checkinteger(L, 1);
    lua_Integer length_size = luaL_checkinteger(L, 2);
    const char* order_name = luaL_checkstring(L, 3);

    if (tag_size <= 0) {
        return luaL_argerror(L, 1, "tag_size must be positive");
    }
    if (length_size <= 0) {
        return luaL_argerror(L, 2, "length_size must be positive");
    }
    tlv_byte_order_t order;
    if (strcmp(order_name, "big") == 0) {
        order = TLV_BYTE_ORDER_BIG_ENDIAN;
    } else if (strcmp(order_name, "little") == 0) {
        order = TLV_BYTE_ORDER_LITTLE_ENDIAN;
    } else {
        return luaL_argerror(L, 3, "expected \"big\" or \"little\"");
    }

    tlv_lua_format_t* format = (tlv_lua_format_t*)lua_newuserdata(L, sizeof(tlv_lua_format_t));
    format->fixed_config.identifier.size = (size_t)tag_size;
    format->fixed_config.length.size = (size_t)length_size;
    format->fixed_config.length.byte_order = order;
    format->fixed_config.element_order = TLV_ELEMENT_ORDER_TLV;
    format->fixed_config.length_scope = TLV_LENGTH_SCOPE_VALUE;
    format->use_der_validation = 0;
    format->name = "fixed";

    tlv_result_t code = tlv_fixed_format_init(&format->format, &format->fixed_config);
    if (code != TLV_OK) {
        return opentlv_lua_raise(L, code, 0, 0);
    }

    luaL_getmetatable(L, OPENTLV_LUA_FORMAT_MT);
    lua_setmetatable(L, -2);
    return 1;
}

void opentlv_lua_register_fixed(lua_State* L) {
    lua_pushcfunction(L, l_format_fixed);
    lua_setfield(L, -2, "fixed");
}
