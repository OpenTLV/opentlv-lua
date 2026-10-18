// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#ifndef OPENTLV_LUA_QUERY_H
#define OPENTLV_LUA_QUERY_H

#include "compat.h"
#include <tlv/query/query.h>

/* Accept a compiled Query or parse a path into caller-owned scratch storage. */
const tlv_query_t* opentlv_lua_check_query(lua_State* L, int arg, tlv_query_t* scratch);
/* Read a nonnegative native size from an optional options table. */
size_t opentlv_lua_query_limit(lua_State* L, int arg, const char* name, size_t fallback);
void   opentlv_lua_open_query(lua_State* L, int module_index);

#endif
