// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#ifndef OPENTLV_LUA_CODEC_H
#define OPENTLV_LUA_CODEC_H

#include "compat.h"
#include <tlv/codec/codec.h>
#include <stdint.h>

/* Native representation adapters only: wire semantics belong to tlv/. Each
 * get() leaves borrowed Lua strings rooted on the stack until encoding ends. */
typedef struct opentlv_lua_codec_rep {
    size_t size;
    void (*push)(lua_State* L, const void* value);
    void (*get)(lua_State* L, int index, void* value);
} opentlv_lua_codec_rep_t;

void        opentlv_lua_open_codec(lua_State* L, int module_index);
void        opentlv_lua_codec_builtins(lua_State* L);
void        opentlv_lua_codec_push(lua_State* L, const tlv_codec_t* codec,
                                   const opentlv_lua_codec_rep_t* rep);
void        opentlv_lua_codec_add(lua_State* L, const char* name, const tlv_codec_t* codec,
                                  const opentlv_lua_codec_rep_t* rep);
int         opentlv_lua_codec_raise(lua_State* L, tlv_result_t code);
uint64_t    opentlv_lua_codec_uint(lua_State* L, int index, uint64_t maximum);
int64_t     opentlv_lua_codec_int(lua_State* L, int index, int64_t minimum, int64_t maximum);
void        opentlv_lua_codec_push_uint(lua_State* L, uint64_t value);
void        opentlv_lua_codec_push_int(lua_State* L, int64_t value);
const char* opentlv_lua_codec_string(lua_State* L, int index, size_t* size);

#endif
