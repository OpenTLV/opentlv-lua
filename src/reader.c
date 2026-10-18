// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "reader.h"
#include "common.h"
#include "error.h"
#include "format.h"

#include <tlv/reader/reader.h>

#define OPENTLV_LUA_READER_MT "opentlv.Reader"

typedef struct tlv_lua_reader {
    tlv_reader_t reader;
    /* Registry references keeping the source string and the format object
     * alive for as long as this reader is: `reader.data` borrows the
     * string's bytes, and `reader.format` (for opentlv.formats.fixed())
     * borrows the format object's embedded tlv_fixed_format_t. */
    int data_ref;
    int format_ref;
} tlv_lua_reader_t;

static tlv_lua_reader_t* check_reader(lua_State* L, int arg) {
    return (tlv_lua_reader_t*)luaL_checkudata(L, arg, OPENTLV_LUA_READER_MT);
}

/* Reads the next element, or returns no element at the end of input; shared by
 * the explicit reader:next() method and the reader(state, control) __call
 * form generic-for uses (see reader_call() below). */
static int reader_next_impl(lua_State* L, tlv_lua_reader_t* self) {
    if (tlv_reader_at_end(&self->reader)) {
        lua_pushnil(L);
        return 1;
    }

    size_t                  offset = self->reader.pos;
    tlv_element_t           element;
    tlv_reader_diagnostic_t diag;
    tlv_reader_diagnostic_init(&diag);

    tlv_result_t code = tlv_reader_next_diag(&self->reader, &element, &diag);
    if (code != TLV_OK) {
        return opentlv_lua_raise_reader_error(L, code, &diag);
    }

    int narrow_code = opentlv_lua_push_element(L, &element, offset);
    if (narrow_code != TLV_OK) {
        return opentlv_lua_raise(L, (tlv_result_t)narrow_code, 1, offset);
    }
    return 1;
}

static int reader_method_next(lua_State* L) {
    return reader_next_impl(L, check_reader(L, 1));
}

/* Lets a Reader be used directly as a generic-for iterator:
 * `for element in reader do ... end`. Lua's generic for calls the sole
 * explist value as `f(state, control)` on every iteration; since `reader`
 * is a userdata rather than a function, that call goes through __call,
 * which always receives the object itself as its first argument. The
 * remaining (state, control) arguments carry no information a Reader
 * needs, since its own position is tracked internally, so they are
 * ignored. */
static int reader_call(lua_State* L) {
    return reader_next_impl(L, check_reader(L, 1));
}

static int reader_at_end(lua_State* L) {
    tlv_lua_reader_t* self = check_reader(L, 1);
    lua_pushboolean(L, tlv_reader_at_end(&self->reader));
    return 1;
}

static int reader_position(lua_State* L) {
    tlv_lua_reader_t* self = check_reader(L, 1);
    lua_pushinteger(L, (lua_Integer)self->reader.pos);
    return 1;
}

static int reader_format(lua_State* L) {
    tlv_lua_reader_t* self = check_reader(L, 1);
    lua_rawgeti(L, LUA_REGISTRYINDEX, self->format_ref);
    return 1;
}

static int reader_gc(lua_State* L) {
    tlv_lua_reader_t* self = check_reader(L, 1);
    luaL_unref(L, LUA_REGISTRYINDEX, self->data_ref);
    luaL_unref(L, LUA_REGISTRYINDEX, self->format_ref);
    return 0;
}

/* opentlv.reader(data, format) -> Reader
 *
 * `format` defaults to opentlv.formats.ber when omitted. */
static int l_reader_new(lua_State* L) {
    size_t      len;
    const char* data = luaL_checklstring(L, 1, &len);

    if (lua_isnoneornil(L, 2)) {
        lua_getfield(L, LUA_REGISTRYINDEX, OPENTLV_LUA_DEFAULT_FORMAT_KEY);
        if (lua_isnil(L, -1)) {
            return luaL_argerror(L, 2, "format is required when BER is disabled");
        }
    } else {
        lua_pushvalue(L, 2);
    }
    int               format_index = lua_gettop(L);
    tlv_lua_format_t* format = opentlv_lua_check_format(L, format_index);

    tlv_lua_reader_t* self = (tlv_lua_reader_t*)lua_newuserdata(L, sizeof(tlv_lua_reader_t));
    tlv_result_t      code =
        tlv_reader_init(&self->reader, len > 0 ? (const uint8_t*)data : NULL, len, &format->format);
    if (code != TLV_OK) {
        return opentlv_lua_raise(L, code, 0, 0);
    }

    lua_pushvalue(L, 1);
    self->data_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushvalue(L, format_index);
    self->format_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    luaL_getmetatable(L, OPENTLV_LUA_READER_MT);
    lua_setmetatable(L, -2);
    return 1;
}

void opentlv_lua_open_reader(lua_State* L, int module_table_index) {
    static const opentlv_lua_method_t methods[] = {
        {"next", reader_method_next},
        {"at_end", reader_at_end},
        {"position", reader_position},
        {"format", reader_format},
        {NULL, NULL},
    };
    opentlv_lua_new_type(L, OPENTLV_LUA_READER_MT, methods, reader_gc, NULL, reader_call);

    lua_pushcfunction(L, l_reader_new);
    lua_setfield(L, module_table_index, "reader");
}
