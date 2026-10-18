// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#ifndef OPENTLV_LUA_SCHEMA_H
#define OPENTLV_LUA_SCHEMA_H

#include "compat.h"
#include <tlv/schema/schema.h>

/* Copies every borrowed native Schema field into an owned Lua diagnostic. */
void opentlv_lua_push_schema_diagnostic(lua_State* L, const tlv_schema_diagnostic_t* detail);

/* Registers the Schema userdata and module.schema constructor. */
void opentlv_lua_open_schema(lua_State* L, int module_table_index);

#endif /* OPENTLV_LUA_SCHEMA_H */
