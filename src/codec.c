// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "codec.h"
#include "common.h"
#include "error.h"

#include <tlv/codec/digits.h>
#include <tlv/codec/number.h>
#include <tlv/codec/text.h>
#include <tlv/value.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define CODEC_MT "opentlv.Codec"

typedef struct lua_codec {
    tlv_codec_t                    codec;
    const opentlv_lua_codec_rep_t* rep;
    union {
        tlv_number_codec_config_t number;
        tlv_text_codec_config_t   text;
        tlv_digits_codec_config_t digits;
    } config;
    int custom;
} lua_codec_t;

int opentlv_lua_codec_raise(lua_State* L, tlv_result_t code) {
    return opentlv_lua_raise(L, code, 0, 0);
}

static int raise_codec_diagnostic(lua_State* L, const tlv_codec_diagnostic_t* d) {
    opentlv_lua_push_diagnostic(L, &d->diagnostic);
    opentlv_lua_push_codec_detail(L, &d->codec);
    lua_setfield(L, -2, "codec_detail");
    return lua_error(L);
}

const char* opentlv_lua_codec_string(lua_State* L, int index, size_t* size) {
    luaL_checktype(L, index, LUA_TSTRING);
    return lua_tolstring(L, index, size);
}

/* Decimal strings are an ergonomic C-integer representation, never a wire
 * decoder. Reject overflow before arithmetic and never round a Lua number. */
static uint64_t magnitude(lua_State* L, int index, uint64_t limit, int* negative) {
    uint64_t value = 0;
    *negative = 0;
#if LUA_VERSION_NUM >= 503
    if (lua_isinteger(L, index)) {
        lua_Integer n = lua_tointeger(L, index);
        *negative = n < 0;
        value = n < 0 ? (uint64_t)(-(n + 1)) + 1 : (uint64_t)n;
    } else
#endif
        if (lua_type(L, index) == LUA_TNUMBER) {
        lua_Number n = lua_tonumber(L, index);
        /* The supported default Lua number is double; also respect float builds. */
        lua_Number exact = sizeof(lua_Number) == sizeof(float) ? (lua_Number)16777215
                                                               : (lua_Number)9007199254740991.0;
        if (!(n >= -exact && n <= exact)) {
            luaL_argerror(L, index, "use a decimal string for an exact 64-bit integer");
        }
        *negative = n < 0;
        if (*negative) n = -n;
        value = (uint64_t)n;
        if ((lua_Number)value != n) luaL_argerror(L, index, "integer required");
    } else {
        size_t      size;
        const char* text = opentlv_lua_codec_string(L, index, &size);
        size_t      i = 0;
        if (size && text[0] == '-') {
            *negative = 1;
            i = 1;
        }
        if (i == size) luaL_argerror(L, index, "decimal integer required");
        for (; i < size; ++i) {
            unsigned digit = (unsigned)(unsigned char)text[i] - '0';
            if (digit > 9 || value > limit / 10 || (value == limit / 10 && digit > limit % 10)) {
                luaL_argerror(L, index, "integer out of range or invalid decimal string");
            }
            value = value * 10 + digit;
        }
    }
    if (value > limit) luaL_argerror(L, index, "integer out of range");
    return value;
}

uint64_t opentlv_lua_codec_uint(lua_State* L, int index, uint64_t maximum) {
    int      negative;
    uint64_t value = magnitude(L, index, maximum, &negative);
    if (negative) luaL_argerror(L, index, "unsigned integer required");
    return value;
}

int64_t opentlv_lua_codec_int(lua_State* L, int index, int64_t minimum, int64_t maximum) {
    int      negative;
    uint64_t value = magnitude(L, index, UINT64_C(9223372036854775808), &negative);
    int64_t  result;
    if (negative)
        result = value ? -(int64_t)(value - 1) - 1 : 0;
    else {
        if (value > INT64_MAX) luaL_argerror(L, index, "signed integer out of range");
        result = (int64_t)value;
    }
    if (result < minimum || result > maximum) luaL_argerror(L, index, "integer out of range");
    return result;
}

void opentlv_lua_codec_push_uint(lua_State* L, uint64_t value) {
#if LUA_VERSION_NUM >= 503
    if (value <= (uint64_t)LUA_MAXINTEGER) {
        lua_pushinteger(L, (lua_Integer)value);
        return;
    }
#else
    uint64_t exact =
        sizeof(lua_Number) == sizeof(float) ? UINT64_C(16777215) : UINT64_C(9007199254740991);
    if (value <= exact) {
        lua_pushnumber(L, (lua_Number)value);
        return;
    }
#endif
    char text[32];
    snprintf(text, sizeof text, "%" PRIu64, value);
    lua_pushstring(L, text);
}

void opentlv_lua_codec_push_int(lua_State* L, int64_t value) {
#if LUA_VERSION_NUM >= 503
    if (value >= LUA_MININTEGER && value <= LUA_MAXINTEGER) {
        lua_pushinteger(L, (lua_Integer)value);
        return;
    }
#else
    int64_t exact =
        sizeof(lua_Number) == sizeof(float) ? INT64_C(16777215) : INT64_C(9007199254740991);
    if (value >= -exact && value <= exact) {
        lua_pushnumber(L, (lua_Number)value);
        return;
    }
#endif
    char text[32];
    snprintf(text, sizeof text, "%" PRId64, value);
    lua_pushstring(L, text);
}

static void push_number(lua_State* L, const void* value) {
    opentlv_lua_codec_push_uint(L, *(const uint64_t*)value);
}
static void get_number(lua_State* L, int index, void* value) {
    *(uint64_t*)value = opentlv_lua_codec_uint(L, index, UINT64_MAX);
}
static void push_text(lua_State* L, const void* value) {
    const tlv_value_t* text = value;
    lua_pushlstring(L, text->data ? (const char*)text->data : "", (size_t)text->size);
}
static void get_text(lua_State* L, int index, void* value) {
    tlv_value_t* text = value;
    size_t       size;
    text->data = (const uint8_t*)opentlv_lua_codec_string(L, index, &size);
    text->size = size;
}
static const opentlv_lua_codec_rep_t number_rep = {sizeof(uint64_t), push_number, get_number};
static const opentlv_lua_codec_rep_t text_rep = {sizeof(tlv_value_t), push_text, get_text};
/* Variable-sized, NUL-terminated native representation handled by decode/encode. */
static const opentlv_lua_codec_rep_t digits_rep = {0, NULL, NULL};

static lua_codec_t* new_codec(lua_State* L) {
    lua_codec_t* codec = lua_newuserdata(L, sizeof(*codec));
    memset(codec, 0, sizeof(*codec));
    luaL_getmetatable(L, CODEC_MT);
    lua_setmetatable(L, -2);
    return codec;
}

void opentlv_lua_codec_push(lua_State* L, const tlv_codec_t* native,
                            const opentlv_lua_codec_rep_t* rep) {
    lua_codec_t* codec = new_codec(L);
    codec->codec = *native;
    codec->rep = rep;
}

void opentlv_lua_codec_add(lua_State* L, const char* name, const tlv_codec_t* native,
                           const opentlv_lua_codec_rep_t* rep) {
    opentlv_lua_codec_push(L, native, rep);
    lua_setfield(L, -2, name);
}

/* Every custom invocation has its own frame, so callbacks may recursively use
 * the same codec or invoke it from another coroutine. The prepared protected
 * closure is rooted before entering C, including on Lua 5.1 (where creating a
 * C closure may allocate). No Lua allocation escapes through a core frame. */
typedef struct custom_call {
    lua_State*     L;
    int            closure;
    int            result;
    int            failed;
    const uint8_t* data;
    size_t         size;
    int            input;
} custom_call_t;

static int invoke_custom(lua_State* L) {
    custom_call_t* call = lua_touserdata(L, lua_upvalueindex(1));
    lua_pushvalue(L, lua_upvalueindex(2));
    if (call->input)
        lua_pushvalue(L, lua_upvalueindex(3));
    else
        lua_pushlstring(L, call->data ? (const char*)call->data : "", call->size);
    lua_call(L, 1, 1);
    if (call->input) luaL_checktype(L, -1, LUA_TSTRING);
    return 1;
}

static tlv_result_t custom_run(custom_call_t* call) {
    lua_pushvalue(call->L, call->closure);
    call->failed = lua_pcall(call->L, 0, 1, 0) != LUA_OK;
    call->result = lua_gettop(call->L);
    return call->failed ? TLV_ERR_INVALID_VALUE : TLV_OK;
}

static tlv_result_t custom_decode(const void* context, const uint8_t* data, size_t size,
                                  void* value, size_t capacity,
                                  tlv_codec_diagnostic_t* diagnostic) {
    tlv_codec_diagnostic_init(diagnostic, TLV_CODEC_OP_DECODE);
    custom_call_t* call = (custom_call_t*)context;
    (void)value;
    (void)capacity;
    call->data = data;
    call->size = size;
    return tlv_codec_diagnostic_result(diagnostic, custom_run(call));
}

static tlv_result_t custom_encode(const void* context, const void* value, size_t size,
                                  uint8_t* data, size_t capacity, size_t* written,
                                  tlv_codec_diagnostic_t* diagnostic) {
    tlv_codec_diagnostic_init(diagnostic, TLV_CODEC_OP_ENCODE);
    custom_call_t* call = (custom_call_t*)context;
    (void)value;
    (void)size;
    if (!call->result) {
        tlv_result_t code = custom_run(call);
        if (code != TLV_OK) return tlv_codec_diagnostic_result(diagnostic, code);
    }
    size_t      length;
    const char* bytes = lua_tolstring(call->L, call->result, &length);
    if (data && capacity < length)
        return tlv_codec_diagnostic_result(diagnostic, TLV_ERR_BUFFER_TOO_SHORT);
    if (data && length) memcpy(data, bytes, length);
    *written = length;
    return tlv_codec_diagnostic_result(diagnostic, TLV_OK);
}

static tlv_codec_t prepare_custom(lua_State* L, lua_codec_t* codec, custom_call_t* call,
                                  int encode) {
    tlv_codec_t native = codec->codec;
    memset(call, 0, sizeof(*call));
    call->L = L;
    call->input = encode;
    luaL_checkstack(L, 8, "codec callback stack");
#if LUA_VERSION_NUM == 501
    lua_getfenv(L, 1);
#else
    lua_getuservalue(L, 1);
#endif
    lua_pushlightuserdata(L, call);
    lua_getfield(L, -2, encode ? "encode" : "decode");
    lua_pushvalue(L, 2);
    lua_pushcclosure(L, invoke_custom, 3);
    call->closure = lua_gettop(L);
    native.context = call;
    return native;
}

static int codec_decode(lua_State* L) {
    tlv_codec_diagnostic_t diagnostic;
    lua_codec_t*           codec = luaL_checkudata(L, 1, CODEC_MT);
    size_t                 size;
    const uint8_t*         data = (const uint8_t*)opentlv_lua_codec_string(L, 2, &size);
    tlv_result_t           code;
    if (codec->custom) {
        custom_call_t call;
        tlv_codec_t   native = prepare_custom(L, codec, &call, 0);
        unsigned char dummy;
        code = tlv_codec_decode(&native, data, size, &dummy, sizeof dummy, &diagnostic);
        if (call.failed) return lua_error(L);
        if (code != TLV_OK) return raise_codec_diagnostic(L, &diagnostic);
        return 1;
    }
    size_t capacity = codec->rep->size;
    if (!capacity) {
        if (size > (SIZE_MAX - 1) / 2) return luaL_error(L, "digit buffer too large");
        capacity = size * 2 + 1;
    }
    void* value = lua_newuserdata(L, capacity);
    code = tlv_codec_decode(&codec->codec, data, size, value, capacity, &diagnostic);
    if (code != TLV_OK) return raise_codec_diagnostic(L, &diagnostic);
    if (codec->rep->push)
        codec->rep->push(L, value);
    else
        lua_pushstring(L, value);
    return 1;
}

static int codec_encode(lua_State* L) {
    tlv_codec_diagnostic_t diagnostic;
    lua_codec_t*           codec = luaL_checkudata(L, 1, CODEC_MT);
    luaL_checkany(L, 2);
    custom_call_t call;
    tlv_codec_t   native = codec->codec;
    void*         value;
    size_t        size;
    if (codec->custom) {
        native = prepare_custom(L, codec, &call, 1);
        value = &call;
        size = sizeof call;
    } else if (!codec->rep->size) {
        value = (void*)opentlv_lua_codec_string(L, 2, &size);
    } else {
        size = codec->rep->size;
        value = lua_newuserdata(L, size);
        memset(value, 0, size);
        codec->rep->get(L, 2, value);
    }
    size_t       required = 0, written = 0;
    tlv_result_t code = tlv_codec_encode(&native, value, size, NULL, 0, &required, &diagnostic);
    if (codec->custom && call.failed) return lua_error(L);
    if (code != TLV_OK) return raise_codec_diagnostic(L, &diagnostic);
    uint8_t* bytes = lua_newuserdata(L, required ? required : 1);
    code = tlv_codec_encode(&native, value, size, bytes, required, &written, &diagnostic);
    if (codec->custom && call.failed) return lua_error(L);
    if (code != TLV_OK) return raise_codec_diagnostic(L, &diagnostic);
    lua_pushlstring(L, (const char*)bytes, written);
    return 1;
}

static size_t option_size(lua_State* L, const char* name) {
    lua_getfield(L, 1, name);
    size_t value =
        lua_isnil(L, -1) ? 0 : (size_t)opentlv_lua_codec_uint(L, lua_gettop(L), SIZE_MAX);
    lua_pop(L, 1);
    return value;
}

static int number_codec(lua_State* L) {
    static const char* const encodings[] = {"binary_be", "binary_le", "bcd", NULL};
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_codec_t* codec = new_codec(L);
    lua_getfield(L, 1, "encoding");
    codec->config.number.encoding =
        (tlv_number_encoding_t)luaL_checkoption(L, -1, "binary_be", encodings);
    lua_pop(L, 1);
    codec->config.number.width = option_size(L, "width");
    size_t digits = option_size(L, "digits");
    if (digits > 18) return luaL_error(L, "digits must be at most 18");
    codec->config.number.digits = (unsigned)digits;
    codec->codec = tlv_number_codec(&codec->config.number);
    codec->rep = &number_rep;
    return 1;
}

static int text_codec(lua_State* L) {
    static const char* const alphabets[] = {"ascii_printable", "ascii_alnum", NULL};
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_codec_t* codec = new_codec(L);
    lua_getfield(L, 1, "alphabet");
    codec->config.text.alphabet =
        (tlv_text_alphabet_t)luaL_checkoption(L, -1, "ascii_printable", alphabets);
    lua_pop(L, 1);
    codec->config.text.width = option_size(L, "width");
    lua_getfield(L, 1, "zero_padding");
    if (!lua_isnil(L, -1)) luaL_checktype(L, -1, LUA_TBOOLEAN);
    codec->config.text.zero_padding = lua_toboolean(L, -1);
    lua_pop(L, 1);
    codec->codec = tlv_text_codec(&codec->config.text);
    codec->rep = &text_rep;
    return 1;
}

static int digits_codec(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_codec_t* codec = new_codec(L);
    codec->config.digits.width = option_size(L, "width");
    codec->codec = tlv_digits_codec(&codec->config.digits);
    codec->rep = &digits_rep;
    return 1;
}

static int custom_codec(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_codec_t* codec = new_codec(L);
    codec->custom = 1;
    /* A GC-visible userdata environment, rather than registry roots, permits
     * collection even when a callback closes over its own codec. Snapshot the
     * functions so mutation of the caller's table cannot change the descriptor. */
    lua_newtable(L);
    const char* names[] = {"decode", "encode"};
    for (int i = 0; i < 2; ++i) {
        lua_getfield(L, 1, names[i]);
        if (!lua_isnil(L, -1)) {
            luaL_checktype(L, -1, LUA_TFUNCTION);
            if (i)
                codec->codec.encode = custom_encode;
            else
                codec->codec.decode = custom_decode;
        }
        lua_setfield(L, -2, names[i]);
    }
#if LUA_VERSION_NUM == 501
    lua_setfenv(L, -2);
#else
    lua_setuservalue(L, -2);
#endif
    return 1;
}

void opentlv_lua_open_codec(lua_State* L, int module_index) {
    static const opentlv_lua_method_t methods[] = {
        {"decode", codec_decode}, {"encode", codec_encode}, {NULL, NULL}};
    opentlv_lua_new_type(L, CODEC_MT, methods, NULL, NULL, NULL);
    lua_newtable(L);
    opentlv_lua_codec_builtins(L);
    lua_pushcfunction(L, number_codec);
    lua_setfield(L, -2, "number");
    lua_pushcfunction(L, text_codec);
    lua_setfield(L, -2, "text");
    lua_pushcfunction(L, digits_codec);
    lua_setfield(L, -2, "digits");
    lua_setfield(L, module_index, "codecs");
    lua_pushcfunction(L, custom_codec);
    lua_setfield(L, module_index, "codec");
}
