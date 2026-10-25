// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "writer.h"
#include "common.h"
#include "error.h"
#include "codec.h"
#include "format.h"
#include "source.h"

#include <tlv/writer/tree.h>
#include <stdint.h>
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
    return opentlv_lua_size_option(L, 2, name, fallback);
}

/* Pushes the Format at `arg`, or BER when it is none or nil, and returns it. */
static tlv_lua_format_t* push_format(lua_State* L, int arg) {
    if (lua_isnoneornil(L, arg)) {
        lua_getfield(L, LUA_REGISTRYINDEX, OPENTLV_LUA_DEFAULT_FORMAT_KEY);
        if (lua_isnil(L, -1)) luaL_argerror(L, arg, "format is required when BER is disabled");
    } else
        lua_pushvalue(L, arg);
    return opentlv_lua_check_format(L, lua_gettop(L));
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
    size_t            capacity = option(L, "capacity", 1024);
    size_t            frames = tree ? option(L, "frame_capacity", TLV_TREE_DEFAULT_DEPTH) : 0;
    size_t            scratch = tree ? option(L, "scratch_capacity", capacity) : 0;
    size_t            tags = tree ? option(L, "tag_capacity", capacity) : 0;
    size_t            depth = tree ? option(L, "max_depth", TLV_TREE_DEFAULT_DEPTH) : 0;
    size_t            elements = tree ? option(L, "max_elements", SIZE_MAX) : 0;
    tlv_lua_format_t* format = push_format(L, 1);
    int               format_index = lua_gettop(L);
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
    check_writer(L);
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_getfield(L, 2, "tag");
    lua_getfield(L, 2, "value");
    /* __index may execute Lua, including an explicit finalizer call. */
    lua_writer_t* self = check_writer(L);
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

/* writer:copy_encoded(bytes) appends raw bytes without framing validation. */
static int writer_copy_encoded(lua_State* L) {
    luaL_checkudata(L, 1, WRITER_MT);
    lua_writer_t* self = check_writer(L);
    luaL_checktype(L, 2, LUA_TSTRING);
    size_t                  size;
    const char*             bytes = lua_tolstring(L, 2, &size);
    tlv_writer_diagnostic_t diag;
    tlv_writer_diagnostic_init(&diag);
    tlv_result_t code = tlv_writer_copy_encoded_diag(
        &self->writer, size ? (const uint8_t*)bytes : NULL, size, &diag);
    if (code != TLV_OK) return opentlv_lua_raise_writer_error(L, code, &diag);
    return 0;
}

/* writer:preserve(source [, element]) appends the original bytes of `source`
 * while `element` (default: the decoded one) is unchanged. */
static int writer_preserve(lua_State* L) {
    luaL_checkudata(L, 1, WRITER_MT);
    check_writer(L);
    tlv_element_t element = opentlv_lua_check_source(L, 2)->element;
    if (!lua_isnoneornil(L, 3)) opentlv_lua_check_element(L, 3, &element);
    /* __index may execute Lua, including an explicit finalizer call. */
    lua_writer_t*           self = check_writer(L);
    const tlv_source_t*     source = opentlv_lua_check_source(L, 2);
    tlv_writer_diagnostic_t diag;
    tlv_writer_diagnostic_init(&diag);
    tlv_result_t code = tlv_writer_preserve_diag(&self->writer, source, &element, &diag);
    if (code != TLV_OK) return opentlv_lua_raise_writer_error(L, code, &diag);
    return 0;
}

/* Reads a structural event table at `index`: "kind" ("begin", "element" or
 * "end"), "depth" and optional "skipped", plus "tag" and "value" for begin
 * and element events; a begin Value is ignored and may be nil. Pushes the Tag
 * and Value (nil when absent), which stay on the stack, so their bytes remain
 * rooted while `event` is used. */
static void check_event(lua_State* L, int index, tlv_tree_event_t* event) {
    luaL_checktype(L, index, LUA_TTABLE);
    memset(event, 0, sizeof(*event));
    lua_getfield(L, index, "kind");
    const char* kind = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : NULL;
    if (kind && strcmp(kind, "begin") == 0)
        event->kind = TLV_TREE_BEGIN;
    else if (kind && strcmp(kind, "element") == 0)
        event->kind = TLV_TREE_ELEMENT;
    else if (kind && strcmp(kind, "end") == 0)
        event->kind = TLV_TREE_END;
    else
        luaL_error(L, "event kind must be \"begin\", \"element\" or \"end\"");
    lua_pop(L, 1);
    lua_getfield(L, index, "depth");
    event->depth = opentlv_lua_check_size(L, -1, "depth");
    lua_pop(L, 1);
    lua_getfield(L, index, "skipped");
    if (!lua_isnil(L, -1)) {
        if (!lua_isboolean(L, -1)) luaL_error(L, "skipped must be a boolean");
        event->skipped = lua_toboolean(L, -1);
    }
    lua_pop(L, 1);
    if (event->kind == TLV_TREE_END) {
        lua_pushnil(L);
        lua_pushnil(L);
        return;
    }
    lua_getfield(L, index, "tag");
    lua_getfield(L, index, "value");
    size_t size;
    if (!lua_isnil(L, -2)) {
        if (lua_type(L, -2) != LUA_TSTRING) luaL_error(L, "event tag must be a string or nil");
        const char* tag = lua_tolstring(L, -2, &size);
        event->element.tag = tlv_tag((const uint8_t*)tag, size);
    }
    if (lua_type(L, -1) == LUA_TSTRING) {
        event->element.value.data = (const uint8_t*)lua_tolstring(L, -1, &size);
        event->element.value.size = (tlv_size_t)size;
    } else if (event->kind == TLV_TREE_ELEMENT || !lua_isnil(L, -1)) {
        luaL_error(L, "event value must be a string");
    }
}

/* tree_writer:write_event(event), for example one from tree_reader:next_event(). */
static int tree_write_event(lua_State* L) {
    luaL_checkudata(L, 1, TREE_MT);
    check_writer(L);
    tlv_tree_event_t event;
    check_event(L, 2, &event);
    /* __index may execute Lua, including an explicit finalizer call. */
    lua_writer_t*           self = check_writer(L);
    tlv_writer_diagnostic_t diag;
    tlv_writer_diagnostic_init(&diag);
    tlv_result_t code = tlv_tree_writer_write_event_diag(&self->tree, &event, &diag);
    if (code != TLV_OK) return opentlv_lua_raise_writer_error(L, code, &diag);
    return 0;
}

/* Measurement state shared with the protected pull function. Core calls the
 * provider callbacks from native frames, so Lua runs only inside lua_pcall()
 * and an error is stored and rethrown after Core returns. */
typedef struct measure_ctx {
    lua_State*       L;
    int              events;
    int              array;
    lua_Integer      next;
    lua_Integer      retained;
    int              pull_index;
    int              items_index;
    int              retain_index;
    int              error_index;
    int              failed;
    int              done;
    tlv_tree_event_t event;
    int              constructed;
} measure_ctx_t;

static void retain(lua_State* L, measure_ctx_t* ctx, int index) {
    if (lua_type(L, index) != LUA_TSTRING) return;
    lua_pushvalue(L, index);
    lua_rawseti(L, 3, (int)++ctx->retained);
}

/* pull(ctx, items, retained) reads one item or event. Tag and Value strings
 * are retained until measurement returns: Core keeps parent Tags and
 * diagnostics may refer to the last Tag. */
static int measure_pull(lua_State* L) {
    measure_ctx_t* ctx = (measure_ctx_t*)lua_touserdata(L, 1);
    if (ctx->array)
        lua_rawgeti(L, 2, (int)++ctx->next);
    else {
        lua_pushvalue(L, 2);
        lua_call(L, 0, 1);
    }
    if (lua_isnil(L, -1)) {
        ctx->done = 1;
        return 0;
    }
    int item = lua_gettop(L);
    if (ctx->events) {
        check_event(L, item, &ctx->event);
    } else {
        luaL_checktype(L, item, LUA_TTABLE);
        memset(&ctx->event, 0, sizeof(ctx->event));
        lua_getfield(L, item, "depth");
        ctx->event.depth = opentlv_lua_check_size(L, -1, "depth");
        lua_getfield(L, item, "constructed");
        ctx->constructed = lua_toboolean(L, -1);
        lua_getfield(L, item, "tag");
        lua_getfield(L, item, "value");
        size_t size;
        if (!lua_isnil(L, -2)) {
            if (lua_type(L, -2) != LUA_TSTRING) luaL_error(L, "item tag must be a string or nil");
            const char* tag = lua_tolstring(L, -2, &size);
            ctx->event.element.tag = tlv_tag((const uint8_t*)tag, size);
        }
        /* A constructed item's Value is ignored; Tree Writer builds it. */
        if (lua_type(L, -1) == LUA_TSTRING) {
            ctx->event.element.value.data = (const uint8_t*)lua_tolstring(L, -1, &size);
            ctx->event.element.value.size = (tlv_size_t)size;
        } else if (!ctx->constructed || !lua_isnil(L, -1)) {
            luaL_error(L, "item value must be a string");
        }
    }
    retain(L, ctx, lua_gettop(L) - 1);
    retain(L, ctx, lua_gettop(L));
    return 0;
}

static tlv_result_t measure_next(measure_ctx_t* ctx) {
    if (ctx->done) return TLV_END;
    lua_State* L = ctx->L;
    lua_pushvalue(L, ctx->pull_index);
    lua_pushlightuserdata(L, ctx);
    lua_pushvalue(L, ctx->items_index);
    lua_pushvalue(L, ctx->retain_index);
    if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
        lua_replace(L, ctx->error_index);
        ctx->failed = 1;
        return TLV_ERR_CALLBACK;
    }
    return ctx->done ? TLV_END : TLV_OK;
}

static tlv_result_t measure_item(void* context, tlv_element_t* element, size_t* depth,
                                 int* constructed) {
    measure_ctx_t* ctx = (measure_ctx_t*)context;
    tlv_result_t   code = measure_next(ctx);
    if (code == TLV_OK) {
        *element = ctx->event.element;
        *depth = ctx->event.depth;
        *constructed = ctx->constructed;
    }
    return code;
}

static tlv_result_t measure_event(void* context, tlv_tree_event_t* event) {
    measure_ctx_t* ctx = (measure_ctx_t*)context;
    tlv_result_t   code = measure_next(ctx);
    if (code == TLV_OK) *event = ctx->event;
    return code;
}

static void* workspace(lua_State* L, size_t count, size_t width) {
    if (count == 0) return NULL;
    if (count > SIZE_MAX / width) luaL_error(L, "writer storage size overflow");
    return lua_newuserdata(L, count * width);
}

/* measure_tree(format, items, options) and measure_events(format, events,
 * options) -> the staged encoding, whose length is the measured size. Items
 * or events are an array or a callable returning one per call, nil at the
 * end. Workspace exhaustion reports "required_data" and "required_scratch". */
static int measure(lua_State* L, int events) {
    int array = lua_istable(L, 2);
    if (luaL_getmetafield(L, 2, "__call")) {
        lua_pop(L, 1);
        array = 0;
    } else if (!array && !lua_isfunction(L, 2)) {
        return luaL_argerror(L, 2, "table or callable expected");
    }
    if (!lua_isnoneornil(L, 3)) luaL_checktype(L, 3, LUA_TTABLE);
    size_t capacity = opentlv_lua_size_option(L, 3, "capacity", 1024);
    size_t scratch = opentlv_lua_size_option(L, 3, "scratch_capacity", capacity);
    size_t frames = opentlv_lua_size_option(L, 3, "frame_capacity", TLV_TREE_DEFAULT_DEPTH);
    size_t depth = opentlv_lua_size_option(L, 3, "max_depth", TLV_TREE_DEFAULT_DEPTH);
    size_t elements = opentlv_lua_size_option(L, 3, "max_elements", SIZE_MAX);
    lua_settop(L, 3);
    tlv_lua_format_t* format = push_format(L, 1);
    /* Lua-owned storage needs no cleanup when an error unwinds. */
    tlv_tree_writer_workspace_t space;
    memset(&space, 0, sizeof(space));
    space.frames = (tlv_tree_writer_frame_t*)workspace(L, frames, sizeof(*space.frames));
    space.frame_capacity = frames;
    space.data = (uint8_t*)workspace(L, capacity, 1);
    space.data_capacity = capacity;
    space.scratch = (uint8_t*)workspace(L, scratch, 1);
    space.scratch_capacity = scratch;
    measure_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.L = L;
    ctx.events = events;
    ctx.array = array;
    ctx.items_index = 2;
    luaL_checkstack(L, 8, "measurement stack");
    lua_newtable(L);
    ctx.retain_index = lua_gettop(L);
    lua_pushcfunction(L, measure_pull);
    ctx.pull_index = lua_gettop(L);
    lua_pushnil(L);
    ctx.error_index = lua_gettop(L);
    tlv_writer_diagnostic_t diag;
    tlv_writer_diagnostic_init(&diag);
    size_t       size = 0;
    tlv_result_t code = events
                            ? tlv_tree_writer_measure_events(&format->format, measure_event, &ctx,
                                                             &space, depth, elements, &size, &diag)
                            : tlv_tree_writer_measure(&format->format, measure_item, &ctx, &space,
                                                      depth, elements, &size, &diag);
    if (ctx.failed) {
        lua_pushvalue(L, ctx.error_index);
        return lua_error(L);
    }
    if (code != TLV_OK) {
        opentlv_lua_push_writer_error(L, code, &diag);
        if (space.required_data) {
            opentlv_lua_codec_push_uint(L, space.required_data);
            lua_setfield(L, -2, "required_data");
        }
        if (space.required_scratch) {
            opentlv_lua_codec_push_uint(L, space.required_scratch);
            lua_setfield(L, -2, "required_scratch");
        }
        return lua_error(L);
    }
    lua_pushlstring(L, size ? (const char*)space.data : "", size);
    return 1;
}

static int l_measure_tree(lua_State* L) {
    return measure(L, 0);
}
static int l_measure_events(lua_State* L) {
    return measure(L, 1);
}

/* encoded_size(tag, value, format): a string Value is measured as an Element;
 * a number is a Value length for formats that size without reading Value. */
static int l_encoded_size(lua_State* L) {
    tlv_tag_t         tag = read_tag(L, 1);
    tlv_lua_format_t* format = push_format(L, 3);
    size_t            size;
    if (lua_type(L, 2) == LUA_TNUMBER) {
        size_t       length = opentlv_lua_check_size(L, 2, "length");
        tlv_result_t code = tlv_encoded_size(tag, length, &format->format, &size);
        if (code != TLV_OK) return opentlv_lua_raise(L, code, 0, 0);
    } else {
        luaL_checktype(L, 2, LUA_TSTRING);
        tlv_element_t element;
        size_t        length;
        element.tag = tag;
        element.value.data = (const uint8_t*)lua_tolstring(L, 2, &length);
        element.value.size = (tlv_size_t)length;
        tlv_writer_diagnostic_t diag;
        tlv_writer_diagnostic_init(&diag);
        tlv_result_t code = tlv_element_encoded_size_diag(&element, &format->format, &size, &diag);
        if (code != TLV_OK) return opentlv_lua_raise_writer_error(L, code, &diag);
    }
    lua_pushinteger(L, (lua_Integer)size);
    return 1;
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
                                                   {"copy_encoded", writer_copy_encoded},
                                                   {"preserve", writer_preserve},
                                                   {"bytes", writer_bytes},
                                                   {"size", writer_size},
                                                   {"remaining", writer_remaining},
                                                   {"format", writer_format},
                                                   {NULL, NULL}};
    static const opentlv_lua_method_t tree_methods[] = {{"write", writer_write},
                                                        {"write_element", writer_element},
                                                        {"begin", tree_begin},
                                                        {"end_element", tree_end},
                                                        {"write_event", tree_write_event},
                                                        {"finish", writer_bytes},
                                                        {"bytes", writer_bytes},
                                                        {"size", writer_size},
                                                        {"format", writer_format},
                                                        {NULL, NULL}};
    opentlv_lua_new_type(L, WRITER_MT, methods, writer_gc, NULL, NULL);
    opentlv_lua_new_type(L, TREE_MT, tree_methods, writer_gc, NULL, NULL);
    lua_pushcfunction(L, writer_new);
    lua_setfield(L, module_table_index, "writer");
    lua_pushcfunction(L, tree_new);
    lua_setfield(L, module_table_index, "tree_writer");
    lua_pushcfunction(L, l_encoded_size);
    lua_setfield(L, module_table_index, "encoded_size");
    lua_pushcfunction(L, l_measure_tree);
    lua_setfield(L, module_table_index, "measure_tree");
    lua_pushcfunction(L, l_measure_events);
    lua_setfield(L, module_table_index, "measure_events");
}
