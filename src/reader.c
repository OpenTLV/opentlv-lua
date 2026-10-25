// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "reader.h"
#include "common.h"
#include "error.h"
#include "format.h"
#include "source.h"
#include "visitor.h"

#include <tlv/defaults.h>
#include <tlv/reader/reader.h>
#include <tlv/reader/tree.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define OPENTLV_LUA_READER_MT "opentlv.Reader"
#define TREE_READER_MT "opentlv.TreeReader"

typedef struct tlv_lua_reader {
    tlv_reader_t reader;
    /* Registry references keeping the current input string and the format
     * object alive for as long as this reader is: `reader.data` borrows the
     * string's bytes, and `reader.format` borrows the format object's
     * embedded descriptor. set_input() replaces the input reference. */
    int data_ref;
    int format_ref;
    /* Nonzero while visit() runs native traversal over this cursor. */
    int busy;
} tlv_lua_reader_t;

/* Same ownership as tlv_lua_reader_t, plus caller-owned frame storage the C
 * Tree Reader borrows; __gc is installed before the frames are allocated. */
typedef struct lua_tree_reader {
    tlv_tree_reader_t reader;
    tlv_tree_frame_t* frames;
    int               data_ref;
    int               format_ref;
    int               busy;
} lua_tree_reader_t;

static void check_idle(lua_State* L, int data_ref, int busy) {
    if (data_ref == LUA_NOREF) luaL_error(L, "reader has been collected");
    if (busy) luaL_error(L, "cannot use a reader inside its own visitor callback");
}

static tlv_lua_reader_t* check_reader(lua_State* L, int arg) {
    tlv_lua_reader_t* self = (tlv_lua_reader_t*)luaL_checkudata(L, arg, OPENTLV_LUA_READER_MT);
    check_idle(L, self->data_ref, self->busy);
    return self;
}

static lua_tree_reader_t* check_tree_reader(lua_State* L, int arg) {
    lua_tree_reader_t* self = (lua_tree_reader_t*)luaL_checkudata(L, arg, TREE_READER_MT);
    check_idle(L, self->data_ref, self->busy);
    return self;
}

/* Pushes the Format at `arg`, or BER when it is none or nil, and returns it. */
static tlv_lua_format_t* push_format(lua_State* L, int arg) {
    if (lua_isnoneornil(L, arg)) {
        lua_getfield(L, LUA_REGISTRYINDEX, OPENTLV_LUA_DEFAULT_FORMAT_KEY);
        if (lua_isnil(L, -1)) luaL_argerror(L, arg, "format is required when BER is disabled");
    } else {
        lua_pushvalue(L, arg);
    }
    return opentlv_lua_check_format(L, lua_gettop(L));
}

static tlv_lua_format_t* retained_format(lua_State* L, int format_ref) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, format_ref);
    tlv_lua_format_t* format = opentlv_lua_check_format(L, -1);
    lua_pop(L, 1); /* Still referenced by the cursor. */
    return format;
}

static int options_final_input(lua_State* L, int options_index) {
    if (!lua_isnoneornil(L, options_index)) luaL_checktype(L, options_index, LUA_TTABLE);
    return opentlv_lua_boolean_option(L, options_index, "final_input", 1);
}

/* Reads set_input(data, discard, final_input) arguments and references the new
 * window before the cursor borrows it. The binding checks the prefix contract
 * Core leaves to the caller: the window must start with the old undiscarded
 * bytes. Returns the new reference, or LUA_NOREF with *code set. */
static int new_window(lua_State* L, const uint8_t* old_data, size_t old_size, const char** data,
                      size_t* size, size_t* discard, int* final_input, tlv_result_t* code) {
    *data = luaL_checklstring(L, 2, size);
    *discard = lua_isnoneornil(L, 3) ? 0 : opentlv_lua_check_size(L, 3, "discard");
    *final_input = 1;
    if (!lua_isnoneornil(L, 4)) {
        luaL_checktype(L, 4, LUA_TBOOLEAN);
        *final_input = lua_toboolean(L, 4);
    }
    size_t kept = *discard <= old_size ? old_size - *discard : 0;
    if (*discard > old_size || *size < kept ||
        (kept && memcmp(old_data + *discard, *data, kept) != 0)) {
        *code = TLV_ERR_INVALID_ARG;
        return LUA_NOREF;
    }
    *code = TLV_OK;
    lua_pushvalue(L, 2);
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

/* Reads the next element, or returns no element at the end of final input;
 * shared by the explicit reader:next() method and the reader(state, control)
 * __call form generic-for uses (see reader_call() below). */
static int reader_next_impl(lua_State* L, tlv_lua_reader_t* self) {
    size_t                  offset = tlv_reader_offset(&self->reader);
    tlv_element_t           element;
    tlv_reader_diagnostic_t diag;
    tlv_reader_diagnostic_init(&diag);

    tlv_result_t code = tlv_reader_next_diag(&self->reader, &element, &diag);
    if (code == TLV_END) {
        lua_pushnil(L);
        return 1;
    }
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

/* reader:next_source() -> element, source, from the same single decode. */
static int reader_next_source(lua_State* L) {
    tlv_lua_reader_t*       self = check_reader(L, 1);
    size_t                  offset = tlv_reader_offset(&self->reader);
    tlv_element_t           element;
    tlv_source_t            source;
    tlv_reader_diagnostic_t diag;
    tlv_reader_diagnostic_init(&diag);
    tlv_result_t code = tlv_reader_next_source_diag(&self->reader, &element, &source, &diag);
    if (code == TLV_END) {
        lua_pushnil(L);
        return 1;
    }
    if (code != TLV_OK) return opentlv_lua_raise_reader_error(L, code, &diag);
    int narrow_code = opentlv_lua_push_element(L, &element, offset);
    if (narrow_code != TLV_OK) return opentlv_lua_raise(L, (tlv_result_t)narrow_code, 1, offset);
    lua_rawgeti(L, LUA_REGISTRYINDEX, self->data_ref);
    lua_rawgeti(L, LUA_REGISTRYINDEX, self->format_ref);
    opentlv_lua_push_source(L, lua_gettop(L) - 1, lua_gettop(L), &source, offset);
    lua_replace(L, -3);
    lua_pop(L, 1);
    return 2;
}

static int reader_at_end(lua_State* L) {
    tlv_lua_reader_t* self = check_reader(L, 1);
    lua_pushboolean(L, tlv_reader_at_end(&self->reader));
    return 1;
}

static int reader_consumed(lua_State* L) {
    tlv_lua_reader_t* self = check_reader(L, 1);
    lua_pushinteger(L, (lua_Integer)tlv_reader_consumed(&self->reader));
    return 1;
}

static int reader_offset(lua_State* L) {
    tlv_lua_reader_t* self = check_reader(L, 1);
    lua_pushinteger(L, (lua_Integer)tlv_reader_offset(&self->reader));
    return 1;
}

static int reader_format(lua_State* L) {
    tlv_lua_reader_t* self = check_reader(L, 1);
    lua_rawgeti(L, LUA_REGISTRYINDEX, self->format_ref);
    return 1;
}

/* reader:set_input(data [, discard [, final_input]]) */
static int reader_set_input(lua_State* L) {
    tlv_lua_reader_t* self = check_reader(L, 1);
    const char*       data;
    size_t            size, discard;
    int               final_input;
    tlv_result_t      code;
    int ref = new_window(L, self->reader.data, self->reader.size, &data, &size, &discard,
                         &final_input, &code);
    if (code == TLV_OK)
        code = tlv_reader_set_input(&self->reader, size ? (const uint8_t*)data : NULL, size,
                                    discard, final_input);
    if (code != TLV_OK) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return opentlv_lua_raise(L, code, 0, 0);
    }
    luaL_unref(L, LUA_REGISTRYINDEX, self->data_ref);
    self->data_ref = ref;
    return 0;
}

/* reader:visit(callback) -> visited, stopped; resumes at the next element. */
static int reader_visit(lua_State* L) {
    tlv_lua_reader_t* self = check_reader(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    tlv_lua_format_t* format = retained_format(L, self->format_ref);
    return opentlv_lua_visit_cursor(L, format, &self->reader, NULL, 2, &self->busy);
}

static int reader_gc(lua_State* L) {
    tlv_lua_reader_t* self = (tlv_lua_reader_t*)luaL_checkudata(L, 1, OPENTLV_LUA_READER_MT);
    if (self->busy) return luaL_error(L, "cannot collect a reader inside its visitor callback");
    luaL_unref(L, LUA_REGISTRYINDEX, self->data_ref);
    luaL_unref(L, LUA_REGISTRYINDEX, self->format_ref);
    self->data_ref = LUA_NOREF;
    self->format_ref = LUA_NOREF;
    return 0;
}

/* opentlv.reader(data, format, options) -> Reader
 *
 * `format` defaults to opentlv.formats.ber when omitted; options.final_input
 * = false starts incremental input. */
static int l_reader_new(lua_State* L) {
    size_t      len;
    const char* data = luaL_checklstring(L, 1, &len);
    int         final_input = options_final_input(L, 3);

    tlv_lua_format_t* format = push_format(L, 2);
    int               format_index = lua_gettop(L);

    tlv_lua_reader_t* self = (tlv_lua_reader_t*)lua_newuserdata(L, sizeof(tlv_lua_reader_t));
    const uint8_t*    bytes = len > 0 ? (const uint8_t*)data : NULL;
    tlv_result_t      code =
        final_input ? tlv_reader_init(&self->reader, bytes, len, &format->format)
                         : tlv_reader_init_incremental(&self->reader, bytes, len, &format->format);
    if (code != TLV_OK) {
        return opentlv_lua_raise(L, code, 0, 0);
    }
    self->busy = 0;

    lua_pushvalue(L, 1);
    self->data_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushvalue(L, format_index);
    self->format_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    luaL_getmetatable(L, OPENTLV_LUA_READER_MT);
    lua_setmetatable(L, -2);
    return 1;
}

/* Pushes one Tree Reader item: the element with "depth" and "constructed". */
static int push_item(lua_State* L, const tlv_tree_item_t* item) {
    int code = opentlv_lua_push_element(L, &item->element, item->offset);
    if (code != TLV_OK) return opentlv_lua_raise(L, (tlv_result_t)code, 1, item->offset);
    lua_pushinteger(L, (lua_Integer)item->depth);
    lua_setfield(L, -2, "depth");
    lua_pushboolean(L, item->constructed);
    lua_setfield(L, -2, "constructed");
    return 1;
}

static int tree_next_impl(lua_State* L, lua_tree_reader_t* self) {
    tlv_tree_item_t         item;
    tlv_reader_diagnostic_t diag;
    tlv_reader_diagnostic_init(&diag);
    tlv_result_t code = tlv_tree_reader_next_diag(&self->reader, &item, &diag);
    if (code == TLV_END) {
        lua_pushnil(L);
        return 1;
    }
    if (code != TLV_OK) return opentlv_lua_raise_reader_error(L, code, &diag);
    return push_item(L, &item);
}

static int tree_next(lua_State* L) {
    return tree_next_impl(L, check_tree_reader(L, 1));
}

/* Generic-for form, as for Reader. */
static int tree_call(lua_State* L) {
    return tree_next_impl(L, check_tree_reader(L, 1));
}

static const char* event_kind_name(tlv_tree_event_kind_t kind) {
    switch (kind) {
        case TLV_TREE_BEGIN: return "begin";
        case TLV_TREE_ELEMENT: return "element";
        default: return "end";
    }
}

/* tree_reader:next_event() -> {kind, depth, offset, skipped, ...}; begin and
 * element events also carry the element fields and "constructed". */
static int tree_next_event(lua_State* L) {
    lua_tree_reader_t*      self = check_tree_reader(L, 1);
    tlv_tree_event_t        event;
    tlv_reader_diagnostic_t diag;
    tlv_reader_diagnostic_init(&diag);
    tlv_result_t code = tlv_tree_reader_next_event_diag(&self->reader, &event, &diag);
    if (code == TLV_END) {
        lua_pushnil(L);
        return 1;
    }
    if (code != TLV_OK) return opentlv_lua_raise_reader_error(L, code, &diag);
    if (event.kind == TLV_TREE_END) {
        lua_newtable(L);
        lua_pushinteger(L, (lua_Integer)event.offset);
        lua_setfield(L, -2, "offset");
    } else {
        int narrow_code = opentlv_lua_push_element(L, &event.element, event.offset);
        if (narrow_code != TLV_OK)
            return opentlv_lua_raise(L, (tlv_result_t)narrow_code, 1, event.offset);
        lua_pushboolean(L, event.kind == TLV_TREE_BEGIN);
        lua_setfield(L, -2, "constructed");
    }
    lua_pushstring(L, event_kind_name(event.kind));
    lua_setfield(L, -2, "kind");
    lua_pushinteger(L, (lua_Integer)event.depth);
    lua_setfield(L, -2, "depth");
    lua_pushboolean(L, event.skipped);
    lua_setfield(L, -2, "skipped");
    return 1;
}

static int tree_skip_subtree(lua_State* L) {
    lua_tree_reader_t* self = check_tree_reader(L, 1);
    tlv_result_t       code = tlv_tree_reader_skip_subtree(&self->reader);
    if (code != TLV_OK) return opentlv_lua_raise(L, code, 0, 0);
    return 0;
}

static int tree_at_end(lua_State* L) {
    lua_tree_reader_t* self = check_tree_reader(L, 1);
    lua_pushboolean(L, tlv_tree_reader_at_end(&self->reader));
    return 1;
}

static int tree_consumed(lua_State* L) {
    lua_tree_reader_t* self = check_tree_reader(L, 1);
    lua_pushinteger(L, (lua_Integer)tlv_tree_reader_consumed(&self->reader));
    return 1;
}

static int tree_offset(lua_State* L) {
    lua_tree_reader_t* self = check_tree_reader(L, 1);
    lua_pushinteger(L, (lua_Integer)tlv_tree_reader_offset(&self->reader));
    return 1;
}

static int tree_format(lua_State* L) {
    lua_tree_reader_t* self = check_tree_reader(L, 1);
    lua_rawgeti(L, LUA_REGISTRYINDEX, self->format_ref);
    return 1;
}

/* tree_reader:set_input(data [, discard [, final_input]]) */
static int tree_set_input(lua_State* L) {
    lua_tree_reader_t* self = check_tree_reader(L, 1);
    const char*        data;
    size_t             size, discard;
    int                final_input;
    tlv_result_t       code;
    int ref = new_window(L, self->reader.input.data, self->reader.input.size, &data, &size,
                         &discard, &final_input, &code);
    if (code == TLV_OK)
        code = tlv_tree_reader_set_input(&self->reader, size ? (const uint8_t*)data : NULL, size,
                                         discard, final_input);
    if (code != TLV_OK) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return opentlv_lua_raise(L, code, 0, 0);
    }
    luaL_unref(L, LUA_REGISTRYINDEX, self->data_ref);
    self->data_ref = ref;
    return 0;
}

/* tree_reader:visit(callback) -> visited, stopped; callback(element, depth). */
static int tree_visit(lua_State* L) {
    lua_tree_reader_t* self = check_tree_reader(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    tlv_lua_format_t* format = retained_format(L, self->format_ref);
    return opentlv_lua_visit_cursor(L, format, NULL, &self->reader, 2, &self->busy);
}

static int tree_gc(lua_State* L) {
    lua_tree_reader_t* self = (lua_tree_reader_t*)luaL_checkudata(L, 1, TREE_READER_MT);
    if (self->busy) return luaL_error(L, "cannot collect a reader inside its visitor callback");
    free(self->frames);
    self->frames = NULL;
    luaL_unref(L, LUA_REGISTRYINDEX, self->data_ref);
    luaL_unref(L, LUA_REGISTRYINDEX, self->format_ref);
    self->data_ref = LUA_NOREF;
    self->format_ref = LUA_NOREF;
    return 0;
}

/* opentlv.tree_reader(data, format, options) -> TreeReader */
static int l_tree_reader_new(lua_State* L) {
    size_t      len;
    const char* data = luaL_checklstring(L, 1, &len);
    int         final_input = options_final_input(L, 3);
    size_t      capacity = opentlv_lua_size_option(L, 3, "frame_capacity", TLV_TREE_DEFAULT_DEPTH);
    size_t      depth = opentlv_lua_size_option(L, 3, "max_depth", TLV_TREE_DEFAULT_DEPTH);
    size_t      elements =
        opentlv_lua_size_option(L, 3, "max_elements", OPENTLV_LUA_DEFAULT_MAX_ELEMENTS);
    if (capacity > SIZE_MAX / sizeof(tlv_tree_frame_t))
        return luaL_error(L, "frame_capacity is too large");

    tlv_lua_format_t*  format = push_format(L, 2);
    int                format_index = lua_gettop(L);
    lua_tree_reader_t* self = (lua_tree_reader_t*)lua_newuserdata(L, sizeof(*self));
    memset(self, 0, sizeof(*self));
    self->data_ref = LUA_NOREF;
    self->format_ref = LUA_NOREF;
    luaL_getmetatable(L, TREE_READER_MT);
    lua_setmetatable(L, -2);
    lua_pushvalue(L, format_index);
    self->format_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushvalue(L, 1);
    self->data_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    if (capacity) {
        self->frames = (tlv_tree_frame_t*)malloc(capacity * sizeof(*self->frames));
        if (!self->frames) return luaL_error(L, "cannot allocate tree reader frames");
    }
    const uint8_t* bytes = len > 0 ? (const uint8_t*)data : NULL;
    tlv_result_t   code =
        final_input ? tlv_tree_reader_init(&self->reader, bytes, len, &format->format, self->frames,
                                             capacity, depth, elements)
                      : tlv_tree_reader_init_incremental(&self->reader, bytes, len, &format->format,
                                                         self->frames, capacity, depth, elements);
    if (code != TLV_OK) return opentlv_lua_raise(L, code, 0, 0);
    return 1;
}

/* opentlv.read(data, format) -> element, size | nil */
static int l_read(lua_State* L) {
    size_t                  size;
    const char*             data = luaL_checklstring(L, 1, &size);
    tlv_lua_format_t*       format = push_format(L, 2);
    tlv_element_t           element;
    size_t                  consumed;
    tlv_reader_diagnostic_t diag;
    tlv_reader_diagnostic_init(&diag);
    tlv_result_t code = tlv_read_diag(size ? (const uint8_t*)data : NULL, size, &format->format,
                                      &element, &consumed, &diag);
    if (code == TLV_END) {
        lua_pushnil(L);
        return 1;
    }
    if (code != TLV_OK) return opentlv_lua_raise_reader_error(L, code, &diag);
    int narrow_code = opentlv_lua_push_element(L, &element, 0);
    if (narrow_code != TLV_OK) return opentlv_lua_raise(L, (tlv_result_t)narrow_code, 1, 0);
    lua_pushinteger(L, (lua_Integer)consumed);
    return 2;
}

/* opentlv.read_source(data, format) -> element, source | nil */
static int l_read_source(lua_State* L) {
    size_t                  size;
    const char*             data = luaL_checklstring(L, 1, &size);
    tlv_lua_format_t*       format = push_format(L, 2);
    int                     format_index = lua_gettop(L);
    tlv_element_t           element;
    tlv_source_t            source;
    size_t                  consumed;
    tlv_reader_diagnostic_t diag;
    tlv_reader_diagnostic_init(&diag);
    tlv_result_t code = tlv_read_source_diag(size ? (const uint8_t*)data : NULL, size,
                                             &format->format, &element, &consumed, &source, &diag);
    if (code == TLV_END) {
        lua_pushnil(L);
        return 1;
    }
    if (code != TLV_OK) return opentlv_lua_raise_reader_error(L, code, &diag);
    int narrow_code = opentlv_lua_push_element(L, &element, 0);
    if (narrow_code != TLV_OK) return opentlv_lua_raise(L, (tlv_result_t)narrow_code, 1, 0);
    opentlv_lua_push_source(L, 1, format_index, &source, 0);
    return 2;
}

void opentlv_lua_open_reader(lua_State* L, int module_table_index) {
    static const opentlv_lua_method_t methods[] = {
        {"next", reader_method_next},    {"next_source", reader_next_source},
        {"at_end", reader_at_end},       {"position", reader_consumed},
        {"consumed", reader_consumed},   {"offset", reader_offset},
        {"set_input", reader_set_input}, {"visit", reader_visit},
        {"format", reader_format},       {NULL, NULL},
    };
    static const opentlv_lua_method_t tree_methods[] = {
        {"next", tree_next},
        {"next_event", tree_next_event},
        {"skip_subtree", tree_skip_subtree},
        {"at_end", tree_at_end},
        {"consumed", tree_consumed},
        {"offset", tree_offset},
        {"set_input", tree_set_input},
        {"visit", tree_visit},
        {"format", tree_format},
        {NULL, NULL},
    };
    opentlv_lua_open_source(L);
    opentlv_lua_new_type(L, OPENTLV_LUA_READER_MT, methods, reader_gc, NULL, reader_call);
    opentlv_lua_new_type(L, TREE_READER_MT, tree_methods, tree_gc, NULL, tree_call);

    lua_pushcfunction(L, l_reader_new);
    lua_setfield(L, module_table_index, "reader");
    lua_pushcfunction(L, l_tree_reader_new);
    lua_setfield(L, module_table_index, "tree_reader");
    lua_pushcfunction(L, l_read);
    lua_setfield(L, module_table_index, "read");
    lua_pushcfunction(L, l_read_source);
    lua_setfield(L, module_table_index, "read_source");
}
