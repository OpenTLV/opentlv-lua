// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#ifndef OPENTLV_LUA_VISITOR_H
#define OPENTLV_LUA_VISITOR_H

#include "compat.h"
#include "format.h"

#include <tlv/query/query.h>
#include <tlv/reader/tree.h>

/* Element budget of tree traversal when the caller sets none. */
#define OPENTLV_LUA_DEFAULT_MAX_ELEMENTS ((size_t)65536)

/* Registers visit(data, format, callback), a sequential C Reader visitor,
 * and module_table["visit_tree"], the opentlv.visit_tree(data, format,
 * callback, opts) preorder tree traversal built on tlv_tree_reader_visit()/
 * tlv_der_visit(); `callback` may be omitted to validate structure and
 * limits only. module_table must be on top of the stack; the stack is
 * unchanged on return. Call once from luaopen_opentlv__core(). */
void opentlv_lua_open_visitor(lua_State* L, int module_table_index);

/* Resumes the Visitor of an initialized Reader (`tree` NULL) or Tree Reader
 * (`reader` NULL) with the function at `callback_index`, called as
 * callback(element) or callback(element, depth) like opentlv.visit() and
 * opentlv.visit_tree(). *busy is nonzero while native traversal runs, so the
 * cursor's methods can reject reentry from the callback. Returns
 * `visited, stopped`, or propagates a callback error unchanged and raises
 * traversal failures, including need-more-data, after releasing the callback. */
int opentlv_lua_visit_cursor(lua_State* L, tlv_lua_format_t* format, tlv_reader_t* reader,
                             tlv_tree_reader_t* tree, int callback_index, int* busy);

/* Visits Query matches through the same protected callback(element), where
 * element also has "depth": the resumable tlv_query_visit() over `tree` and
 * `matcher`, or, with `tree` NULL, tlv_query_visit_buffer() over complete
 * `data` with its own frames. Returns `visited, stopped` like
 * opentlv_lua_visit_cursor(). */
int opentlv_lua_visit_query(lua_State* L, tlv_lua_format_t* format, tlv_tree_reader_t* tree,
                            tlv_query_matcher_t* matcher, const uint8_t* data, size_t size,
                            const tlv_query_t* query, size_t max_depth, size_t max_elements,
                            int callback_index);

#endif /* OPENTLV_LUA_VISITOR_H */
