// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#ifndef OPENTLV_LUA_ERROR_H
#define OPENTLV_LUA_ERROR_H

#include "compat.h"

#include <tlv/result.h>
#include <tlv/codec/diagnostic.h>
#include <tlv/reader/reader.h>
#include <tlv/writer/writer.h>

/* Metatable name for every error table this binding raises. */
#define OPENTLV_LUA_ERROR_MT "opentlv.Error"

/* Registers the "opentlv.Error" metatable (__tostring only). Call once from
 * luaopen_opentlv__core(). */
void opentlv_lua_open_error(lua_State* L);

/* Sets module_table["errors"] = { OK = 0, BUFFER_TOO_SHORT = 1, ... }, one
 * entry per tlv_result_t value. module_table must be on top of the stack;
 * the stack is unchanged on return. */
void opentlv_lua_register_error_codes(lua_State* L, int module_table_index);

/* Pushes a table describing `code`, with the "opentlv.Error" metatable set:
 * always "code", "message" (tlv_result_string(code)) and "location"; when
 * has_offset, `offset` is an input point reported as "offset" and in
 * "location". Does not raise; the caller decides whether to return it,
 * stash it (see visitor.c), or raise it with lua_error(). */
void opentlv_lua_push_error(lua_State* L, tlv_result_t code, int has_offset, size_t offset);

/* Pushes a location table: "domain" and "kind", plus "begin" and "end" when known. */
void opentlv_lua_push_location(lua_State* L, const tlv_location_t* location);

/* Copies the common native diagnostic, including its complete location,
 * optional path and contexts, into an owned error table with the
 * "opentlv.Error" metatable. Does not raise. */
void opentlv_lua_push_diagnostic(lua_State* L, const tlv_diagnostic_t* diagnostic);

/* Like opentlv_lua_push_error(), additionally filling "expected", "actual",
 * "operation" and "tag" from a reader diagnostic when it reports them.
 * `diag` may be NULL, equivalent to opentlv_lua_push_error() with has_offset
 * false. */
void opentlv_lua_add_reader_detail(lua_State* L, const tlv_reader_detail_t* detail);
void opentlv_lua_push_reader_error(lua_State* L, tlv_result_t code,
                                   const tlv_reader_diagnostic_t* diag);

/* Raises the table built by opentlv_lua_push_error()/opentlv_lua_push_reader_error()
 * via lua_error(). Safe to call directly from any function Lua calls
 * directly; never call these from inside a callback invoked by a C library
 * function such as tlv_tree_reader_visit()/tlv_der_visit() (see visitor.c). Both never
 * return; the int result mirrors lua_error()'s so callers can write
 * `return opentlv_lua_raise(...)`. */
int opentlv_lua_raise(lua_State* L, tlv_result_t code, int has_offset, size_t offset);
int opentlv_lua_raise_reader_error(lua_State* L, tlv_result_t code,
                                   const tlv_reader_diagnostic_t* diag);

/* Copies borrowed writer diagnostic fields into the raised Lua error table. */
int opentlv_lua_raise_writer_error(lua_State* L, tlv_result_t code,
                                   const tlv_writer_diagnostic_t* diag);

/* Push owned conversion detail without duplicating the enclosing common diagnostic. */
void opentlv_lua_push_codec_detail(lua_State* L, const tlv_codec_detail_t* detail);

#endif /* OPENTLV_LUA_ERROR_H */
