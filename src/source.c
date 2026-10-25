// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "source.h"
#include "codec.h"
#include "common.h"
#include "error.h"

#include <string.h>

#define SOURCE_MT "opentlv.Source"

/* The decode result of one element. tlv_source_t borrows the input string and
 * the Format descriptor (including format-supplied identifier storage), so
 * both stay referenced until __gc. */
typedef struct lua_source {
    tlv_source_t source;
    size_t       offset;
    int          data_ref;
    int          format_ref;
} lua_source_t;

static lua_source_t* check_source(lua_State* L, int arg) {
    lua_source_t* self = (lua_source_t*)luaL_checkudata(L, arg, SOURCE_MT);
    if (self->data_ref == LUA_NOREF) luaL_error(L, "source has been collected");
    return self;
}

const tlv_source_t* opentlv_lua_check_source(lua_State* L, int arg) {
    return &check_source(L, arg)->source;
}

void opentlv_lua_push_source(lua_State* L, int data_index, int format_index,
                             const tlv_source_t* source, size_t offset) {
    lua_source_t* self = (lua_source_t*)lua_newuserdata(L, sizeof(*self));
    memset(self, 0, sizeof(*self));
    self->data_ref = LUA_NOREF;
    self->format_ref = LUA_NOREF;
    luaL_getmetatable(L, SOURCE_MT);
    lua_setmetatable(L, -2);
    lua_pushvalue(L, format_index);
    self->format_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushvalue(L, data_index);
    self->data_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    self->source = *source;
    self->offset = offset;
}

/* source:element() -> {tag, length, value, offset} of the original decode. */
static int source_element(lua_State* L) {
    lua_source_t* self = check_source(L, 1);
    int           code = opentlv_lua_push_element(L, &self->source.element, self->offset);
    if (code != TLV_OK) return opentlv_lua_raise(L, (tlv_result_t)code, 1, self->offset);
    return 1;
}

/* source:bytes() -> the complete original encoding, trailer included. */
static int source_bytes(lua_State* L) {
    lua_source_t* self = check_source(L, 1);
    lua_pushlstring(L, (const char*)self->source.data, self->source.size);
    return 1;
}

static void set_range(lua_State* L, const tlv_range_t* range, const char* name) {
    if (!range->present) return;
    lua_newtable(L);
    opentlv_lua_codec_push_uint(L, range->offset);
    lua_setfield(L, -2, "offset");
    opentlv_lua_codec_push_uint(L, range->size);
    lua_setfield(L, -2, "size");
    lua_setfield(L, -2, name);
}

/* source:layout() -> {size, header, tag, length, value, trailer}; each known
 * field is an {offset, size} table relative to the element start. */
static int source_layout(lua_State* L) {
    lua_source_t* self = check_source(L, 1);
    lua_newtable(L);
    opentlv_lua_codec_push_uint(L, self->source.size);
    lua_setfield(L, -2, "size");
    set_range(L, &self->source.header, "header");
    set_range(L, &self->source.tag, "tag");
    set_range(L, &self->source.length, "length");
    set_range(L, &self->source.value, "value");
    set_range(L, &self->source.trailer, "trailer");
    return 1;
}

static int source_offset(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)check_source(L, 1)->offset);
    return 1;
}

static int source_format(lua_State* L) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, check_source(L, 1)->format_ref);
    return 1;
}

/* source:preserve([element]) -> the original bytes, through
 * tlv_source_preserve(), only while `element` (default: the decoded one)
 * still has the decoded Tag and Value. */
static int source_preserve(lua_State* L) {
    lua_source_t* self = check_source(L, 1);
    tlv_element_t element = self->source.element;
    if (!lua_isnoneornil(L, 2)) opentlv_lua_check_element(L, 2, &element);
    size_t       written = 0;
    uint8_t*     buffer = NULL;
    tlv_result_t code = tlv_source_preserve(&self->source, &element, NULL, 0, &written);
    if (code == TLV_OK && written) {
        buffer = (uint8_t*)lua_newuserdata(L, written);
        code = tlv_source_preserve(&self->source, &element, buffer, written, &written);
    }
    if (code != TLV_OK) {
        opentlv_lua_push_error(L, code, 0, 0);
        lua_pushliteral(L, "preserve");
        lua_setfield(L, -2, "operation");
        return lua_error(L);
    }
    lua_pushlstring(L, buffer ? (const char*)buffer : "", written);
    return 1;
}

static int source_gc(lua_State* L) {
    lua_source_t* self = (lua_source_t*)luaL_checkudata(L, 1, SOURCE_MT);
    luaL_unref(L, LUA_REGISTRYINDEX, self->data_ref);
    luaL_unref(L, LUA_REGISTRYINDEX, self->format_ref);
    self->data_ref = LUA_NOREF;
    self->format_ref = LUA_NOREF;
    return 0;
}

void opentlv_lua_open_source(lua_State* L) {
    static const opentlv_lua_method_t methods[] = {
        {"element", source_element},
        {"bytes", source_bytes},
        {"layout", source_layout},
        {"offset", source_offset},
        {"format", source_format},
        {"preserve", source_preserve},
        {NULL, NULL},
    };
    opentlv_lua_new_type(L, SOURCE_MT, methods, source_gc, NULL, NULL);
}
