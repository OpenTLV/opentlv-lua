// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#ifndef OPENTLV_LUA_GENERATOR_H
#define OPENTLV_LUA_GENERATOR_H

#include "compat.h"

/* Registers the Generator userdata, module.generator and
 * module.GENERATOR_VERSION; the stack is unchanged on return. */
void opentlv_lua_open_generator(lua_State* L, int module_table_index);

#endif /* OPENTLV_LUA_GENERATOR_H */
