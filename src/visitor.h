// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#ifndef OPENTLV_LUA_VISITOR_H
#define OPENTLV_LUA_VISITOR_H

#include "compat.h"

/* Registers visit(data, format, callback), a sequential C Reader visitor,
 * and module_table["visit_tree"], the opentlv.visit_tree(data, format,
 * callback, opts) preorder tree traversal built on tlv_tree_reader_visit()/
 * tlv_der_visit(); `callback` may be omitted to validate structure and
 * limits only. module_table must be on top of the stack; the stack is
 * unchanged on return. Call once from luaopen_opentlv__core(). */
void opentlv_lua_open_visitor(lua_State* L, int module_table_index);

#endif /* OPENTLV_LUA_VISITOR_H */
