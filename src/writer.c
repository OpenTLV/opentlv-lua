// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "writer.h"
#include "common.h"
#include "error.h"
#include "format.h"

#include <tlv/writer/tree.h>
#include <stdlib.h>
#include <string.h>

#define WRITER_MT "opentlv.Writer"
#define TREE_MT "opentlv.TreeWriter"

/* All allocations belong to this userdata; install __gc before allocating.
 * Formats retain the shared registry ownership convention. Open tags are
 * copied by the C Tree Writer into bounded tag storage. */
typedef struct lua_writer {
    tlv_writer_t             writer;
    tlv_tree_writer_t        tree;
    int                      is_tree;
    int                      format_ref;
    uint8_t*                 data;
    uint8_t*                 scratch;
    uint8_t*                 tags;
    tlv_tree_writer_frame_t* frames;
    uint8_t                  empty_tags;
} lua_writer_t;

static lua_writer_t* check_userdata(lua_State* L) {
    /* Both types share methods, but no unrelated userdata is accepted. */
    if (lua_getmetatable(L, 1)) {
        luaL_getmetatable(L, TREE_MT);
        int equal = lua_rawequal(L, -1, -2);
        lua_pop(L, 2);
        if (equal) return (lua_writer_t*)luaL_checkudata(L, 1, TREE_MT);
    }
    return (lua_writer_t*)luaL_checkudata(L, 1, WRITER_MT);
}

static lua_writer_t* check_writer(lua_State* L) {
    lua_writer_t* self = check_userdata(L);
    if (self->format_ref == LUA_NOREF) luaL_error(L, "writer has been collected");
    return self;
}

static size_t option(lua_State* L, const char* name, size_t fallback) {
    if (lua_isnoneornil(L, 2)) return fallback;
    lua_getfield(L, 2, name);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return fallback;
    }
    luaL_checktype(L, -1, LUA_TNUMBER);
#if LUA_VERSION_NUM >= 503
    if (lua_isinteger(L, -1)) {
        lua_Integer value = lua_tointeger(L, -1);
        if (value < 0 || (uintmax_t)value > SIZE_MAX)
            luaL_error(L, "%s must be a nonnegative native integer", name);
        lua_pop(L, 1);
        return (size_t)value;
    }
#endif
    lua_Number value = lua_tonumber(L, -1);
    /* Strict upper bound avoids floating-point rounding of SIZE_MAX. */
    if (!(value >= 0 && value < (lua_Number)SIZE_MAX))
        luaL_error(L, "%s must be a nonnegative native integer", name);
    size_t result = (size_t)value;
    if ((lua_Number)result != value) luaL_error(L, "%s must be an integer", name);
    lua_pop(L, 1);
    return result;
}

static void* allocate(lua_State* L, size_t count, size_t width) {
    if (count == 0) return NULL;
    if (count > SIZE_MAX / width) luaL_error(L, "writer storage size overflow");
    void* result = malloc(count * width);
    if (result == NULL) luaL_error(L, "cannot allocate writer storage");
    return result;
}

static int writer_gc(lua_State* L) {
    lua_writer_t* self = check_userdata(L);
    free(self->data);
    self->data = NULL;
    free(self->scratch);
    self->scratch = NULL;
    free(self->tags);
    self->tags = NULL;
    free(self->frames);
    self->frames = NULL;
    luaL_unref(L, LUA_REGISTRYINDEX, self->format_ref);
    self->format_ref = LUA_NOREF;
    return 0;
}

static int new_writer(lua_State* L, int tree) {
    if (!lua_isnoneornil(L, 2)) luaL_checktype(L, 2, LUA_TTABLE);
    size_t capacity = option(L, "capacity", 1024);
    size_t frames = tree ? option(L, "frame_capacity", TLV_TREE_DEFAULT_DEPTH) : 0;
    size_t scratch = tree ? option(L, "scratch_capacity", capacity) : 0;
    size_t tags = tree ? option(L, "tag_capacity", capacity) : 0;
    size_t depth = tree ? option(L, "max_depth", TLV_TREE_DEFAULT_DEPTH) : 0;
    size_t elements = tree ? option(L, "max_elements", SIZE_MAX) : 0;
    if (lua_isnoneornil(L, 1)) {
        lua_getfield(L, LUA_REGISTRYINDEX, OPENTLV_LUA_DEFAULT_FORMAT_KEY);
        if (lua_isnil(L, -1)) return luaL_argerror(L, 1, "format is required when BER is disabled");
    } else
        lua_pushvalue(L, 1);
    int               format_index = lua_gettop(L);
    tlv_lua_format_t* format = opentlv_lua_check_format(L, format_index);
    lua_writer_t*     self = (lua_writer_t*)lua_newuserdata(L, sizeof(*self));
    memset(self, 0, sizeof(*self));
    self->is_tree = tree;
    self->format_ref = LUA_NOREF;
    luaL_getmetatable(L, tree ? TREE_MT : WRITER_MT);
    lua_setmetatable(L, -2);
    lua_pushvalue(L, format_index);
    self->format_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    self->data = allocate(L, capacity, 1);
    tlv_result_t code;
    if (tree) {
        self->scratch = allocate(L, scratch, 1);
        self->tags = allocate(L, tags, 1);
        self->frames = allocate(L, frames, sizeof(*self->frames));
        code = tlv_tree_writer_init(&self->tree, self->data, capacity, &format->format,
                                    self->frames, frames, self->scratch, scratch, depth, elements);
        /* A non-NULL zero-capacity region keeps copied-Tag mode enabled. */
        if (code == TLV_OK)
            code = tlv_tree_writer_set_tag_storage(&self->tree,
                                                   tags ? self->tags : &self->empty_tags, tags);
    } else
        code = tlv_writer_init(&self->writer, self->data, capacity, &format->format);
    if (code != TLV_OK) return opentlv_lua_raise(L, code, 0, 0);
    return 1;
}

static int writer_new(lua_State* L) {
    return new_writer(L, 0);
}
static int tree_new(lua_State* L) {
    return new_writer(L, 1);
}

static tlv_tag_t read_tag(lua_State* L, int index) {
    if (lua_isnil(L, index)) return tlv_tag(NULL, 0);
    luaL_checktype(L, index, LUA_TSTRING);
    size_t      size;
    const char* bytes = lua_tolstring(L, index, &size);
    return tlv_tag((const uint8_t*)bytes, size);
}

static int write_impl(lua_State* L, lua_writer_t* self, int tag_index, int value_index) {
    tlv_element_t element;
    element.tag = read_tag(L, tag_index);
    luaL_checktype(L, value_index, LUA_TSTRING);
    size_t length;
    element.value.data = (const uint8_t*)lua_tolstring(L, value_index, &length);
    element.value.size = (tlv_size_t)length;
    tlv_writer_diagnostic_t diag;
    tlv_writer_diagnostic_init(&diag);
    tlv_result_t code = self->is_tree
                            ? tlv_tree_writer_write_element_diag(&self->tree, &element, &diag)
                            : tlv_writer_write_element_diag(&self->writer, &element, &diag);
    if (code != TLV_OK) return opentlv_lua_raise_writer_error(L, code, &diag);
    return 0;
}

static int writer_write(lua_State* L) {
    return write_impl(L, check_writer(L), 2, 3);
}

static int writer_element(lua_State* L) {
    lua_writer_t* self = check_writer(L);
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_getfield(L, 2, "tag");
    lua_getfield(L, 2, "value");
    /* __index may execute Lua, including an explicit finalizer call. */
    self = check_writer(L);
    return write_impl(L, self, lua_gettop(L) - 1, lua_gettop(L));
}

static int tree_begin(lua_State* L) {
    luaL_checkudata(L, 1, TREE_MT);
    lua_writer_t*           self = check_writer(L);
    tlv_tag_t               tag = read_tag(L, 2);
    tlv_writer_diagnostic_t diag;
    tlv_writer_diagnostic_init(&diag);
    tlv_result_t code = tlv_tree_writer_begin_diag(&self->tree, tag, &diag);
    if (code != TLV_OK) return opentlv_lua_raise_writer_error(L, code, &diag);
    return 0;
}

static int tree_end(lua_State* L) {
    luaL_checkudata(L, 1, TREE_MT);
    lua_writer_t*           self = check_writer(L);
    tlv_writer_diagnostic_t diag;
    tlv_writer_diagnostic_init(&diag);
    tlv_result_t code = tlv_tree_writer_end_diag(&self->tree, &diag);
    if (code != TLV_OK) return opentlv_lua_raise_writer_error(L, code, &diag);
    return 0;
}

static int writer_bytes(lua_State* L) {
    lua_writer_t* self = check_writer(L);
    if (self->is_tree) {
        tlv_result_t code = tlv_tree_writer_finish(&self->tree);
        if (code != TLV_OK) return opentlv_lua_raise(L, code, 0, 0);
    }
    size_t size =
        self->is_tree ? tlv_tree_writer_size(&self->tree) : tlv_writer_size(&self->writer);
    lua_pushlstring(L, size ? (const char*)self->data : "", size);
    return 1;
}

static int writer_size(lua_State* L) {
    lua_writer_t* self = check_writer(L);
    size_t        size =
        self->is_tree ? tlv_tree_writer_size(&self->tree) : tlv_writer_size(&self->writer);
    lua_pushnumber(L, (lua_Number)size);
    return 1;
}

static int writer_remaining(lua_State* L) {
    luaL_checkudata(L, 1, WRITER_MT);
    lua_writer_t* self = check_writer(L);
    lua_pushnumber(L, (lua_Number)tlv_writer_remaining(&self->writer));
    return 1;
}

static int writer_format(lua_State* L) {
    lua_writer_t* self = check_writer(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, self->format_ref);
    return 1;
}

void opentlv_lua_open_writer(lua_State* L, int module_table_index) {
    static const opentlv_lua_method_t methods[] = {{"write", writer_write},
                                                   {"write_element", writer_element},
                                                   {"bytes", writer_bytes},
                                                   {"size", writer_size},
                                                   {"remaining", writer_remaining},
                                                   {"format", writer_format},
                                                   {NULL, NULL}};
    static const opentlv_lua_method_t tree_methods[] = {
        {"write", writer_write},   {"write_element", writer_element}, {"begin", tree_begin},
        {"end_element", tree_end}, {"finish", writer_bytes},          {"bytes", writer_bytes},
        {"size", writer_size},     {"format", writer_format},         {NULL, NULL}};
    opentlv_lua_new_type(L, WRITER_MT, methods, writer_gc, NULL, NULL);
    opentlv_lua_new_type(L, TREE_MT, tree_methods, writer_gc, NULL, NULL);
    lua_pushcfunction(L, writer_new);
    lua_setfield(L, module_table_index, "writer");
    lua_pushcfunction(L, tree_new);
    lua_setfield(L, module_table_index, "tree_writer");
}
