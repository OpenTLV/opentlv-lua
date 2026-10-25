// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "format.h"
#include "common.h"
#include "codec.h"
#include "error.h"

#include <tlv/config.h>
#include <tlv/size.h>

tlv_lua_format_t* opentlv_lua_check_format(lua_State* L, int arg) {
    return (tlv_lua_format_t*)luaL_checkudata(L, arg, OPENTLV_LUA_FORMAT_MT);
}

static int format_tostring(lua_State* L) {
    tlv_lua_format_t* format = opentlv_lua_check_format(L, 1);
    lua_pushfstring(L, "opentlv.Format<%s>", format->name);
    return 1;
}

static int format_can_read(lua_State* L) {
    lua_pushboolean(L, tlv_format_can_read(&opentlv_lua_check_format(L, 1)->format));
    return 1;
}

static int format_can_write(lua_State* L) {
    lua_pushboolean(L, tlv_format_can_write(&opentlv_lua_check_format(L, 1)->format));
    return 1;
}

/* Reads the (tag, value) arguments of measure() and encode(); a nil Tag is an
 * absent identifier, as for Writer. The strings are borrowed for the call. */
static tlv_element_t element_arguments(lua_State* L) {
    tlv_element_t element;
    element.tag = tlv_tag(NULL, 0);
    if (!lua_isnoneornil(L, 2)) {
        luaL_checktype(L, 2, LUA_TSTRING);
        size_t      size;
        const char* bytes = lua_tolstring(L, 2, &size);
        element.tag = tlv_tag((const uint8_t*)bytes, size);
    }
    luaL_checktype(L, 3, LUA_TSTRING);
    size_t length;
    element.value.data = (const uint8_t*)lua_tolstring(L, 3, &length);
    element.value.size = (tlv_size_t)length;
    return element;
}

/* format:decode(data) -> element, size
 *
 * Decodes one complete element at the start of data through
 * tlv_format_decode(); size is its encoded extent and trailing bytes are not
 * read. Empty input raises END. */
static int format_decode(lua_State* L) {
    tlv_lua_format_t* format = opentlv_lua_check_format(L, 1);
    luaL_checktype(L, 2, LUA_TSTRING);
    size_t             size;
    const char*        data = lua_tolstring(L, 2, &size);
    tlv_decoded_t      decoded;
    tlv_format_error_t error;
    tlv_result_t code = tlv_format_decode(&format->format, size ? (const uint8_t*)data : NULL, size,
                                          &decoded, &error);
    if (code != TLV_OK)
        return opentlv_lua_raise_format_error(L, code, "decode", code == TLV_END ? NULL : &error);
    int narrow_code = opentlv_lua_push_element(L, &decoded.element, 0);
    if (narrow_code != TLV_OK) return opentlv_lua_raise(L, (tlv_result_t)narrow_code, 0, 0);
    lua_pushinteger(L, (lua_Integer)decoded.source.size);
    return 2;
}

/* format:measure(tag, value) -> {header, value, trailer, total} */
static int format_measure(lua_State* L) {
    tlv_lua_format_t*  format = opentlv_lua_check_format(L, 1);
    tlv_element_t      element = element_arguments(L);
    tlv_encoding_t     encoding;
    tlv_format_error_t error;
    tlv_result_t       code = tlv_format_measure(&format->format, &element, &encoding, &error);
    if (code != TLV_OK) return opentlv_lua_raise_format_error(L, code, "measure", &error);
    lua_newtable(L);
    opentlv_lua_codec_push_uint(L, encoding.header);
    lua_setfield(L, -2, "header");
    opentlv_lua_codec_push_uint(L, encoding.value);
    lua_setfield(L, -2, "value");
    opentlv_lua_codec_push_uint(L, encoding.trailer);
    lua_setfield(L, -2, "trailer");
    opentlv_lua_codec_push_uint(L, encoding.total);
    lua_setfield(L, -2, "total");
    return 1;
}

/* format:encode(tag, value) -> string of exactly the measured size. */
static int format_encode(lua_State* L) {
    tlv_lua_format_t*  format = opentlv_lua_check_format(L, 1);
    tlv_element_t      element = element_arguments(L);
    tlv_encoding_t     encoding;
    tlv_format_error_t error;
    tlv_result_t       code = tlv_format_measure(&format->format, &element, &encoding, &error);
    if (code != TLV_OK) return opentlv_lua_raise_format_error(L, code, "measure", &error);
    size_t size;
    code = tlv_size_to_native(encoding.total, &size);
    if (code != TLV_OK) return opentlv_lua_raise(L, code, 0, 0);
    uint8_t* buffer = lua_newuserdata(L, size ? size : 1);
    size_t   written = 0;
    code = tlv_format_encode(&format->format, &element, buffer, size, &written, &error);
    if (code != TLV_OK) return opentlv_lua_raise_format_error(L, code, "encode", &error);
    lua_pushlstring(L, (const char*)buffer, written);
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
    static const opentlv_lua_method_t methods[] = {
        {"can_read", format_can_read}, {"can_write", format_can_write}, {"decode", format_decode},
        {"measure", format_measure},   {"encode", format_encode},       {NULL, NULL},
    };
    opentlv_lua_new_type(L, OPENTLV_LUA_FORMAT_MT, methods, NULL, format_tostring, NULL);

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
    opentlv_lua_register_compose(L);

    lua_getfield(L, -1, "ber");
    lua_setfield(L, LUA_REGISTRYINDEX, OPENTLV_LUA_DEFAULT_FORMAT_KEY);

    lua_setfield(L, module_table_index, "formats");
    opentlv_lua_open_fields(L, module_table_index);
}
