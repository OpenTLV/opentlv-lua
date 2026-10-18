// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

/* Test-only module: fail all growth allocations after the selected point,
 * including Lua's emergency-GC retry, then restore the interpreter allocator. */
#include "compat.h"

typedef struct {
    lua_Alloc original;
    void*     context;
    size_t    remaining;
} allocator_t;

static void* failing_alloc(void* context, void* pointer, size_t old_size, size_t new_size) {
    allocator_t* allocator = (allocator_t*)context;
    if (new_size && (!pointer || new_size > old_size)) {
        if (!allocator->remaining) return NULL;
        allocator->remaining--;
    }
    return allocator->original(allocator->context, pointer, old_size, new_size);
}

static int run(lua_State* L) {
    allocator_t allocator;
    luaL_checktype(L, 1, LUA_TFUNCTION);
    allocator.remaining = (size_t)luaL_checkinteger(L, 2);
    luaL_checkstack(L, 3, "allocator test");
    lua_pushvalue(L, 1);
    allocator.original = lua_getallocf(L, &allocator.context);
    lua_setallocf(L, failing_alloc, &allocator);
    int status = lua_pcall(L, 0, 0, 0);
    lua_setallocf(L, allocator.original, allocator.context);
    if (status != LUA_OK) lua_pop(L, 1);
    lua_pushboolean(L, status == LUA_OK);
    return 1;
}

int luaopen_opentlv_test_allocator(lua_State* L) {
    lua_pushcfunction(L, run);
    return 1;
}
