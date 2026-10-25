// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "generator.h"
#include "codec.h"
#include "common.h"
#include "error.h"
#include "format.h"

#include <tlv/generator.h>
#include <limits.h>
#include <string.h>

#define GENERATOR_MT "opentlv.Generator"

/* Configuration snapshot; each call sets its own case index. The userdata
 * environment roots the Format, the candidate table and every candidate Tag
 * string. Core validates the configuration and the Format on every call. */
typedef struct lua_generator {
    tlv_generator_options_t options;
    const tlv_format_t*     format;
} lua_generator_t;

static size_t required_size(lua_State* L, int table, const char* name) {
    lua_getfield(L, table, name);
    if (lua_isnil(L, -1)) luaL_error(L, "%s is required", name);
    size_t value = opentlv_lua_check_size(L, -1, name);
    lua_pop(L, 1);
    return value;
}

/* opentlv.generator{format, candidates, seed, max_elements, max_depth,
 * max_value_size, max_case_size} */
static int generator_new(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 1);
    lua_generator_t* self = lua_newuserdata(L, sizeof(*self));
    memset(self, 0, sizeof(*self));
    luaL_getmetatable(L, GENERATOR_MT);
    lua_setmetatable(L, -2);
    lua_newtable(L);
    lua_getfield(L, 1, "format");
    if (lua_isnil(L, -1)) return luaL_error(L, "format is required");
    self->format = &opentlv_lua_check_format(L, -1)->format;
    lua_setfield(L, 3, "format");
    lua_getfield(L, 1, "seed");
    if (!lua_isnil(L, -1)) self->options.seed = opentlv_lua_codec_uint(L, 4, UINT64_MAX);
    lua_pop(L, 1);
    self->options.max_elements = required_size(L, 1, "max_elements");
    self->options.max_depth = required_size(L, 1, "max_depth");
    self->options.max_value_size = required_size(L, 1, "max_value_size");
    self->options.max_case_size = required_size(L, 1, "max_case_size");

    lua_getfield(L, 1, "candidates");
    if (!lua_istable(L, 4)) return luaL_error(L, "candidates must be an array of tables");
#if LUA_VERSION_NUM >= 502
    size_t count = lua_rawlen(L, 4);
#else
    size_t count = lua_objlen(L, 4);
#endif
    if (count > INT_MAX || count > SIZE_MAX / sizeof(tlv_generator_candidate_t))
        return luaL_error(L, "candidate table is too large");
    tlv_generator_candidate_t* candidates = NULL;
    if (count) {
        candidates = lua_newuserdata(L, count * sizeof(*candidates));
        memset(candidates, 0, count * sizeof(*candidates));
        lua_setfield(L, 3, "candidates");
    }
    for (size_t i = 0; i < count; ++i) {
        lua_rawgeti(L, 4, (int)i + 1);
        if (!lua_istable(L, 5)) return luaL_error(L, "candidate %d must be a table", (int)i + 1);
        /* nil is an absent identifier and "" an explicitly empty one. */
        lua_getfield(L, 5, "tag");
        if (lua_isnil(L, -1))
            lua_pop(L, 1);
        else {
            if (lua_type(L, -1) != LUA_TSTRING)
                return luaL_error(L, "candidate %d tag must be a string or nil", (int)i + 1);
            size_t      size;
            const char* tag = lua_tolstring(L, -1, &size);
            candidates[i].tag = tlv_tag((const uint8_t*)tag, size);
            lua_rawseti(L, 3, (int)i + 1);
        }
        candidates[i].min_value_size = required_size(L, 5, "min_value_size");
        candidates[i].max_value_size = required_size(L, 5, "max_value_size");
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    self->options.candidates = candidates;
    self->options.candidate_count = count;
#if LUA_VERSION_NUM == 501
    lua_setfenv(L, 2);
#else
    lua_setuservalue(L, 2);
#endif
    return 1;
}

static lua_generator_t* check_generator(lua_State* L) {
    return (lua_generator_t*)luaL_checkudata(L, 1, GENERATOR_MT);
}

/* workspace_size() -> scratch bytes each case requires; validates the
 * configuration, not the Format. */
static int generator_workspace_size(lua_State* L) {
    size_t       size = 0;
    tlv_result_t code = tlv_generator_workspace_size(&check_generator(L)->options, &size);
    if (code != TLV_OK) return opentlv_lua_raise(L, code, 0, 0);
    opentlv_lua_codec_push_uint(L, size);
    return 1;
}

/* generate(case_index) -> one complete case as a binary string. */
static int generator_generate(lua_State* L) {
    lua_generator_t*        self = check_generator(L);
    tlv_generator_options_t options = self->options;
    if (!lua_isnoneornil(L, 2)) options.case_index = opentlv_lua_codec_uint(L, 2, UINT64_MAX);
    size_t workspace = 0, capacity = options.max_case_size, written = 0;
    /* Capabilities precede configuration, as in tlv_generate(). */
    tlv_result_t code = tlv_format_can_read(self->format) && tlv_format_can_write(self->format)
                            ? tlv_generator_workspace_size(&options, &workspace)
                            : TLV_ERR_UNSUPPORTED;
    if (code == TLV_OK && capacity > SIZE_MAX - workspace) code = TLV_ERR_OVERFLOW;
    if (code != TLV_OK) return opentlv_lua_raise(L, code, 0, 0);
    /* One rooted block holds the scratch workspace, then the case. */
    uint8_t* storage = lua_newuserdata(L, workspace + capacity);
    code = tlv_generate(self->format, &options, storage + workspace, capacity, storage, workspace,
                        &written);
    if (code != TLV_OK) return opentlv_lua_raise(L, code, 0, 0);
    lua_pushlstring(L, (const char*)storage + workspace, written);
    return 1;
}

void opentlv_lua_open_generator(lua_State* L, int module_table_index) {
    static const opentlv_lua_method_t methods[] = {{"generate", generator_generate},
                                                   {"workspace_size", generator_workspace_size},
                                                   {NULL, NULL}};
    opentlv_lua_new_type(L, GENERATOR_MT, methods, NULL, NULL, NULL);
    lua_pushcfunction(L, generator_new);
    lua_setfield(L, module_table_index, "generator");
    lua_pushinteger(L, TLV_GENERATOR_VERSION);
    lua_setfield(L, module_table_index, "GENERATOR_VERSION");
}
