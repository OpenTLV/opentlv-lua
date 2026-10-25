// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#ifndef OPENTLV_LUA_READER_H
#define OPENTLV_LUA_READER_H

#include "compat.h"

/* Registers the "opentlv.Reader", "opentlv.TreeReader" and "opentlv.Source"
 * metatables and module_table["reader"], ["tree_reader"], ["read"] and
 * ["read_source"]: the opentlv.reader(data, format, options) and
 * opentlv.tree_reader(data, format, options) constructors and the one-element
 * opentlv.read(data, format) and opentlv.read_source(data, format).
 * module_table must be on top of the stack; the stack is unchanged on return.
 * Call once from luaopen_opentlv__core(), after opentlv_lua_open_format(). */
void opentlv_lua_open_reader(lua_State* L, int module_table_index);

#endif /* OPENTLV_LUA_READER_H */
