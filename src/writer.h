// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#ifndef OPENTLV_LUA_WRITER_H
#define OPENTLV_LUA_WRITER_H

#include "compat.h"

/* Registers the sequential and tree writer constructors on the module. */
void opentlv_lua_open_writer(lua_State* L, int module_table_index);

#endif
