// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel
#ifndef OPENTLV_LUA_PROGRAM_H
#define OPENTLV_LUA_PROGRAM_H
#include "compat.h"
#include <tlv/query/program.h>
void opentlv_lua_open_program(lua_State* L, int module_index);
/* Pushes a Query error table: the common diagnostic, the Reader detail of a Reader
 * cause and a "query" table whose fields each come from one native member. */
void opentlv_lua_push_query_error(lua_State* L, tlv_result_t code, const tlv_query_diagnostic_t* d);
#endif
