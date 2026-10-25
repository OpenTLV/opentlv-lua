// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "codec.h"
#include "common.h"
#include "error.h"
#include "format.h"
#include "schema.h"

#include <tlv/codec/digits.h>
#include <tlv/codec/number.h>
#include <tlv/codec/structure.h>
#include <tlv/codec/text.h>
#include <tlv/schema/number.h>
#include <tlv/value.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define CODEC_MT "opentlv.Codec"
#define STRUCTURE_CODEC_MT "opentlv.StructureCodec"

typedef struct lua_codec {
    tlv_codec_t                    codec;
    const opentlv_lua_codec_rep_t* rep;
    union {
        tlv_number_codec_config_t number;
        tlv_text_codec_config_t   text;
        tlv_digits_codec_config_t digits;
        struct {
            tlv_schema_number_t number;
            tlv_schema_entry_t  entry;
        } schema_number;
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

int opentlv_lua_codec_query(lua_State* L, int index, tlv_query_codec_adapter_t* adapter,
                            size_t* scratch_size) {
    lua_codec_t* codec = luaL_checkudata(L, index, CODEC_MT);
    if (codec->custom) return 0;
    if (codec->rep == &number_rep)
        adapter->representation = TLV_QUERY_CODEC_UNSIGNED;
    else if (codec->rep == &text_rep)
        adapter->representation = TLV_QUERY_CODEC_UTF8;
    else if (!opentlv_lua_codec_builtin_query(codec->rep, &adapter->representation))
        return 0;
    adapter->codec = &codec->codec;
    *scratch_size = codec->rep->size;
    return 1;
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

/* Value and structure callbacks share the protected input and output steps. */
static tlv_result_t custom_input(const void* context, const uint8_t* data, size_t size) {
    custom_call_t* call = (custom_call_t*)context;
    call->data = data;
    call->size = size;
    return custom_run(call);
}

/* Calls the Lua encoder once; the size query and the write reuse its string. */
static tlv_result_t custom_output(const void* context, uint8_t* data, size_t capacity,
                                  size_t* written) {
    custom_call_t* call = (custom_call_t*)context;
    if (!call->result) {
        tlv_result_t code = custom_run(call);
        if (code != TLV_OK) return code;
    }
    size_t      length;
    const char* bytes = lua_tolstring(call->L, call->result, &length);
    if (data && capacity < length) return TLV_ERR_BUFFER_TOO_SHORT;
    if (data && length) memcpy(data, bytes, length);
    *written = length;
    return TLV_OK;
}

static tlv_result_t custom_decode(const void* context, const uint8_t* data, size_t size,
                                  void* value, size_t capacity,
                                  tlv_codec_diagnostic_t* diagnostic) {
    tlv_codec_diagnostic_init(diagnostic, TLV_CODEC_OP_DECODE);
    (void)value;
    (void)capacity;
    return tlv_codec_diagnostic_result(diagnostic, custom_input(context, data, size));
}

static tlv_result_t custom_encode(const void* context, const void* value, size_t size,
                                  uint8_t* data, size_t capacity, size_t* written,
                                  tlv_codec_diagnostic_t* diagnostic) {
    tlv_codec_diagnostic_init(diagnostic, TLV_CODEC_OP_ENCODE);
    (void)value;
    (void)size;
    return tlv_codec_diagnostic_result(diagnostic, custom_output(context, data, capacity, written));
}

/* Core initializes the diagnostic and validates the sequence around these. */
static tlv_result_t structure_decode(const void* context, const tlv_format_t* format,
                                     const uint8_t* data, size_t size, void* value, size_t capacity,
                                     tlv_codec_diagnostic_t* diagnostic) {
    (void)format;
    (void)value;
    (void)capacity;
    return tlv_codec_diagnostic_result(diagnostic, custom_input(context, data, size));
}

static tlv_result_t structure_encode(const void* context, const tlv_format_t* format,
                                     const void* value, size_t size, uint8_t* data, size_t capacity,
                                     size_t* written, tlv_codec_diagnostic_t* diagnostic) {
    (void)format;
    (void)value;
    (void)size;
    return tlv_codec_diagnostic_result(diagnostic, custom_output(context, data, capacity, written));
}

/* Roots the protected closure that applies the `encode` or `decode` callback
 * from the environment of the userdata at index 1 to argument 2. */
static void prepare_call(lua_State* L, custom_call_t* call, int encode) {
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
}

static tlv_codec_t prepare_custom(lua_State* L, lua_codec_t* codec, custom_call_t* call,
                                  int encode) {
    tlv_codec_t native = codec->codec;
    prepare_call(L, call, encode);
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

static void set_environment(lua_State* L, int index) {
#if LUA_VERSION_NUM == 501
    lua_setfenv(L, index);
#else
    lua_setuservalue(L, index);
#endif
}

static void number_config(lua_State* L, tlv_number_codec_config_t* config) {
    static const char* const encodings[] = {"binary_be", "binary_le", "bcd", NULL};
    lua_getfield(L, 1, "encoding");
    config->encoding = (tlv_number_encoding_t)luaL_checkoption(L, -1, "binary_be", encodings);
    lua_pop(L, 1);
    config->width = option_size(L, "width");
    size_t digits = option_size(L, "digits");
    if (digits > 18) luaL_error(L, "digits must be at most 18");
    config->digits = (unsigned)digits;
}

static int number_codec(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_codec_t* codec = new_codec(L);
    number_config(L, &codec->config.number);
    codec->codec = tlv_number_codec(&codec->config.number);
    codec->rep = &number_rep;
    return 1;
}

/* The userdata environment retains the rule's tag and name strings. */
static int schema_number_codec(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 1);
    lua_codec_t* codec = new_codec(L);
    lua_newtable(L);
    lua_getfield(L, 1, "rule");
    if (!lua_istable(L, -1)) return luaL_error(L, "rule must be a length rule table");
    opentlv_lua_schema_entry(L, 4, 3, &codec->config.schema_number.entry);
    lua_pop(L, 1);
    set_environment(L, 2);
    codec->config.schema_number.number.schema = &codec->config.schema_number.entry;
    number_config(L, &codec->config.schema_number.number.representation);
    codec->codec = tlv_schema_number_codec(&codec->config.schema_number.number);
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

/* A GC-visible userdata environment, rather than registry roots, permits
 * collection even when a callback closes over its own codec. Snapshots the
 * optional callbacks of the table at index 1 into a new environment table left
 * on the stack, so mutation of the caller's table cannot change the
 * descriptor; returns bit 1 for `decode` and bit 2 for `encode`. */
static int snapshot_callbacks(lua_State* L) {
    static const char* const names[] = {"decode", "encode"};
    int                      present = 0;
    lua_newtable(L);
    for (int i = 0; i < 2; ++i) {
        lua_getfield(L, 1, names[i]);
        if (!lua_isnil(L, -1)) {
            luaL_checktype(L, -1, LUA_TFUNCTION);
            present |= 1 << i;
        }
        lua_setfield(L, -2, names[i]);
    }
    return present;
}

static int custom_codec(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_codec_t* codec = new_codec(L);
    codec->custom = 1;
    int callbacks = snapshot_callbacks(L);
    if (callbacks & 1) codec->codec.decode = custom_decode;
    if (callbacks & 2) codec->codec.encode = custom_encode;
    set_environment(L, -2);
    return 1;
}

/* opentlv.structure_codec{format, schema, max_depth, max_elements, decode,
 * encode}: the environment roots the Format, the Schema and the callbacks.
 * Each call copies the descriptor with its own callback frame as context. */
static int structure_codec_new(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 1);
    tlv_structure_codec_t* codec = lua_newuserdata(L, sizeof(*codec));
    memset(codec, 0, sizeof(*codec));
    luaL_getmetatable(L, STRUCTURE_CODEC_MT);
    lua_setmetatable(L, -2);
    int callbacks = snapshot_callbacks(L);
    if (callbacks & 1) codec->decode = structure_decode;
    if (callbacks & 2) codec->encode = structure_encode;
    lua_getfield(L, 1, "format");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_getfield(L, LUA_REGISTRYINDEX, OPENTLV_LUA_DEFAULT_FORMAT_KEY);
        if (lua_isnil(L, -1)) return luaL_error(L, "format is required when BER is disabled");
    }
    codec->format = &opentlv_lua_check_format(L, -1)->format;
    lua_setfield(L, 3, "format");
    lua_getfield(L, 1, "schema");
    if (!lua_isnil(L, -1)) codec->schema = opentlv_lua_check_structure_schema(L, -1);
    lua_setfield(L, 3, "schema");
    codec->max_depth = opentlv_lua_size_option(L, 1, "max_depth", 32);
    codec->max_elements = opentlv_lua_size_option(L, 1, "max_elements", 100000);
    set_environment(L, 2);
    return 1;
}

static int structure_codec_decode(lua_State* L) {
    tlv_structure_codec_t* codec = luaL_checkudata(L, 1, STRUCTURE_CODEC_MT);
    size_t                 size;
    const uint8_t*         data = (const uint8_t*)opentlv_lua_codec_string(L, 2, &size);
    lua_settop(L, 2);
    tlv_codec_diagnostic_t diagnostic;
    custom_call_t          call;
    tlv_structure_codec_t  native = *codec;
    unsigned char          dummy;
    prepare_call(L, &call, 0);
    native.context = &call;
    tlv_result_t code =
        tlv_structure_decode(&native, data, size, &dummy, sizeof dummy, &diagnostic);
    if (call.failed) return lua_error(L);
    if (code != TLV_OK) return raise_codec_diagnostic(L, &diagnostic);
    return 1;
}

static int structure_codec_encode(lua_State* L) {
    tlv_structure_codec_t* codec = luaL_checkudata(L, 1, STRUCTURE_CODEC_MT);
    luaL_checkany(L, 2);
    lua_settop(L, 2);
    tlv_codec_diagnostic_t diagnostic;
    custom_call_t          call;
    tlv_structure_codec_t  native = *codec;
    size_t                 required = 0, written = 0;
    prepare_call(L, &call, 1);
    native.context = &call;
    tlv_result_t code =
        tlv_structure_encode(&native, &call, sizeof call, NULL, 0, &required, &diagnostic);
    if (call.failed) return lua_error(L);
    if (code != TLV_OK) return raise_codec_diagnostic(L, &diagnostic);
    uint8_t* bytes = lua_newuserdata(L, required ? required : 1);
    code =
        tlv_structure_encode(&native, &call, sizeof call, bytes, required, &written, &diagnostic);
    if (code != TLV_OK) return raise_codec_diagnostic(L, &diagnostic);
    lua_pushlstring(L, (const char*)bytes, written);
    return 1;
}

void opentlv_lua_open_codec(lua_State* L, int module_index) {
    static const opentlv_lua_method_t methods[] = {
        {"decode", codec_decode}, {"encode", codec_encode}, {NULL, NULL}};
    static const opentlv_lua_method_t structure_methods[] = {
        {"decode", structure_codec_decode}, {"encode", structure_codec_encode}, {NULL, NULL}};
    opentlv_lua_new_type(L, CODEC_MT, methods, NULL, NULL, NULL);
    opentlv_lua_new_type(L, STRUCTURE_CODEC_MT, structure_methods, NULL, NULL, NULL);
    lua_newtable(L);
    opentlv_lua_codec_builtins(L);
    lua_pushcfunction(L, number_codec);
    lua_setfield(L, -2, "number");
    lua_pushcfunction(L, schema_number_codec);
    lua_setfield(L, -2, "schema_number");
    lua_pushcfunction(L, text_codec);
    lua_setfield(L, -2, "text");
    lua_pushcfunction(L, digits_codec);
    lua_setfield(L, -2, "digits");
    lua_setfield(L, module_index, "codecs");
    lua_pushcfunction(L, custom_codec);
    lua_setfield(L, module_index, "codec");
    lua_pushcfunction(L, structure_codec_new);
    lua_setfield(L, module_index, "structure_codec");
}
