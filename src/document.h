// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#ifndef OPENTLV_LUA_DOCUMENT_H
#define OPENTLV_LUA_DOCUMENT_H

#include "compat.h"
#include <tlv/config.h>

#if OPENTLV_DOCUMENT
#include <tlv/document/document.h>
void opentlv_lua_open_document(lua_State* L, int module_index);
/* Checked private bridges for compiled Query; never bypass handle validity. */
tlv_document_t* opentlv_lua_document_native(lua_State* L, int index);
void            opentlv_lua_document_invalidate(lua_State* L, int index);
void            opentlv_lua_document_query_guard(lua_State* L, int index, int begin);
tlv_node_t*     opentlv_lua_node_native(lua_State* L, int index, int document_index);
int             opentlv_lua_document_push_node(lua_State* L, int document_index, tlv_node_t* node);
#endif

#endif
