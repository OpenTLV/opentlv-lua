// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#ifndef OPENTLV_LUA_DEFINITION_H
#define OPENTLV_LUA_DEFINITION_H

#include "compat.h"

/* Registers the DefinitionRegistry userdata and module.definition_registry;
 * the stack is unchanged on return. */
void opentlv_lua_open_definition(lua_State* L, int module_table_index);

#endif /* OPENTLV_LUA_DEFINITION_H */
