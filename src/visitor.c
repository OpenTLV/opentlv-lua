// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include <stdlib.h>
#include "visitor.h"
#include "common.h"
#include "error.h"
#include "format.h"

#if OPENTLV_FORMAT_DER
#include <tlv/builtins/asn1/der_validation.h>
#endif
#include <tlv/reader/visitor.h>

#define OPENTLV_LUA_DEFAULT_MAX_ELEMENTS ((size_t)65536)

/* Synchronous state: neither Lua nor the core retains this pointer. Input and
 * Format remain rooted in the outer call's arguments throughout traversal. */
typedef struct {
    lua_State*           L;
    tlv_lua_format_t*    format;
    tlv_reader_t*        reader;
    int                  callback_ref;
    int                  invoke_index;
    int                  error_index;
    int                  failed;
    int                  tree;
    int                  stopped;
    size_t               visited;
    const tlv_element_t* element;
    size_t               depth;
    size_t               offset;
} visitor_ctx_t;

/* Conversion and callback execution both run inside pcall, so allocation
 * errors cannot unwind native traversal frames or skip resource cleanup. */
static int invoke_callback(lua_State* L) {
    visitor_ctx_t* ctx = (visitor_ctx_t*)lua_touserdata(L, 1);
    lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->callback_ref);
    int code = opentlv_lua_push_element(L, ctx->element, ctx->offset);
    if (code != TLV_OK) return opentlv_lua_raise(L, (tlv_result_t)code, 1, ctx->offset);
    lua_pushboolean(
        L, ctx->format->format.is_constructed != NULL &&
               ctx->format->format.is_constructed(ctx->format->format.context, &ctx->element->tag));
    lua_setfield(L, -2, "constructed");
    if (ctx->tree) lua_pushinteger(L, (lua_Integer)ctx->depth);
    lua_call(L, ctx->tree ? 2 : 1, 1);
    return 1;
}

static tlv_visit_result_t visitor_trampoline(const tlv_element_t* element, size_t depth,
                                             size_t offset, void* context) {
    visitor_ctx_t* ctx = (visitor_ctx_t*)context;
    lua_State*     L = ctx->L;
    ctx->element = element;
    ctx->depth = depth;
    ctx->offset = offset;
    /* Function and stack capacity are prepared before acquiring resources.
     * Preserve errors without luaL_ref, which could itself allocate. */
    lua_pushvalue(L, ctx->invoke_index);
    lua_pushlightuserdata(L, ctx);
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        lua_replace(L, ctx->error_index);
        ctx->failed = 1;
        return TLV_VISIT_ERROR;
    }
    ctx->visited++;
    ctx->stopped = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
    lua_pop(L, 1);
    return ctx->stopped ? TLV_VISIT_STOP : TLV_VISIT_CONTINUE;
}

static tlv_visit_result_t sequential_trampoline(const tlv_element_t* element, void* context) {
    visitor_ctx_t*     ctx = (visitor_ctx_t*)context;
    tlv_visit_result_t result = visitor_trampoline(element, 0, ctx->offset, context);
    /* Reader consumed the complete wire element, including any trailer. */
    ctx->offset = ctx->reader->pos;
    return result;
}

static size_t limit_option(lua_State* L, const char* name, size_t fallback) {
    if (lua_isnoneornil(L, 4)) return fallback;
    lua_getfield(L, 4, name);
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

static int visit(lua_State* L, int tree) {
    size_t            data_len;
    const char*       data = luaL_checklstring(L, 1, &data_len);
    tlv_lua_format_t* format = opentlv_lua_check_format(L, 2);
    int               has_callback = !lua_isnoneornil(L, 3);
    if (has_callback || !tree) luaL_checktype(L, 3, LUA_TFUNCTION);
    /* Normalize optional arguments before adding internal stack slots. */
    lua_settop(L, tree ? 4 : 3);
    if (tree && !lua_isnil(L, 4)) luaL_checktype(L, 4, LUA_TTABLE);
    size_t max_depth = TLV_TREE_DEFAULT_DEPTH;
    size_t max_elements = OPENTLV_LUA_DEFAULT_MAX_ELEMENTS;
#if OPENTLV_FORMAT_DER
    if (format->use_der_validation) {
        max_depth = tlv_der_default_limits.max_depth;
        max_elements = tlv_der_default_limits.max_elements;
    }
#endif
    if (tree) {
        max_depth = limit_option(L, "max_depth", max_depth);
        max_elements = limit_option(L, "max_elements", max_elements);
    }
    visitor_ctx_t ctx = {0};
    ctx.L = L;
    ctx.format = format;
    ctx.tree = tree;
    ctx.callback_ref = LUA_NOREF;
    luaL_checkstack(L, 4, "visitor stack");
    lua_pushcfunction(L, invoke_callback);
    ctx.invoke_index = lua_gettop(L);
    lua_pushnil(L);
    ctx.error_index = lua_gettop(L);
    if (has_callback) {
        lua_pushvalue(L, 3);
        ctx.callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    tlv_result_t            code;
    tlv_reader_diagnostic_t tree_diagnostic = {0};
    tlv_reader_diagnostic_t diagnostic;
    tlv_reader_diagnostic_init(&diagnostic);
    if (!tree) {
        tlv_reader_t reader;
        ctx.reader = &reader;
        code = tlv_reader_init(&reader, (const uint8_t*)data, data_len, &format->format);
        if (code == TLV_OK)
            code = tlv_reader_visit_diag(&reader, sequential_trampoline, &ctx, &diagnostic);
    }
#if OPENTLV_FORMAT_DER
    else if (format->use_der_validation) {
        tlv_der_limits_t limits = tlv_der_default_limits;
        limits.max_depth = max_depth;
        limits.max_elements = max_elements;
        code = tlv_der_visit((const uint8_t*)data, data_len, &limits,
                             has_callback ? visitor_trampoline : NULL, &ctx,
                             &tree_diagnostic.diagnostic);
    }
#endif
    else {
        tlv_tree_reader_t reader;
        size_t            capacity = max_depth < data_len ? max_depth : data_len;
        tlv_tree_frame_t* frames = NULL;
        if (capacity > SIZE_MAX / sizeof(*frames)) {
            code = TLV_ERR_OVERFLOW;
        } else {
            if (capacity) frames = (tlv_tree_frame_t*)malloc(capacity * sizeof(*frames));
            code = capacity && !frames ? TLV_ERR_OUT_OF_MEMORY
                                       : tlv_tree_reader_init(&reader, (const uint8_t*)data,
                                                              data_len, &format->format, frames,
                                                              capacity, max_depth, max_elements);
            if (code == TLV_OK)
                code = tlv_tree_reader_visit(&reader, has_callback ? visitor_trampoline : NULL,
                                             &ctx, &tree_diagnostic);
            free(frames);
        }
    }

    luaL_unref(L, LUA_REGISTRYINDEX, ctx.callback_ref);
    if (ctx.failed) {
        lua_pushvalue(L, ctx.error_index);
        return lua_error(L);
    }
    if (code != TLV_OK) {
        if (!tree) return opentlv_lua_raise_reader_error(L, code, &diagnostic);
        return opentlv_lua_raise_reader_error(L, code, &tree_diagnostic);
    }
    lua_pushinteger(L, (lua_Integer)ctx.visited);
    lua_pushboolean(L, ctx.stopped);
    return 2;
}

static int l_visit(lua_State* L) {
    return visit(L, 0);
}
static int l_visit_tree(lua_State* L) {
    return visit(L, 1);
}

void opentlv_lua_open_visitor(lua_State* L, int module_table_index) {
    lua_pushcfunction(L, l_visit);
    lua_setfield(L, module_table_index, "visit");
    lua_pushcfunction(L, l_visit_tree);
    lua_setfield(L, module_table_index, "visit_tree");
}
