// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "format.h"
#include "common.h"

#include <tlv/config.h>

tlv_lua_format_t* opentlv_lua_check_format(lua_State* L, int arg) {
    return (tlv_lua_format_t*)luaL_checkudata(L, arg, OPENTLV_LUA_FORMAT_MT);
}

static int format_tostring(lua_State* L) {
    tlv_lua_format_t* format = opentlv_lua_check_format(L, 1);
    lua_pushfstring(L, "opentlv.Format<%s>", format->name);
    return 1;
}

void opentlv_lua_register_builtin(lua_State* L, tlv_format_t format_value, int use_der_validation,
                                  const char* name) {
    tlv_lua_format_t* format = (tlv_lua_format_t*)lua_newuserdata(L, sizeof(tlv_lua_format_t));
    format->format = format_value;
    format->use_der_validation = use_der_validation;
    format->name = name;
    luaL_getmetatable(L, OPENTLV_LUA_FORMAT_MT);
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, name);
}

void opentlv_lua_open_format(lua_State* L, int module_table_index) {
    opentlv_lua_new_type(L, OPENTLV_LUA_FORMAT_MT, NULL, NULL, format_tostring, NULL);

    lua_newtable(L); /* formats */

#if OPENTLV_FORMAT_BER
    opentlv_lua_register_ber(L);
#endif
#if OPENTLV_FORMAT_CER
    opentlv_lua_register_cer(L);
#endif
#if OPENTLV_FORMAT_DER
    opentlv_lua_register_der(L);
#endif
#if OPENTLV_BLUETOOTH
    opentlv_lua_register_bluetooth_ltv(L);
#endif
#if OPENTLV_NFC
    opentlv_lua_register_nfc_type2(L);
#endif
#if OPENTLV_LLDP
    opentlv_lua_register_lldp(L);
#endif
#if OPENTLV_EMV
    opentlv_lua_register_emv(L);
#endif
    opentlv_lua_register_fixed(L);

    lua_getfield(L, -1, "ber");
    lua_setfield(L, LUA_REGISTRYINDEX, OPENTLV_LUA_DEFAULT_FORMAT_KEY);

    lua_setfield(L, module_table_index, "formats");
}
