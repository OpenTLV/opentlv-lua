// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

/* Portability shims for building against Lua 5.1 through 5.4 and LuaJIT
 * (which implements the Lua 5.1 C API) with the same source. Every other
 * shared Lua C API call (lua_newuserdata, luaL_checkudata,
 * luaL_ref/luaL_unref, lua_pcall, ...) is identical across these versions.
 * Codec adapters locally select version-specific integer, array-length and
 * userdata environment APIs. LUA_OK needs a fallback since Lua 5.1 predates it. */
#ifndef OPENTLV_LUA_COMPAT_H
#define OPENTLV_LUA_COMPAT_H

#include <lua.h>
#include <lauxlib.h>

#ifndef LUA_OK
#define LUA_OK 0
#endif

#endif /* OPENTLV_LUA_COMPAT_H */
