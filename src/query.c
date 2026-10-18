// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "query.h"
#include "common.h"
#include "error.h"
#include "format.h"
#include "program.h"
#include "../../common/path_query.h"

#include <string.h>
#include <limits.h>

#define QUERY_MT "opentlv.Query"
#define MATCHER_MT "opentlv.QueryMatcher"
typedef struct {
    tlv_query_t         query;
    tlv_query_matcher_t matcher;
    tlv_tree_reader_t   reader;
    int                 format_ref, input_ref, busy, mode;
    size_t              depth, elements, capacity;
    tlv_tree_frame_t    frames[];
} query_matcher;

static int matcher_gc(lua_State* L) {
    query_matcher* q = luaL_checkudata(L, 1, MATCHER_MT);
    luaL_unref(L, LUA_REGISTRYINDEX, q->format_ref);
    luaL_unref(L, LUA_REGISTRYINDEX, q->input_ref);
    q->format_ref = q->input_ref = LUA_NOREF;
    return 0;
}
static int matcher_initialize(lua_State* L, query_matcher* q) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, q->format_ref);
    tlv_lua_format_t* format = opentlv_lua_check_format(L, -1);
    tlv_result_t      rc = tlv_tree_reader_init_incremental(
        &q->reader, NULL, 0, &format->format, q->frames, q->capacity, q->depth, q->elements);
    lua_pop(L, 1);
    if (rc == TLV_OK) rc = tlv_query_matcher_init(&q->matcher, &q->query);
    if (rc != TLV_OK) return opentlv_lua_raise(L, rc, 0, 0);
    luaL_unref(L, LUA_REGISTRYINDEX, q->input_ref);
    q->input_ref = LUA_NOREF;
    q->mode = 0;
    return 0;
}
static int query_matcher_new(lua_State* L) {
    const tlv_query_t* query = luaL_checkudata(L, 1, QUERY_MT);
    lua_settop(L, 3);
    if (lua_isnil(L, 2)) {
        lua_getfield(L, LUA_REGISTRYINDEX, OPENTLV_LUA_DEFAULT_FORMAT_KEY);
        lua_replace(L, 2);
    }
    opentlv_lua_check_format(L, 2);
    size_t depth = opentlv_lua_query_limit(L, 3, "max_depth", TLV_TREE_DEFAULT_DEPTH);
    size_t capacity = opentlv_lua_query_limit(L, 3, "frame_capacity", depth);
    size_t elements = opentlv_lua_query_limit(L, 3, "max_elements", 65536);
    if (capacity > (SIZE_MAX - sizeof(query_matcher)) / sizeof(tlv_tree_frame_t))
        return opentlv_lua_raise(L, TLV_ERR_OVERFLOW, 0, 0);
    query_matcher* q = lua_newuserdata(L, sizeof(*q) + capacity * sizeof(tlv_tree_frame_t));
    memset(q, 0, sizeof *q);
    q->format_ref = q->input_ref = LUA_NOREF;
    q->query = *query;
    q->depth = depth;
    q->capacity = capacity;
    q->elements = elements;
    luaL_getmetatable(L, MATCHER_MT);
    lua_setmetatable(L, -2);
    lua_pushvalue(L, 2);
    q->format_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    matcher_initialize(L, q);
    return 1;
}
/* Projection, replacement and user callbacks all run under one protected
 * scope: allocation errors also release the reentry guard. */
static int matcher_call(lua_State* L, lua_CFunction function) {
    query_matcher* q = luaL_checkudata(L, 1, MATCHER_MT);
    if (q->busy) return luaL_error(L, "Query matcher is active in a callback");
    int nargs = lua_gettop(L);
    if (!lua_checkstack(L, 2)) return luaL_error(L, "Query matcher stack exhausted");
    lua_pushcfunction(L, function);
    lua_insert(L, 1);
    q->busy = 1;
    int status = lua_pcall(L, nargs, LUA_MULTRET, 0);
    q->busy = 0;
    if (status != LUA_OK) return lua_error(L);
    return lua_gettop(L);
}
static int matcher_reset_run(lua_State* L) {
    return matcher_initialize(L, luaL_checkudata(L, 1, MATCHER_MT));
}
static int matcher_reset(lua_State* L) {
    return matcher_call(L, matcher_reset_run);
}
static int matcher_rebind_run(lua_State* L) {
    query_matcher*     q = luaL_checkudata(L, 1, MATCHER_MT);
    tlv_query_t        scratch;
    const tlv_query_t* query = opentlv_lua_check_query(L, 2, &scratch);
    tlv_result_t       rc = tlv_query_matcher_rebind(&q->matcher, query);
    if (rc != TLV_OK) return opentlv_lua_raise(L, rc, 0, 0);
    q->query = *query;
    rc = tlv_query_matcher_rebind(&q->matcher, &q->query);
    if (rc != TLV_OK) return opentlv_lua_raise(L, rc, 0, 0);
    return 0;
}
static int matcher_rebind(lua_State* L) {
    return matcher_call(L, matcher_rebind_run);
}
static int matcher_matches_run(lua_State* L) {
    query_matcher* q = luaL_checkudata(L, 1, MATCHER_MT);
    if (q->mode == 2) return luaL_error(L, "reset before switching from Reader to manual feed");
    size_t      size;
    const char* bytes = luaL_checklstring(L, 2, &size);
    luaL_checktype(L, 3, LUA_TNUMBER);
    lua_Integer depth = luaL_checkinteger(L, 3);
    if (depth < 0 || (uintmax_t)depth > SIZE_MAX || (lua_Number)depth != lua_tonumber(L, 3))
        return luaL_error(L, "nonnegative depth required");
    tlv_tag_t tag = tlv_tag((const uint8_t*)bytes, size);
    q->mode = 1;
    lua_pushboolean(L, tlv_query_matcher_visit(&q->matcher, &tag, (size_t)depth));
    return 1;
}
static int matcher_matches(lua_State* L) {
    return matcher_call(L, matcher_matches_run);
}
static int matcher_input_run(lua_State* L) {
    query_matcher* q = luaL_checkudata(L, 1, MATCHER_MT);
    if (q->mode == 1) return luaL_error(L, "reset before switching from manual feed to Reader");
    size_t      size;
    const char* bytes = luaL_checklstring(L, 2, &size);
    lua_Integer discard = luaL_optinteger(L, 3, 0);
    if (!lua_isnoneornil(L, 3)) luaL_checktype(L, 3, LUA_TNUMBER);
    if (discard < 0 || (uintmax_t)discard > SIZE_MAX ||
        (!lua_isnoneornil(L, 3) && (lua_Number)discard != lua_tonumber(L, 3)))
        return luaL_error(L, "nonnegative discard required");
    lua_pushvalue(L, 2);
    int          ref = luaL_ref(L, LUA_REGISTRYINDEX);
    tlv_result_t rc =
        tlv_tree_reader_set_input(&q->reader, (const uint8_t*)bytes, size, (size_t)discard,
                                  lua_isnoneornil(L, 4) || lua_toboolean(L, 4));
    if (rc != TLV_OK) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return opentlv_lua_raise(L, rc, 0, 0);
    }
    luaL_unref(L, LUA_REGISTRYINDEX, q->input_ref);
    q->input_ref = ref;
    q->mode = 2;
    return 0;
}
static int matcher_input(lua_State* L) {
    return matcher_call(L, matcher_input_run);
}
static int matcher_pull(lua_State* L, int visit) {
    query_matcher* q = luaL_checkudata(L, 1, MATCHER_MT);
    if (q->mode == 1) return luaL_error(L, "reset before switching from manual feed to Reader");
    if (visit) luaL_checktype(L, 2, LUA_TFUNCTION);
    q->mode = 2;
    for (;;) {
        tlv_tree_item_t         item;
        tlv_reader_diagnostic_t diagnostic;
        tlv_reader_diagnostic_init(&diagnostic);
        tlv_result_t rc = tlv_tree_reader_next_diag(&q->reader, &item, &diagnostic);
        if (rc == TLV_END) {
            lua_pushnil(L);
            return visit ? 0 : 1;
        }
        if (rc != TLV_OK) return opentlv_lua_raise_reader_error(L, rc, &diagnostic);
        if (!tlv_query_matcher_visit(&q->matcher, &item.element.tag, item.depth)) continue;
        if (visit) lua_pushvalue(L, 2);
        rc = (tlv_result_t)opentlv_lua_push_element(L, &item.element, item.offset);
        if (rc != TLV_OK) return opentlv_lua_raise(L, rc, 1, item.offset);
        lua_pushinteger(L, (lua_Integer)item.depth);
        lua_setfield(L, -2, "depth");
        lua_pushboolean(L, item.constructed);
        lua_setfield(L, -2, "constructed");
        if (!visit) return 1;
        lua_call(L, 1, 1);
        int stop = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
        lua_pop(L, 1);
        if (stop) return 0;
    }
}
static int matcher_next_run(lua_State* L) {
    return matcher_pull(L, 0);
}
static int matcher_visit_run(lua_State* L) {
    return matcher_pull(L, 1);
}
static int matcher_next(lua_State* L) {
    return matcher_call(L, matcher_next_run);
}
static int matcher_visit(lua_State* L) {
    return matcher_call(L, matcher_visit_run);
}

const tlv_query_t* opentlv_lua_check_query(lua_State* L, int arg, tlv_query_t* scratch) {
    if (lua_type(L, arg) == LUA_TUSERDATA)
        return (const tlv_query_t*)luaL_checkudata(L, arg, QUERY_MT);
    size_t           length;
    const char*      text = luaL_checklstring(L, arg, &length);
    tlv_diagnostic_t parse = {0};
    tlv_result_t     code = tlv_query_parse_n(text, length, scratch, &parse);
    if (code != TLV_OK) {
        const tlv_query_diagnostic_t failure =
            opentlv_binding_path_failure(code, &parse, text, length);
        opentlv_lua_push_query_error(L, code, &failure);
        lua_error(L);
    }
    return scratch;
}

size_t opentlv_lua_query_limit(lua_State* L, int arg, const char* name, size_t fallback) {
    if (lua_isnoneornil(L, arg)) return fallback;
    luaL_checktype(L, arg, LUA_TTABLE);
    lua_getfield(L, arg, name);
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
    if (!(value >= 0 && value < (lua_Number)SIZE_MAX))
        luaL_error(L, "%s must be a nonnegative native integer", name);
    size_t result = (size_t)value;
    if ((lua_Number)result != value) luaL_error(L, "%s must be an integer", name);
    lua_pop(L, 1);
    return result;
}

static int query_new(lua_State* L) {
    tlv_query_t        scratch;
    const tlv_query_t* query = opentlv_lua_check_query(L, 1, &scratch);
    tlv_query_t*       self = (tlv_query_t*)lua_newuserdata(L, sizeof(*self));
    *self = *query;
    luaL_getmetatable(L, QUERY_MT);
    lua_setmetatable(L, -2);
    return 1;
}

static int query_steps(lua_State* L) {
    const tlv_query_t* self = (const tlv_query_t*)luaL_checkudata(L, 1, QUERY_MT);
    lua_newtable(L);
    size_t count = tlv_query_count(self);
    for (size_t i = 0; i < count; ++i) {
        tlv_tag_t tag = tlv_query_step(self, i);
        lua_pushlstring(L, (const char*)tag.data, tag.size);
        lua_rawseti(L, -2, (int)i + 1);
    }
    return 1;
}

/* Pull between Lua allocations: no Lua error can unwind a native callback.
 * Frames are Lua-owned scratch, so even allocation failures release them. */
static int query_evaluate(lua_State* L) {
    const tlv_query_t* self = (const tlv_query_t*)luaL_checkudata(L, 1, QUERY_MT);
    size_t             length;
    const uint8_t*     data = (const uint8_t*)luaL_checklstring(L, 2, &length);
    lua_settop(L, 4);
    if (lua_isnil(L, 3)) {
        lua_getfield(L, LUA_REGISTRYINDEX, OPENTLV_LUA_DEFAULT_FORMAT_KEY);
        lua_replace(L, 3);
    }
    tlv_lua_format_t* format = opentlv_lua_check_format(L, 3);
    size_t            depth = opentlv_lua_query_limit(L, 4, "max_depth", TLV_TREE_DEFAULT_DEPTH);
    size_t            elements = opentlv_lua_query_limit(L, 4, "max_elements", 65536);
    size_t            capacity = depth < length ? depth : length;
    if (capacity > SIZE_MAX / sizeof(tlv_tree_frame_t))
        return opentlv_lua_raise(L, TLV_ERR_OVERFLOW, 0, 0);
    tlv_tree_frame_t*   frames = (tlv_tree_frame_t*)lua_newuserdata(L, capacity * sizeof(*frames));
    tlv_tree_reader_t   reader;
    tlv_query_matcher_t matcher;
    tlv_result_t        code = tlv_query_matcher_init(&matcher, self);
    if (code == TLV_OK)
        code = tlv_tree_reader_init(&reader, data, length, &format->format, frames, capacity, depth,
                                    elements);
    if (code != TLV_OK) return opentlv_lua_raise(L, code, 0, 0);
    lua_newtable(L);
    int count = 0;
    for (;;) {
        tlv_tree_item_t         item;
        tlv_reader_diagnostic_t diagnostic;
        tlv_reader_diagnostic_init(&diagnostic);
        code = tlv_tree_reader_next_diag(&reader, &item, &diagnostic);
        if (code == TLV_END) break;
        if (code != TLV_OK) return opentlv_lua_raise_reader_error(L, code, &diagnostic);
        if (!tlv_query_matcher_visit(&matcher, &item.element.tag, item.depth)) continue;
        if (count == INT_MAX) return opentlv_lua_raise(L, TLV_ERR_LIMIT, 0, 0);
        code = (tlv_result_t)opentlv_lua_push_element(L, &item.element, item.offset);
        if (code != TLV_OK) return opentlv_lua_raise(L, code, 1, item.offset);
        lua_pushinteger(L, (lua_Integer)item.depth);
        lua_setfield(L, -2, "depth");
        lua_pushboolean(L, item.constructed);
        lua_setfield(L, -2, "constructed");
        lua_rawseti(L, -2, ++count);
    }
    return 1;
}

void opentlv_lua_open_query(lua_State* L, int module_index) {
    static const opentlv_lua_method_t methods[] = {{"steps", query_steps},
                                                   {"evaluate", query_evaluate},
                                                   {"matcher", query_matcher_new},
                                                   {NULL, NULL}};
    static const opentlv_lua_method_t matcher_methods[] = {{"matches", matcher_matches},
                                                           {"reset", matcher_reset},
                                                           {"rebind", matcher_rebind},
                                                           {"set_input", matcher_input},
                                                           {"next", matcher_next},
                                                           {"visit", matcher_visit},
                                                           {NULL, NULL}};
    opentlv_lua_new_type(L, MATCHER_MT, matcher_methods, matcher_gc, NULL, NULL);
    opentlv_lua_new_type(L, QUERY_MT, methods, NULL, NULL, NULL);
    lua_pushcfunction(L, query_new);
    lua_setfield(L, module_index, "query");
}
