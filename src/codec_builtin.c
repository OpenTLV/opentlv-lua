// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

/* Explicit adapters for the documented native representations. These functions
 * copy fields, check host representation bounds and root borrowed Lua strings;
 * all Value validation and wire conversion is delegated to the C codecs. */
#include "codec.h"
#include <tlv/config.h>
#include <tlv/codec/values.h>
#include <tlv/codec/ipv4.h>
#include <tlv/builtins/dhcp/codec.h>
#include <tlv/builtins/lldp/codec.h>
#include <tlv/builtins/bluetooth/uuid.h>
#if OPENTLV_FORMAT_BER
#include <tlv/builtins/asn1/asn1_codec.h>
#endif
#if OPENTLV_BLUETOOTH
#include <tlv/builtins/bluetooth/ad_codec.h>
#include <tlv/builtins/bluetooth/service_data.h>
#include <tlv/builtins/bluetooth/manufacturer_data.h>
#endif
#if OPENTLV_EMV
#include <tlv/builtins/emv/presentation.h>
#endif
#include <limits.h>
#include <string.h>

static void push_bytes(lua_State* L, const uint8_t* data, size_t size) {
    lua_pushlstring(L, data ? (const char*)data : "", size);
}
static void get_bytes(lua_State* L, int index, void* value, size_t expected) {
    size_t      size;
    const char* bytes = opentlv_lua_codec_string(L, index, &size);
    if (size != expected) luaL_argerror(L, index, "incorrect byte string size");
    if (size) memcpy(value, bytes, size);
}
static size_t array_size(lua_State* L, int index, size_t maximum) {
    luaL_checktype(L, index, LUA_TTABLE);
#if LUA_VERSION_NUM == 501
    size_t size = lua_objlen(L, index);
#else
    size_t size = lua_rawlen(L, index);
#endif
    if (size > maximum) luaL_argerror(L, index, "array too large");
    return size;
}
static void push_u8(lua_State* L, const void* value) {
    const uint8_t* v = value;
    opentlv_lua_codec_push_uint(L, *v);
}
static void get_u8(lua_State* L, int index, void* value) {
    uint8_t* v = value;
    *v = (uint8_t)opentlv_lua_codec_uint(L, index, UINT8_MAX);
}
static const opentlv_lua_codec_rep_t rep_u8 = {sizeof(uint8_t), push_u8, get_u8};
static void                          push_u16(lua_State* L, const void* value) {
    const uint16_t* v = value;
    opentlv_lua_codec_push_uint(L, *v);
}
static void get_u16(lua_State* L, int index, void* value) {
    uint16_t* v = value;
    *v = (uint16_t)opentlv_lua_codec_uint(L, index, UINT16_MAX);
}
static const opentlv_lua_codec_rep_t rep_u16 = {sizeof(uint16_t), push_u16, get_u16};
static void                          push_u32(lua_State* L, const void* value) {
    const uint32_t* v = value;
    opentlv_lua_codec_push_uint(L, *v);
}
static void get_u32(lua_State* L, int index, void* value) {
    uint32_t* v = value;
    *v = (uint32_t)opentlv_lua_codec_uint(L, index, UINT32_MAX);
}
static const opentlv_lua_codec_rep_t rep_u32 = {sizeof(uint32_t), push_u32, get_u32};
#if OPENTLV_EMV
static void push_u64(lua_State* L, const void* value) {
    const uint64_t* v = value;
    opentlv_lua_codec_push_uint(L, *v);
}
static void get_u64(lua_State* L, int index, void* value) {
    uint64_t* v = value;
    *v = (uint64_t)opentlv_lua_codec_uint(L, index, UINT64_MAX);
}
static const opentlv_lua_codec_rep_t rep_u64 = {sizeof(uint64_t), push_u64, get_u64};
#endif
static void push_i64(lua_State* L, const void* value) {
    const int64_t* v = value;
    opentlv_lua_codec_push_int(L, *v);
}
static void get_i64(lua_State* L, int index, void* value) {
    int64_t* v = value;
    *v = (int64_t)opentlv_lua_codec_int(L, index, INT64_MIN, INT64_MAX);
}
static const opentlv_lua_codec_rep_t rep_i64 = {sizeof(int64_t), push_i64, get_i64};
static void                          push_value(lua_State* L, const void* value) {
    const tlv_value_t* v = value;
    push_bytes(L, v->data, (size_t)v->size);
}
static void get_value(lua_State* L, int index, void* value) {
    tlv_value_t* v = value;
    size_t       size;
    v->data = (const uint8_t*)opentlv_lua_codec_string(L, index, &size);
    v->size = size;
}
static const opentlv_lua_codec_rep_t rep_value = {sizeof(tlv_value_t), push_value, get_value};
static void                          push_ipv4(lua_State* L, const void* value) {
    const tlv_ipv4_t* v = value;
    push_bytes(L, v->bytes, 4);
}
static void get_ipv4(lua_State* L, int index, void* value) {
    tlv_ipv4_t* v = value;
    get_bytes(L, index, v->bytes, 4);
}
static const opentlv_lua_codec_rep_t rep_ipv4 = {sizeof(tlv_ipv4_t), push_ipv4, get_ipv4};
static void                          push_ipv4_list(lua_State* L, const void* value) {
    const tlv_ipv4_list_t* v = value;
    if (v->raw.size / 4 > INT_MAX) luaL_error(L, "array too large");
    lua_newtable(L);
    for (size_t i = 0; i < v->raw.size / 4; ++i) {
        tlv_ipv4_t   item;
        tlv_result_t code = tlv_ipv4_list_at(v, i, &item);
        if (code != TLV_OK) opentlv_lua_codec_raise(L, code);
        push_ipv4(L, &item);
        lua_rawseti(L, -2, (int)i + 1);
    }
}
static void get_ipv4_list(lua_State* L, int index, void* value) {
    tlv_ipv4_list_t* v = value;
    size_t           count = array_size(L, index, INT_MAX);
    if (count > SIZE_MAX / 4) luaL_error(L, "array too large");
    uint8_t* bytes = lua_newuserdata(L, count ? count * 4 : 1);
    for (size_t i = 0; i < count; ++i) {
        tlv_ipv4_t item;
        size_t     written;
        lua_rawgeti(L, index, (int)i + 1);
        get_ipv4(L, lua_gettop(L), &item);
        lua_pop(L, 1);
        tlv_result_t code =
            tlv_codec_encode(&tlv_codec_ipv4, &item, sizeof item, bytes + i * 4, 4, &written, NULL);
        if (code != TLV_OK) opentlv_lua_codec_raise(L, code);
    }
    v->raw.data = bytes;
    v->raw.size = count * 4;
}
static const opentlv_lua_codec_rep_t rep_ipv4_list = {sizeof(tlv_ipv4_list_t), push_ipv4_list,
                                                      get_ipv4_list};
#if OPENTLV_FORMAT_BER
static void push_boolean(lua_State* L, const void* value) {
    const bool* v = value;
    lua_pushboolean(L, *v);
}
static void get_boolean(lua_State* L, int index, void* value) {
    bool* v = value;
    luaL_checktype(L, index, LUA_TBOOLEAN);
    *v = lua_toboolean(L, index) != 0;
}
static const opentlv_lua_codec_rep_t rep_boolean = {sizeof(bool), push_boolean, get_boolean};
static void                          push_null(lua_State* L, const void* value) {
    (void)value;
    lua_pushnil(L);
}
static void get_null(lua_State* L, int index, void* value) {
    (void)value;
    luaL_checktype(L, index, LUA_TNIL);
}
static const opentlv_lua_codec_rep_t rep_null = {1, push_null, get_null};
static void                          push_octets(lua_State* L, const void* value) {
    const tlv_asn1_octet_string_t* v = value;
    push_bytes(L, v->data, v->length * 1);
}
static void get_octets(lua_State* L, int index, void* value) {
    tlv_asn1_octet_string_t* v = value;
    size_t                   size;
    v->data = (const uint8_t*)opentlv_lua_codec_string(L, index, &size);
    if (size % 1) luaL_argerror(L, index, "incomplete code unit");
    v->length = size / 1;
}
static const opentlv_lua_codec_rep_t rep_octets = {sizeof(tlv_asn1_octet_string_t), push_octets,
                                                   get_octets};
static void                          push_string(lua_State* L, const void* value) {
    const tlv_asn1_string_t* v = value;
    push_bytes(L, v->data, v->length * 1);
}
static void get_string(lua_State* L, int index, void* value) {
    tlv_asn1_string_t* v = value;
    size_t             size;
    v->data = (const uint8_t*)opentlv_lua_codec_string(L, index, &size);
    if (size % 1) luaL_argerror(L, index, "incomplete code unit");
    v->length = size / 1;
}
static const opentlv_lua_codec_rep_t rep_string = {sizeof(tlv_asn1_string_t), push_string,
                                                   get_string};
static void                          push_bmp(lua_State* L, const void* value) {
    const tlv_asn1_bmp_string_t* v = value;
    push_bytes(L, v->data, v->length * 2);
}
static void get_bmp(lua_State* L, int index, void* value) {
    tlv_asn1_bmp_string_t* v = value;
    size_t                 size;
    v->data = (const uint8_t*)opentlv_lua_codec_string(L, index, &size);
    if (size % 2) luaL_argerror(L, index, "incomplete code unit");
    v->length = size / 2;
}
static const opentlv_lua_codec_rep_t rep_bmp = {sizeof(tlv_asn1_bmp_string_t), push_bmp, get_bmp};
static void                          push_universal(lua_State* L, const void* value) {
    const tlv_asn1_universal_string_t* v = value;
    push_bytes(L, v->data, v->length * 4);
}
static void get_universal(lua_State* L, int index, void* value) {
    tlv_asn1_universal_string_t* v = value;
    size_t                       size;
    v->data = (const uint8_t*)opentlv_lua_codec_string(L, index, &size);
    if (size % 4) luaL_argerror(L, index, "incomplete code unit");
    v->length = size / 4;
}
static const opentlv_lua_codec_rep_t rep_universal = {sizeof(tlv_asn1_universal_string_t),
                                                      push_universal, get_universal};
static void                          push_bit_string(lua_State* L, const void* value) {
    const tlv_asn1_bit_string_t* v = value;
    lua_newtable(L);

    opentlv_lua_codec_push_uint(L, v->unused_bits);
    lua_setfield(L, -2, "unused_bits");

    push_bytes(L, v->data, v->length);
    lua_setfield(L, -2, "data");
}
static void get_bit_string(lua_State* L, int index, void* value) {
    tlv_asn1_bit_string_t* v = value;
    luaL_checktype(L, index, LUA_TTABLE);
    luaL_checkstack(L, 6, "codec fields");
    lua_getfield(L, index, "unused_bits");
    v->unused_bits = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "data");
    v->data = (const uint8_t*)opentlv_lua_codec_string(L, lua_gettop(L), &v->length);
}
static const opentlv_lua_codec_rep_t rep_bit_string = {sizeof(tlv_asn1_bit_string_t),
                                                       push_bit_string, get_bit_string};
static void                          push_utc_time(lua_State* L, const void* value) {
    const tlv_asn1_utc_time_t* v = value;
    lua_newtable(L);

    opentlv_lua_codec_push_int(L, v->year);
    lua_setfield(L, -2, "year");

    opentlv_lua_codec_push_uint(L, v->month);
    lua_setfield(L, -2, "month");

    opentlv_lua_codec_push_uint(L, v->day);
    lua_setfield(L, -2, "day");

    opentlv_lua_codec_push_uint(L, v->hour);
    lua_setfield(L, -2, "hour");

    opentlv_lua_codec_push_uint(L, v->minute);
    lua_setfield(L, -2, "minute");

    opentlv_lua_codec_push_uint(L, v->second);
    lua_setfield(L, -2, "second");
}
static void get_utc_time(lua_State* L, int index, void* value) {
    tlv_asn1_utc_time_t* v = value;
    luaL_checktype(L, index, LUA_TTABLE);
    luaL_checkstack(L, 10, "codec fields");
    lua_getfield(L, index, "year");
    v->year = (int32_t)opentlv_lua_codec_int(L, lua_gettop(L), INT32_MIN, INT32_MAX);
    lua_getfield(L, index, "month");
    v->month = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "day");
    v->day = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "hour");
    v->hour = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "minute");
    v->minute = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "second");
    v->second = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
}
static const opentlv_lua_codec_rep_t rep_utc_time = {sizeof(tlv_asn1_utc_time_t), push_utc_time,
                                                     get_utc_time};
static void                          push_generalized_time(lua_State* L, const void* value) {
    const tlv_asn1_generalized_time_t* v = value;
    lua_newtable(L);

    opentlv_lua_codec_push_int(L, v->year);
    lua_setfield(L, -2, "year");

    opentlv_lua_codec_push_uint(L, v->month);
    lua_setfield(L, -2, "month");

    opentlv_lua_codec_push_uint(L, v->day);
    lua_setfield(L, -2, "day");

    opentlv_lua_codec_push_uint(L, v->hour);
    lua_setfield(L, -2, "hour");

    opentlv_lua_codec_push_uint(L, v->minute);
    lua_setfield(L, -2, "minute");

    opentlv_lua_codec_push_uint(L, v->second);
    lua_setfield(L, -2, "second");

    push_bytes(L, v->fraction_digits, v->fraction_digits_length);
    lua_setfield(L, -2, "fraction_digits");
}
static void get_generalized_time(lua_State* L, int index, void* value) {
    tlv_asn1_generalized_time_t* v = value;
    luaL_checktype(L, index, LUA_TTABLE);
    luaL_checkstack(L, 11, "codec fields");
    lua_getfield(L, index, "year");
    v->year = (int32_t)opentlv_lua_codec_int(L, lua_gettop(L), INT32_MIN, INT32_MAX);
    lua_getfield(L, index, "month");
    v->month = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "day");
    v->day = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "hour");
    v->hour = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "minute");
    v->minute = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "second");
    v->second = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "fraction_digits");
    v->fraction_digits =
        (const uint8_t*)opentlv_lua_codec_string(L, lua_gettop(L), &v->fraction_digits_length);
    if (!v->fraction_digits_length) v->fraction_digits = NULL;
}
static const opentlv_lua_codec_rep_t rep_generalized_time = {
    sizeof(tlv_asn1_generalized_time_t), push_generalized_time, get_generalized_time};
static void push_date(lua_State* L, const void* value) {
    const tlv_asn1_date_t* v = value;
    lua_newtable(L);

    opentlv_lua_codec_push_int(L, v->year);
    lua_setfield(L, -2, "year");

    opentlv_lua_codec_push_uint(L, v->month);
    lua_setfield(L, -2, "month");

    opentlv_lua_codec_push_uint(L, v->day);
    lua_setfield(L, -2, "day");
}
static void get_date(lua_State* L, int index, void* value) {
    tlv_asn1_date_t* v = value;
    luaL_checktype(L, index, LUA_TTABLE);
    luaL_checkstack(L, 7, "codec fields");
    lua_getfield(L, index, "year");
    v->year = (int32_t)opentlv_lua_codec_int(L, lua_gettop(L), INT32_MIN, INT32_MAX);
    lua_getfield(L, index, "month");
    v->month = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "day");
    v->day = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
}
static const opentlv_lua_codec_rep_t rep_date = {sizeof(tlv_asn1_date_t), push_date, get_date};
static void                          push_time_of_day(lua_State* L, const void* value) {
    const tlv_asn1_time_of_day_t* v = value;
    lua_newtable(L);

    opentlv_lua_codec_push_uint(L, v->hour);
    lua_setfield(L, -2, "hour");

    opentlv_lua_codec_push_uint(L, v->minute);
    lua_setfield(L, -2, "minute");

    opentlv_lua_codec_push_uint(L, v->second);
    lua_setfield(L, -2, "second");
}
static void get_time_of_day(lua_State* L, int index, void* value) {
    tlv_asn1_time_of_day_t* v = value;
    luaL_checktype(L, index, LUA_TTABLE);
    luaL_checkstack(L, 7, "codec fields");
    lua_getfield(L, index, "hour");
    v->hour = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "minute");
    v->minute = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "second");
    v->second = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
}
static const opentlv_lua_codec_rep_t rep_time_of_day = {sizeof(tlv_asn1_time_of_day_t),
                                                        push_time_of_day, get_time_of_day};
static void                          push_date_time(lua_State* L, const void* value) {
    const tlv_asn1_date_time_t* v = value;
    lua_newtable(L);

    opentlv_lua_codec_push_int(L, v->year);
    lua_setfield(L, -2, "year");

    opentlv_lua_codec_push_uint(L, v->month);
    lua_setfield(L, -2, "month");

    opentlv_lua_codec_push_uint(L, v->day);
    lua_setfield(L, -2, "day");

    opentlv_lua_codec_push_uint(L, v->hour);
    lua_setfield(L, -2, "hour");

    opentlv_lua_codec_push_uint(L, v->minute);
    lua_setfield(L, -2, "minute");

    opentlv_lua_codec_push_uint(L, v->second);
    lua_setfield(L, -2, "second");
}
static void get_date_time(lua_State* L, int index, void* value) {
    tlv_asn1_date_time_t* v = value;
    luaL_checktype(L, index, LUA_TTABLE);
    luaL_checkstack(L, 10, "codec fields");
    lua_getfield(L, index, "year");
    v->year = (int32_t)opentlv_lua_codec_int(L, lua_gettop(L), INT32_MIN, INT32_MAX);
    lua_getfield(L, index, "month");
    v->month = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "day");
    v->day = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "hour");
    v->hour = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "minute");
    v->minute = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "second");
    v->second = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
}
static const opentlv_lua_codec_rep_t rep_date_time = {sizeof(tlv_asn1_date_time_t), push_date_time,
                                                      get_date_time};
static void                          push_oid(lua_State* L, const void* value) {
    const tlv_asn1_oid_t* v = value;
    lua_newtable(L);
    for (size_t i = 0; i < v->count; ++i) {
        opentlv_lua_codec_push_uint(L, v->arcs[i]);
        lua_rawseti(L, -2, (int)i + 1);
    }
}
static void get_oid(lua_State* L, int index, void* value) {
    tlv_asn1_oid_t* v = value;
    v->count = array_size(L, index, TLV_ASN1_OID_MAX_ARCS);
    for (size_t i = 0; i < v->count; ++i) {
        lua_rawgeti(L, index, (int)i + 1);
        v->arcs[i] = opentlv_lua_codec_uint(L, lua_gettop(L), UINT64_MAX);
        lua_pop(L, 1);
    }
}
static const opentlv_lua_codec_rep_t rep_oid = {sizeof(tlv_asn1_oid_t), push_oid, get_oid};
static void                          push_iri(lua_State* L, const void* value) {
    const tlv_asn1_iri_t* v = value;
    lua_newtable(L);
    for (size_t i = 0; i < v->count; ++i) {
        push_bytes(L, v->arcs[i].data, v->arcs[i].length);
        lua_rawseti(L, -2, (int)i + 1);
    }
}
static void get_iri(lua_State* L, int index, void* value) {
    tlv_asn1_iri_t* v = value;
    v->count = array_size(L, index, TLV_ASN1_IRI_MAX_ARCS);
    luaL_checkstack(L, (int)v->count + 1, "IRI arcs");
    for (size_t i = 0; i < v->count; ++i) {
        lua_rawgeti(L, index, (int)i + 1);
        v->arcs[i].data =
            (const uint8_t*)opentlv_lua_codec_string(L, lua_gettop(L), &v->arcs[i].length);
    }
}
static const opentlv_lua_codec_rep_t rep_iri = {sizeof(tlv_asn1_iri_t), push_iri, get_iri};
#endif
#if OPENTLV_LLDP
static void push_lldp_id(lua_State* L, const void* value) {
    const tlv_lldp_id_t* v = value;
    lua_newtable(L);

    opentlv_lua_codec_push_uint(L, v->subtype);
    lua_setfield(L, -2, "subtype");

    push_value(L, &v->identifier);
    lua_setfield(L, -2, "identifier");
}
static void get_lldp_id(lua_State* L, int index, void* value) {
    tlv_lldp_id_t* v = value;
    luaL_checktype(L, index, LUA_TTABLE);
    luaL_checkstack(L, 6, "codec fields");
    lua_getfield(L, index, "subtype");
    v->subtype = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "identifier");
    get_value(L, lua_gettop(L), &v->identifier);
}
static const opentlv_lua_codec_rep_t rep_lldp_id = {sizeof(tlv_lldp_id_t), push_lldp_id,
                                                    get_lldp_id};
static void                          push_lldp_capabilities(lua_State* L, const void* value) {
    const tlv_lldp_capabilities_t* v = value;
    lua_newtable(L);

    opentlv_lua_codec_push_uint(L, v->supported);
    lua_setfield(L, -2, "supported");

    opentlv_lua_codec_push_uint(L, v->enabled);
    lua_setfield(L, -2, "enabled");
}
static void get_lldp_capabilities(lua_State* L, int index, void* value) {
    tlv_lldp_capabilities_t* v = value;
    luaL_checktype(L, index, LUA_TTABLE);
    luaL_checkstack(L, 6, "codec fields");
    lua_getfield(L, index, "supported");
    v->supported = (uint16_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT16_MAX);
    lua_getfield(L, index, "enabled");
    v->enabled = (uint16_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT16_MAX);
}
static const opentlv_lua_codec_rep_t rep_lldp_capabilities = {
    sizeof(tlv_lldp_capabilities_t), push_lldp_capabilities, get_lldp_capabilities};
static void push_lldp_management(lua_State* L, const void* value) {
    const tlv_lldp_management_address_t* v = value;
    lua_newtable(L);

    opentlv_lua_codec_push_uint(L, v->address_subtype);
    lua_setfield(L, -2, "address_subtype");

    push_value(L, &v->address);
    lua_setfield(L, -2, "address");

    opentlv_lua_codec_push_uint(L, v->interface_subtype);
    lua_setfield(L, -2, "interface_subtype");

    opentlv_lua_codec_push_uint(L, v->interface_number);
    lua_setfield(L, -2, "interface_number");

    push_value(L, &v->oid);
    lua_setfield(L, -2, "oid");
}
static void get_lldp_management(lua_State* L, int index, void* value) {
    tlv_lldp_management_address_t* v = value;
    luaL_checktype(L, index, LUA_TTABLE);
    luaL_checkstack(L, 9, "codec fields");
    lua_getfield(L, index, "address_subtype");
    v->address_subtype = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "address");
    get_value(L, lua_gettop(L), &v->address);
    lua_getfield(L, index, "interface_subtype");
    v->interface_subtype = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "interface_number");
    v->interface_number = (uint32_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT32_MAX);
    lua_getfield(L, index, "oid");
    get_value(L, lua_gettop(L), &v->oid);
}
static const opentlv_lua_codec_rep_t rep_lldp_management = {
    sizeof(tlv_lldp_management_address_t), push_lldp_management, get_lldp_management};
static void push_lldp_organisation(lua_State* L, const void* value) {
    const tlv_lldp_organisation_t* v = value;
    lua_newtable(L);

    push_bytes(L, v->oui, 3);
    lua_setfield(L, -2, "oui");

    opentlv_lua_codec_push_uint(L, v->subtype);
    lua_setfield(L, -2, "subtype");

    push_value(L, &v->payload);
    lua_setfield(L, -2, "payload");
}
static void get_lldp_organisation(lua_State* L, int index, void* value) {
    tlv_lldp_organisation_t* v = value;
    luaL_checktype(L, index, LUA_TTABLE);
    luaL_checkstack(L, 7, "codec fields");
    lua_getfield(L, index, "oui");
    get_bytes(L, lua_gettop(L), v->oui, 3);
    lua_getfield(L, index, "subtype");
    v->subtype = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "payload");
    get_value(L, lua_gettop(L), &v->payload);
}
static const opentlv_lua_codec_rep_t rep_lldp_organisation = {
    sizeof(tlv_lldp_organisation_t), push_lldp_organisation, get_lldp_organisation};
#endif
#if OPENTLV_BLUETOOTH
static void push_i8(lua_State* L, const void* value) {
    const int8_t* v = value;
    opentlv_lua_codec_push_int(L, *v);
}
static void get_i8(lua_State* L, int index, void* value) {
    int8_t* v = value;
    *v = (int8_t)opentlv_lua_codec_int(L, index, INT8_MIN, INT8_MAX);
}
static const opentlv_lua_codec_rep_t rep_i8 = {sizeof(int8_t), push_i8, get_i8};
static void                          push_uuid128(lua_State* L, const void* value) {
    const tlv_bluetooth_uuid128_t* v = value;
    push_bytes(L, v->bytes, 16);
}
static void get_uuid128(lua_State* L, int index, void* value) {
    tlv_bluetooth_uuid128_t* v = value;
    get_bytes(L, index, v->bytes, 16);
}
static const opentlv_lua_codec_rep_t rep_uuid128 = {sizeof(tlv_bluetooth_uuid128_t), push_uuid128,
                                                    get_uuid128};
static void                          push_service16(lua_State* L, const void* value) {
    const tlv_bluetooth_service_data16_t* v = value;
    lua_newtable(L);

    opentlv_lua_codec_push_uint(L, v->uuid);
    lua_setfield(L, -2, "uuid");

    push_value(L, &v->payload);
    lua_setfield(L, -2, "payload");

    push_value(L, &v->raw);
    lua_setfield(L, -2, "raw");
}
static void get_service16(lua_State* L, int index, void* value) {
    tlv_bluetooth_service_data16_t* v = value;
    luaL_checktype(L, index, LUA_TTABLE);
    luaL_checkstack(L, 7, "codec fields");
    lua_getfield(L, index, "uuid");
    v->uuid = (uint16_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT16_MAX);
    lua_getfield(L, index, "payload");
    get_value(L, lua_gettop(L), &v->payload);
    lua_getfield(L, index, "raw");
    /* Informational raw bytes are ignored on encode, as in C. */
}
static const opentlv_lua_codec_rep_t rep_service16 = {sizeof(tlv_bluetooth_service_data16_t),
                                                      push_service16, get_service16};
static void                          push_service32(lua_State* L, const void* value) {
    const tlv_bluetooth_service_data32_t* v = value;
    lua_newtable(L);

    opentlv_lua_codec_push_uint(L, v->uuid);
    lua_setfield(L, -2, "uuid");

    push_value(L, &v->payload);
    lua_setfield(L, -2, "payload");

    push_value(L, &v->raw);
    lua_setfield(L, -2, "raw");
}
static void get_service32(lua_State* L, int index, void* value) {
    tlv_bluetooth_service_data32_t* v = value;
    luaL_checktype(L, index, LUA_TTABLE);
    luaL_checkstack(L, 7, "codec fields");
    lua_getfield(L, index, "uuid");
    v->uuid = (uint32_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT32_MAX);
    lua_getfield(L, index, "payload");
    get_value(L, lua_gettop(L), &v->payload);
    lua_getfield(L, index, "raw");
    /* Informational raw bytes are ignored on encode, as in C. */
}
static const opentlv_lua_codec_rep_t rep_service32 = {sizeof(tlv_bluetooth_service_data32_t),
                                                      push_service32, get_service32};
static void                          push_service128(lua_State* L, const void* value) {
    const tlv_bluetooth_service_data128_t* v = value;
    lua_newtable(L);

    push_bytes(L, v->uuid.bytes, 16);
    lua_setfield(L, -2, "uuid");

    push_value(L, &v->payload);
    lua_setfield(L, -2, "payload");

    push_value(L, &v->raw);
    lua_setfield(L, -2, "raw");
}
static void get_service128(lua_State* L, int index, void* value) {
    tlv_bluetooth_service_data128_t* v = value;
    luaL_checktype(L, index, LUA_TTABLE);
    luaL_checkstack(L, 7, "codec fields");
    lua_getfield(L, index, "uuid");
    get_bytes(L, lua_gettop(L), v->uuid.bytes, 16);
    lua_getfield(L, index, "payload");
    get_value(L, lua_gettop(L), &v->payload);
    lua_getfield(L, index, "raw");
    /* Informational raw bytes are ignored on encode, as in C. */
}
static const opentlv_lua_codec_rep_t rep_service128 = {sizeof(tlv_bluetooth_service_data128_t),
                                                       push_service128, get_service128};
static void                          push_manufacturer(lua_State* L, const void* value) {
    const tlv_bluetooth_manufacturer_data_t* v = value;
    lua_newtable(L);

    opentlv_lua_codec_push_uint(L, v->company_id);
    lua_setfield(L, -2, "company_id");

    push_value(L, &v->payload);
    lua_setfield(L, -2, "payload");

    push_value(L, &v->raw);
    lua_setfield(L, -2, "raw");
}
static void get_manufacturer(lua_State* L, int index, void* value) {
    tlv_bluetooth_manufacturer_data_t* v = value;
    luaL_checktype(L, index, LUA_TTABLE);
    luaL_checkstack(L, 7, "codec fields");
    lua_getfield(L, index, "company_id");
    v->company_id = (uint16_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT16_MAX);
    lua_getfield(L, index, "payload");
    get_value(L, lua_gettop(L), &v->payload);
    lua_getfield(L, index, "raw");
    /* Informational raw bytes are ignored on encode, as in C. */
}
static const opentlv_lua_codec_rep_t rep_manufacturer = {sizeof(tlv_bluetooth_manufacturer_data_t),
                                                         push_manufacturer, get_manufacturer};
static void                          push_uuid_list(lua_State* L, const void* value) {
    const tlv_bluetooth_uuid_list_t* v = value;
    if (v->raw.size / v->uuid_size > INT_MAX) luaL_error(L, "array too large");
    lua_newtable(L);
    for (size_t i = 0; i < v->raw.size / v->uuid_size; ++i) {
        union {
            uint16_t                u16;
            uint32_t                u32;
            tlv_bluetooth_uuid128_t u128;
        } item;
        tlv_result_t code = tlv_bluetooth_uuid_list_at(v, i, &item, sizeof item);
        if (code != TLV_OK) opentlv_lua_codec_raise(L, code);
        if (v->uuid_size == 2)
            push_u16(L, &item.u16);
        else if (v->uuid_size == 4)
            push_u32(L, &item.u32);
        else
            push_uuid128(L, &item.u128);
        lua_rawseti(L, -2, (int)i + 1);
    }
}
static void get_uuid_list(lua_State* L, int index, void* value, size_t width,
                          const tlv_codec_t* codec, const opentlv_lua_codec_rep_t* rep) {
    tlv_bluetooth_uuid_list_t* v = value;
    size_t                     count = array_size(L, index, INT_MAX);
    if (count > SIZE_MAX / width) luaL_error(L, "array too large");
    uint8_t* bytes = lua_newuserdata(L, count ? count * width : 1);
    void*    item = lua_newuserdata(L, rep->size);
    for (size_t i = 0; i < count; ++i) {
        size_t written;
        lua_rawgeti(L, index, (int)i + 1);
        rep->get(L, lua_gettop(L), item);
        lua_pop(L, 1);
        tlv_result_t code =
            tlv_codec_encode(codec, item, rep->size, bytes + i * width, width, &written, NULL);
        if (code != TLV_OK) opentlv_lua_codec_raise(L, code);
    }
    v->raw.data = bytes;
    v->raw.size = count * width;
    v->uuid_size = width;
}
static void get_uuid16_list(lua_State* L, int index, void* value) {
    get_uuid_list(L, index, value, 2, &tlv_bluetooth_codec_uuid16, &rep_u16);
}
static const opentlv_lua_codec_rep_t rep_uuid16_list = {sizeof(tlv_bluetooth_uuid_list_t),
                                                        push_uuid_list, get_uuid16_list};
static void                          get_uuid32_list(lua_State* L, int index, void* value) {
    get_uuid_list(L, index, value, 4, &tlv_bluetooth_codec_uuid32, &rep_u32);
}
static const opentlv_lua_codec_rep_t rep_uuid32_list = {sizeof(tlv_bluetooth_uuid_list_t),
                                                        push_uuid_list, get_uuid32_list};
static void                          get_uuid128_list(lua_State* L, int index, void* value) {
    get_uuid_list(L, index, value, 16, &tlv_bluetooth_codec_uuid128, &rep_uuid128);
}
static const opentlv_lua_codec_rep_t rep_uuid128_list = {sizeof(tlv_bluetooth_uuid_list_t),
                                                         push_uuid_list, get_uuid128_list};
#endif
#if OPENTLV_EMV
static void push_emv_date(lua_State* L, const void* value) {
    const tlv_emv_date_t* v = value;
    lua_newtable(L);

    opentlv_lua_codec_push_uint(L, v->year);
    lua_setfield(L, -2, "year");

    opentlv_lua_codec_push_uint(L, v->month);
    lua_setfield(L, -2, "month");

    opentlv_lua_codec_push_uint(L, v->day);
    lua_setfield(L, -2, "day");
}
static void get_emv_date(lua_State* L, int index, void* value) {
    tlv_emv_date_t* v = value;
    luaL_checktype(L, index, LUA_TTABLE);
    luaL_checkstack(L, 7, "codec fields");
    lua_getfield(L, index, "year");
    v->year = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "month");
    v->month = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "day");
    v->day = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
}
static const opentlv_lua_codec_rep_t rep_emv_date = {sizeof(tlv_emv_date_t), push_emv_date,
                                                     get_emv_date};
static void                          push_emv_time(lua_State* L, const void* value) {
    const tlv_emv_time_t* v = value;
    lua_newtable(L);

    opentlv_lua_codec_push_uint(L, v->hour);
    lua_setfield(L, -2, "hour");

    opentlv_lua_codec_push_uint(L, v->minute);
    lua_setfield(L, -2, "minute");

    opentlv_lua_codec_push_uint(L, v->second);
    lua_setfield(L, -2, "second");
}
static void get_emv_time(lua_State* L, int index, void* value) {
    tlv_emv_time_t* v = value;
    luaL_checktype(L, index, LUA_TTABLE);
    luaL_checkstack(L, 7, "codec fields");
    lua_getfield(L, index, "hour");
    v->hour = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "minute");
    v->minute = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "second");
    v->second = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
}
static const opentlv_lua_codec_rep_t rep_emv_time = {sizeof(tlv_emv_time_t), push_emv_time,
                                                     get_emv_time};
static void                          push_emv_cryptogram(lua_State* L, const void* value) {
    const tlv_emv_cryptogram_info_t* v = value;
    lua_newtable(L);

    lua_pushinteger(L, v->type);
    lua_setfield(L, -2, "type");

    opentlv_lua_codec_push_uint(L, v->flags);
    lua_setfield(L, -2, "flags");
}
static void get_emv_cryptogram(lua_State* L, int index, void* value) {
    tlv_emv_cryptogram_info_t* v = value;
    luaL_checktype(L, index, LUA_TTABLE);
    luaL_checkstack(L, 6, "codec fields");
    lua_getfield(L, index, "type");
    v->type = (tlv_emv_cryptogram_type_t)opentlv_lua_codec_uint(L, lua_gettop(L), INT_MAX);
    lua_getfield(L, index, "flags");
    v->flags = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
}
static const opentlv_lua_codec_rep_t rep_emv_cryptogram = {sizeof(tlv_emv_cryptogram_info_t),
                                                           push_emv_cryptogram, get_emv_cryptogram};
static void                          push_emv_cvm(lua_State* L, const void* value) {
    const tlv_emv_cvm_result_t* v = value;
    lua_newtable(L);

    opentlv_lua_codec_push_uint(L, v->method);
    lua_setfield(L, -2, "method");

    opentlv_lua_codec_push_uint(L, v->condition);
    lua_setfield(L, -2, "condition");

    opentlv_lua_codec_push_uint(L, v->result);
    lua_setfield(L, -2, "result");
}
static void get_emv_cvm(lua_State* L, int index, void* value) {
    tlv_emv_cvm_result_t* v = value;
    luaL_checktype(L, index, LUA_TTABLE);
    luaL_checkstack(L, 7, "codec fields");
    lua_getfield(L, index, "method");
    v->method = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "condition");
    v->condition = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "result");
    v->result = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
}
static const opentlv_lua_codec_rep_t rep_emv_cvm = {sizeof(tlv_emv_cvm_result_t), push_emv_cvm,
                                                    get_emv_cvm};
static void                          push_emv_track2(lua_State* L, const void* value) {
    const tlv_emv_track2_t* v = value;
    lua_newtable(L);

    lua_pushstring(L, v->pan);
    lua_setfield(L, -2, "pan");

    opentlv_lua_codec_push_uint(L, v->expiration_year);
    lua_setfield(L, -2, "expiration_year");

    opentlv_lua_codec_push_uint(L, v->expiration_month);
    lua_setfield(L, -2, "expiration_month");

    opentlv_lua_codec_push_uint(L, v->service_code);
    lua_setfield(L, -2, "service_code");

    lua_pushstring(L, v->discretionary_data);
    lua_setfield(L, -2, "discretionary_data");
}
static void get_emv_track2(lua_State* L, int index, void* value) {
    tlv_emv_track2_t* v = value;
    luaL_checktype(L, index, LUA_TTABLE);
    luaL_checkstack(L, 9, "codec fields");
    lua_getfield(L, index, "pan");
    {
        size_t      size;
        const char* text = opentlv_lua_codec_string(L, lua_gettop(L), &size);
        if (size >= sizeof(v->pan) || memchr(text, 0, size)) luaL_error(L, "invalid pan string");
        memcpy(v->pan, text, size);
        v->pan[size] = 0;
    }
    lua_getfield(L, index, "expiration_year");
    v->expiration_year = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "expiration_month");
    v->expiration_month = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "service_code");
    v->service_code = (uint16_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT16_MAX);
    lua_getfield(L, index, "discretionary_data");
    {
        size_t      size;
        const char* text = opentlv_lua_codec_string(L, lua_gettop(L), &size);
        if (size >= sizeof(v->discretionary_data) || memchr(text, 0, size))
            luaL_error(L, "invalid discretionary_data string");
        memcpy(v->discretionary_data, text, size);
        v->discretionary_data[size] = 0;
    }
}
static const opentlv_lua_codec_rep_t rep_emv_track2 = {sizeof(tlv_emv_track2_t), push_emv_track2,
                                                       get_emv_track2};
static void                          push_emv_afl_entry(lua_State* L, const void* value) {
    const tlv_emv_afl_entry_t* v = value;
    lua_newtable(L);

    opentlv_lua_codec_push_uint(L, v->sfi);
    lua_setfield(L, -2, "sfi");

    opentlv_lua_codec_push_uint(L, v->first_record);
    lua_setfield(L, -2, "first_record");

    opentlv_lua_codec_push_uint(L, v->last_record);
    lua_setfield(L, -2, "last_record");

    opentlv_lua_codec_push_uint(L, v->offline_auth_record_count);
    lua_setfield(L, -2, "offline_auth_record_count");
}
static void get_emv_afl_entry(lua_State* L, int index, void* value) {
    tlv_emv_afl_entry_t* v = value;
    luaL_checktype(L, index, LUA_TTABLE);
    luaL_checkstack(L, 8, "codec fields");
    lua_getfield(L, index, "sfi");
    v->sfi = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "first_record");
    v->first_record = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "last_record");
    v->last_record = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
    lua_getfield(L, index, "offline_auth_record_count");
    v->offline_auth_record_count = (uint8_t)opentlv_lua_codec_uint(L, lua_gettop(L), UINT8_MAX);
}
static void push_emv_account(lua_State* L, const void* value) {
    const tlv_emv_account_type_t* v = value;
    opentlv_lua_codec_push_uint(L, *v);
}
static void get_emv_account(lua_State* L, int index, void* value) {
    tlv_emv_account_type_t* v = value;
    *v = (tlv_emv_account_type_t)opentlv_lua_codec_uint(L, index, INT_MAX);
}
static const opentlv_lua_codec_rep_t rep_emv_account = {sizeof(tlv_emv_account_type_t),
                                                        push_emv_account, get_emv_account};
static void                          push_emv_biometric(lua_State* L, const void* value) {
    const tlv_emv_biometric_type_t* v = value;
    opentlv_lua_codec_push_uint(L, *v);
}
static void get_emv_biometric(lua_State* L, int index, void* value) {
    tlv_emv_biometric_type_t* v = value;
    *v = (tlv_emv_biometric_type_t)opentlv_lua_codec_uint(L, index, INT_MAX);
}
static const opentlv_lua_codec_rep_t rep_emv_biometric = {sizeof(tlv_emv_biometric_type_t),
                                                          push_emv_biometric, get_emv_biometric};
static void                          push_emv_numbers(lua_State* L, const void* value) {
    const tlv_emv_number_list_t* v = value;
    lua_newtable(L);
    for (size_t i = 0; i < v->count; ++i) {
        opentlv_lua_codec_push_uint(L, v->values[i]);
        lua_rawseti(L, -2, (int)i + 1);
    }
}
static void get_emv_numbers(lua_State* L, int index, void* value) {
    tlv_emv_number_list_t* v = value;
    v->count = array_size(L, index, 4);
    for (size_t i = 0; i < v->count; ++i) {
        lua_rawgeti(L, index, (int)i + 1);
        v->values[i] = opentlv_lua_codec_uint(L, lua_gettop(L), UINT64_MAX);
        lua_pop(L, 1);
    }
}
static const opentlv_lua_codec_rep_t rep_emv_numbers = {sizeof(tlv_emv_number_list_t),
                                                        push_emv_numbers, get_emv_numbers};
static void                          push_emv_afl(lua_State* L, const void* value) {
    const tlv_emv_afl_t* v = value;
    lua_newtable(L);
    for (size_t i = 0; i < v->count; ++i) {
        push_emv_afl_entry(L, &v->entries[i]);
        lua_rawseti(L, -2, (int)i + 1);
    }
}
static void get_emv_afl(lua_State* L, int index, void* value) {
    tlv_emv_afl_t* v = value;
    v->count = array_size(L, index, TLV_EMV_AFL_MAX_ENTRIES);
    for (size_t i = 0; i < v->count; ++i) {
        int top = lua_gettop(L);
        lua_rawgeti(L, index, (int)i + 1);
        get_emv_afl_entry(L, lua_gettop(L), &v->entries[i]);
        lua_settop(L, top);
    }
}
static const opentlv_lua_codec_rep_t rep_emv_afl = {sizeof(tlv_emv_afl_t), push_emv_afl,
                                                    get_emv_afl};
static const opentlv_lua_codec_rep_t rep_digits = {0, NULL, NULL};
static int                           emv_find(lua_State* L) {
    size_t            size;
    const char*       bytes = opentlv_lua_codec_string(L, 1, &size);
    tlv_emv_context_t context =
        lua_isnoneornil(L, 2)
            ? TLV_EMV_CONTEXT_BASE
            : (tlv_emv_context_t)opentlv_lua_codec_uint(L, 2, TLV_EMV_CONTEXT_COUNT - 1);
    tlv_tag_t                   tag = {(const uint8_t*)bytes, size};
    const tlv_emv_definition_t* definition = tlv_emv_find(context, &tag);
    if (!definition || !definition->codec) {
        lua_pushnil(L);
        return 1;
    }
    const opentlv_lua_codec_rep_t* rep;
    switch (tlv_emv_builtin_value_kind(definition)) {
        case TLV_EMV_VALUE_NUMBER:
        case TLV_EMV_VALUE_FLAGS: rep = &rep_u64; break;
        case TLV_EMV_VALUE_DIGITS: rep = &rep_digits; break;
        case TLV_EMV_VALUE_DATE: rep = &rep_emv_date; break;
        case TLV_EMV_VALUE_TIME: rep = &rep_emv_time; break;
        case TLV_EMV_VALUE_ACCOUNT: rep = &rep_emv_account; break;
        case TLV_EMV_VALUE_CRYPTOGRAM: rep = &rep_emv_cryptogram; break;
        case TLV_EMV_VALUE_BIOMETRIC: rep = &rep_emv_biometric; break;
        case TLV_EMV_VALUE_NUMBER_LIST: rep = &rep_emv_numbers; break;
        case TLV_EMV_VALUE_AFL: rep = &rep_emv_afl; break;
        case TLV_EMV_VALUE_CVM_RESULT: rep = &rep_emv_cvm; break;
        case TLV_EMV_VALUE_TRACK2: rep = &rep_emv_track2; break;
        default: return opentlv_lua_codec_raise(L, TLV_ERR_UNSUPPORTED);
    }
    opentlv_lua_codec_push(L, definition->codec, rep);
    return 1;
}
#endif
void opentlv_lua_codec_builtins(lua_State* L) {
    opentlv_lua_codec_add(L, "uint8", &tlv_codec_uint8, &rep_u8);
    opentlv_lua_codec_add(L, "uint16_be", &tlv_codec_uint16_be, &rep_u16);
    opentlv_lua_codec_add(L, "uint16_le", &tlv_codec_uint16_le, &rep_u16);
    opentlv_lua_codec_add(L, "uint32_be", &tlv_codec_uint32_be, &rep_u32);
    opentlv_lua_codec_add(L, "uint32_le", &tlv_codec_uint32_le, &rep_u32);
    opentlv_lua_codec_add(L, "int64_minimal_be", &tlv_codec_int64_minimal_be, &rep_i64);
    opentlv_lua_codec_add(L, "bytes", &tlv_codec_bytes, &rep_value);
    opentlv_lua_codec_add(L, "ipv4", &tlv_codec_ipv4, &rep_ipv4);
    opentlv_lua_codec_add(L, "ipv4_list", &tlv_codec_ipv4_list, &rep_ipv4_list);
    lua_newtable(L);
    opentlv_lua_codec_add(L, "message_type", &tlv_dhcpv4_codec_message_type, &rep_u8);
    lua_setfield(L, -2, "dhcpv4");
    lua_newtable(L);
    opentlv_lua_codec_add(L, "ttl", &tlv_lldp_codec_ttl, &rep_u16);
    opentlv_lua_codec_add(L, "text", &tlv_lldp_codec_text, &rep_value);
#if OPENTLV_LLDP
    opentlv_lua_codec_add(L, "chassis_id", &tlv_lldp_codec_chassis_id, &rep_lldp_id);
    opentlv_lua_codec_add(L, "port_id", &tlv_lldp_codec_port_id, &rep_lldp_id);
    opentlv_lua_codec_add(L, "capabilities", &tlv_lldp_codec_capabilities, &rep_lldp_capabilities);
    opentlv_lua_codec_add(L, "management_address", &tlv_lldp_codec_management_address,
                          &rep_lldp_management);
    opentlv_lua_codec_add(L, "organisation", &tlv_lldp_codec_organisation, &rep_lldp_organisation);
#endif
    lua_setfield(L, -2, "lldp");
    lua_newtable(L);
    opentlv_lua_codec_add(L, "uuid16", &tlv_bluetooth_codec_uuid16, &rep_u16);
    opentlv_lua_codec_add(L, "uuid32", &tlv_bluetooth_codec_uuid32, &rep_u32);
#if OPENTLV_BLUETOOTH
    opentlv_lua_codec_add(L, "flags", &tlv_bluetooth_ad_codec_flags, &rep_value);
    opentlv_lua_codec_add(L, "local_name", &tlv_bluetooth_ad_codec_local_name, &rep_value);
    opentlv_lua_codec_add(L, "tx_power", &tlv_bluetooth_ad_codec_tx_power, &rep_i8);
    opentlv_lua_codec_add(L, "uuid128", &tlv_bluetooth_codec_uuid128, &rep_uuid128);
    opentlv_lua_codec_add(L, "uuid16_list", &tlv_bluetooth_codec_uuid16_list, &rep_uuid16_list);
    opentlv_lua_codec_add(L, "uuid32_list", &tlv_bluetooth_codec_uuid32_list, &rep_uuid32_list);
    opentlv_lua_codec_add(L, "uuid128_list", &tlv_bluetooth_codec_uuid128_list, &rep_uuid128_list);
    opentlv_lua_codec_add(L, "service_data16", &tlv_bluetooth_codec_service_data16, &rep_service16);
    opentlv_lua_codec_add(L, "service_data32", &tlv_bluetooth_codec_service_data32, &rep_service32);
    opentlv_lua_codec_add(L, "service_data128", &tlv_bluetooth_codec_service_data128,
                          &rep_service128);
    opentlv_lua_codec_add(L, "manufacturer_data", &tlv_bluetooth_codec_manufacturer_data,
                          &rep_manufacturer);
#endif
    lua_setfield(L, -2, "bluetooth");
#if OPENTLV_FORMAT_BER
    lua_newtable(L);
    opentlv_lua_codec_add(L, "boolean", &tlv_asn1_codec_boolean, &rep_boolean);
    opentlv_lua_codec_add(L, "integer", &tlv_asn1_codec_integer, &rep_i64);
    opentlv_lua_codec_add(L, "enumerated", &tlv_asn1_codec_enumerated, &rep_i64);
    opentlv_lua_codec_add(L, "bit_string", &tlv_asn1_codec_bit_string, &rep_bit_string);
    opentlv_lua_codec_add(L, "octet_string", &tlv_asn1_codec_octet_string, &rep_octets);
    opentlv_lua_codec_add(L, "null", &tlv_asn1_codec_null, &rep_null);
    opentlv_lua_codec_add(L, "oid", &tlv_asn1_codec_oid, &rep_oid);
    opentlv_lua_codec_add(L, "relative_oid", &tlv_asn1_codec_relative_oid, &rep_oid);
    opentlv_lua_codec_add(L, "bmp_string", &tlv_asn1_codec_bmp_string, &rep_bmp);
    opentlv_lua_codec_add(L, "universal_string", &tlv_asn1_codec_universal_string, &rep_universal);
    opentlv_lua_codec_add(L, "utc_time", &tlv_asn1_codec_utc_time, &rep_utc_time);
    opentlv_lua_codec_add(L, "generalized_time", &tlv_asn1_codec_generalized_time,
                          &rep_generalized_time);
    opentlv_lua_codec_add(L, "date", &tlv_asn1_codec_date, &rep_date);
    opentlv_lua_codec_add(L, "time_of_day", &tlv_asn1_codec_time_of_day, &rep_time_of_day);
    opentlv_lua_codec_add(L, "date_time", &tlv_asn1_codec_date_time, &rep_date_time);
    opentlv_lua_codec_add(L, "oid_iri", &tlv_asn1_codec_oid_iri, &rep_iri);
    opentlv_lua_codec_add(L, "relative_oid_iri", &tlv_asn1_codec_relative_oid_iri, &rep_iri);
    opentlv_lua_codec_add(L, "utf8_string", &tlv_asn1_codec_utf8_string, &rep_string);
    opentlv_lua_codec_add(L, "numeric_string", &tlv_asn1_codec_numeric_string, &rep_string);
    opentlv_lua_codec_add(L, "printable_string", &tlv_asn1_codec_printable_string, &rep_string);
    opentlv_lua_codec_add(L, "ia5_string", &tlv_asn1_codec_ia5_string, &rep_string);
    opentlv_lua_codec_add(L, "visible_string", &tlv_asn1_codec_visible_string, &rep_string);
    opentlv_lua_codec_add(L, "time", &tlv_asn1_codec_time, &rep_string);
    opentlv_lua_codec_add(L, "duration", &tlv_asn1_codec_duration, &rep_string);
    opentlv_lua_codec_add(L, "object_descriptor", &tlv_asn1_codec_object_descriptor, &rep_octets);
    opentlv_lua_codec_add(L, "teletex_string", &tlv_asn1_codec_teletex_string, &rep_octets);
    opentlv_lua_codec_add(L, "videotex_string", &tlv_asn1_codec_videotex_string, &rep_octets);
    opentlv_lua_codec_add(L, "graphic_string", &tlv_asn1_codec_graphic_string, &rep_octets);
    opentlv_lua_codec_add(L, "general_string", &tlv_asn1_codec_general_string, &rep_octets);
    lua_setfield(L, -2, "asn1");
#endif
#if OPENTLV_EMV
    lua_newtable(L);
    opentlv_lua_codec_add(L, "amount", &tlv_emv_codec_amount, &rep_u64);
    lua_pushcfunction(L, emv_find);
    lua_setfield(L, -2, "find");
    lua_newtable(L);
    lua_pushinteger(L, TLV_EMV_CONTEXT_BASE);
    lua_setfield(L, -2, "BASE");
    lua_pushinteger(L, TLV_EMV_CONTEXT_BIT);
    lua_setfield(L, -2, "BIT");
    lua_pushinteger(L, TLV_EMV_CONTEXT_BHT);
    lua_setfield(L, -2, "BHT");
    lua_pushinteger(L, TLV_EMV_CONTEXT_BHT_FORMAT);
    lua_setfield(L, -2, "BHT_FORMAT");
    lua_pushinteger(L, TLV_EMV_CONTEXT_BIT_GROUP);
    lua_setfield(L, -2, "BIT_GROUP");
    lua_pushinteger(L, TLV_EMV_CONTEXT_BIOMETRIC_COUNTERS);
    lua_setfield(L, -2, "BIOMETRIC_COUNTERS");
    lua_pushinteger(L, TLV_EMV_CONTEXT_BIOMETRIC_ATTEMPTS);
    lua_setfield(L, -2, "BIOMETRIC_ATTEMPTS");
    lua_pushinteger(L, TLV_EMV_CONTEXT_BIOMETRIC_VERIFICATION);
    lua_setfield(L, -2, "BIOMETRIC_VERIFICATION");
    lua_setfield(L, -2, "contexts");
    lua_setfield(L, -2, "emv");
#endif
}
