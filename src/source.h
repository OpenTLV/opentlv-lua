// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#ifndef OPENTLV_LUA_SOURCE_H
#define OPENTLV_LUA_SOURCE_H

#include "compat.h"

#include <tlv/format.h>

/* Registers the "opentlv.Source" metatable. Call once from
 * luaopen_opentlv__core(). */
void opentlv_lua_open_source(lua_State* L);

/* Pushes an opentlv.Source for one successful decode. `data_index` is the
 * absolute stack index of the Lua string `source->data` points into and
 * `format_index` that of the Format object whose descriptor decoded it; the
 * Source retains both. `offset` is the absolute logical offset of the element
 * start. Allocation errors are raised. */
void opentlv_lua_push_source(lua_State* L, int data_index, int format_index,
                             const tlv_source_t* source, size_t offset);

/* Checks that the value at `arg` is an opentlv.Source and returns its native
 * source, borrowed for as long as the Source object stays alive. */
const tlv_source_t* opentlv_lua_check_source(lua_State* L, int arg);

#endif /* OPENTLV_LUA_SOURCE_H */
