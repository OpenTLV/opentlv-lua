// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#ifndef OPENTLV_LUA_SCHEMA_H
#define OPENTLV_LUA_SCHEMA_H

#include "compat.h"
#include <tlv/schema/schema.h>

/* Copies every borrowed native Schema field into an owned Lua diagnostic. */
void opentlv_lua_push_schema_diagnostic(lua_State* L, const tlv_schema_diagnostic_t* detail);

/* Reads the length rule fields of the table at `rule` into `entry`, retaining
 * its tag and name strings in the table at `references`. */
void opentlv_lua_schema_entry(lua_State* L, int rule, int references, tlv_schema_entry_t* entry);

/* Returns the structural rules of the Schema or checked Schema at `index`,
 * which stay valid while that value is rooted. */
const tlv_structure_schema_t* opentlv_lua_check_structure_schema(lua_State* L, int index);

/* Registers the Schema userdata and module.schema constructor. */
void opentlv_lua_open_schema(lua_State* L, int module_table_index);

#endif /* OPENTLV_LUA_SCHEMA_H */
