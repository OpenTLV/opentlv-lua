// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#ifndef OPENTLV_LUA_FORMAT_H
#define OPENTLV_LUA_FORMAT_H

#include "compat.h"
#include <tlv/config.h>

#include <tlv/formats/fixed.h>
#include <tlv/format.h>

/* Metatable name shared by all built-in and configured formats. */
#define OPENTLV_LUA_FORMAT_MT "opentlv.Format"

/* Registry key under which opentlv.formats.ber is stashed, so
 * opentlv.reader() can fall back to it when its format argument is omitted
 * without having to reach back into the module table. */
#define OPENTLV_LUA_DEFAULT_FORMAT_KEY "opentlv.default_format"

/* A format bound to Lua: the callbacks used by every generic C entry point
 * (tlv_reader_next(), tlv_tree_reader_visit(), ...) -- shared by reading and
 * writing. format.is_constructed is the nesting predicate (NULL for flat
 * formats), and use_der_validation/name say
 * whether tree traversal should dispatch to the stricter tlv_der_visit()
 * instead of the generic tlv_tree_reader_visit(), and give a name for diagnostics.
 *
 * For the built-in presets format is a
 * copy of the corresponding extern const global; for opentlv.formats.fixed()
 * it is initialized by tlv_fixed_format_init() with `fixed_config` (embedded
 * in this same userdata, so its address stays valid for exactly as long as
 * the format object itself does) as its context. */
typedef struct tlv_lua_format {
    tlv_format_t       format;
    tlv_fixed_format_t fixed_config;
    int                use_der_validation;
    const char*        name;
} tlv_lua_format_t;

/* Registers the "opentlv.Format" metatable, builds module_table["formats"]
 * and stashes BER (or nil when disabled) under OPENTLV_LUA_DEFAULT_FORMAT_KEY.
 * module_table must be on top of the stack; the stack is unchanged on
 * return. Call once from luaopen_opentlv__core(). */
void opentlv_lua_open_format(lua_State* L, int module_table_index);

/* Checks that the value at `arg` is an opentlv.Format, raising a Lua
 * argument error otherwise. */
tlv_lua_format_t* opentlv_lua_check_format(lua_State* L, int arg);

/* Registration helpers expect the formats table on top of the stack and
 * leave the stack unchanged. Built-in descriptors and names must outlive Lua. */
void opentlv_lua_register_builtin(lua_State* L, tlv_format_t format_value, int use_der_validation,
                                  const char* name);
void opentlv_lua_register_fixed(lua_State* L);
#if OPENTLV_FORMAT_BER
void opentlv_lua_register_ber(lua_State* L);
#endif
#if OPENTLV_FORMAT_CER
void opentlv_lua_register_cer(lua_State* L);
#endif
#if OPENTLV_FORMAT_DER
void opentlv_lua_register_der(lua_State* L);
#endif
#if OPENTLV_BLUETOOTH
void opentlv_lua_register_bluetooth_ltv(lua_State* L);
#endif
#if OPENTLV_EMV
void opentlv_lua_register_emv(lua_State* L);
#endif

#if OPENTLV_NFC
void opentlv_lua_register_nfc_type2(lua_State* L);
#endif

#if OPENTLV_LLDP
void opentlv_lua_register_lldp(lua_State* L);
#endif

#endif /* OPENTLV_LUA_FORMAT_H */
