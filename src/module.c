// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

/*
 * luaopen_opentlv__core(): the entry point require("opentlv._core") loads.
 * Ties together the format (format.c), reader (reader.c), writer (writer.c), traversal
 * (visitor.c), error (error.c) and shared (common.c) pieces into the module
 * table Lua sees; the OpenTLV C reader itself (tlv/reader/) does the actual
 * parsing, exactly as for every other OpenTLV binding (see
 * docs/concepts/bindings.md).
 *
 * This is the native module, not the one callers require: `require("opentlv")`
 * resolves to lua/opentlv/init.lua, a one-line pure-Lua file that returns
 * this module unchanged, the same native/pure split Python's
 * opentlv-core/opentlv and Rust's opentlv-sys/opentlv already use.
 *
 * Rule for what belongs here versus in tlv/: functionality that would be
 * useful outside Lua belongs in the OpenTLV C API, not in this binding.
 * This module (and every other file under bindings/lua/src/) only adapts
 * the existing public C API to Lua's calling conventions, ownership model
 * and error handling; it does not implement TLV parsing, encoding or
 * validation logic of its own. When a Lua-binding change would require new
 * decoding/encoding behavior, that behavior is added to tlv/ first, and this
 * binding calls it, the same way format.c, reader.c and visitor.c call
 * tlv_reader_init(), tlv_reader_next_diag() and tlv_tree_reader_visit() rather than
 * re-implementing any part of what those functions do.
 */
#include "common.h"
#include "codec.h"
#include "compat.h"
#include "error.h"
#include "format.h"
#include "query.h"
#include "program.h"
#include "document.h"
#include "reader.h"
#include "schema.h"
#include "visitor.h"
#include "writer.h"

#include <tlv/result.h>
#include <tlv/version.h>

static int l_version(lua_State* L) {
    lua_pushstring(L, tlv_version_string());
    return 1;
}

static int l_strerror(lua_State* L) {
    lua_Integer code = luaL_checkinteger(L, 1);
    lua_pushstring(L, tlv_result_string((tlv_result_t)code));
    return 1;
}

int luaopen_opentlv__core(lua_State* L) {
    lua_newtable(L);
    int module_index = lua_gettop(L);

    opentlv_lua_open_error(L);
    opentlv_lua_register_error_codes(L, module_index);
    opentlv_lua_open_codec(L, module_index);
    opentlv_lua_open_format(L, module_index);
    opentlv_lua_open_reader(L, module_index);
    opentlv_lua_open_schema(L, module_index);
    opentlv_lua_open_writer(L, module_index);
    opentlv_lua_open_visitor(L, module_index);
    opentlv_lua_open_query(L, module_index);
    opentlv_lua_open_program(L, module_index);
#if OPENTLV_DOCUMENT
    opentlv_lua_open_document(L, module_index);
#endif

    lua_pushcfunction(L, l_version);
    lua_setfield(L, module_index, "version");
    lua_pushcfunction(L, l_strerror);
    lua_setfield(L, module_index, "strerror");

    /* Reports the linked C library's version, independent of this binding's
     * own rockspec version, the same distinction docs/development/python.md
     * documents for opentlv.__version__. */
    lua_pushstring(L, tlv_version_string());
    lua_setfield(L, module_index, "_VERSION");

    return 1;
}
