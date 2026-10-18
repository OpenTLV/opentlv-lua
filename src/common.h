// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#ifndef OPENTLV_LUA_COMMON_H
#define OPENTLV_LUA_COMMON_H

#include "compat.h"

#include <tlv/element.h>

/*
 * Shared infrastructure for every OpenTLV Lua userdata type (including Reader,
 * Writer and Schema). Conventions established here, so
 * later components add to them instead of reinventing them:
 *
 * - Ownership/lifetime: a userdata never copies the Lua values it borrows
 *   from (a source string, a format object, ...). Instead it keeps them
 *   alive for exactly as long as it is alive itself by holding a
 *   luaL_ref()/LUA_REGISTRYINDEX reference to each one, released in its
 *   __gc metamethod with luaL_unref(). This is the one ownership mechanism
 *   types borrowing independent objects use; there is no separate "borrowed
 *   vs. owned" userdata flavor. Codec callbacks instead live in a GC-visible
 *   userdata environment/uservalue table so a callback capturing its own codec
 *   forms a collectible cycle, rather than an immortal registry root.
 * - Embedding over pointers: a userdata that needs caller-owned config
 *   storage for the C API it wraps (for example a tlv_fixed_format_t for
 *   tlv_fixed_format_init()) embeds that storage directly inside
 *   itself rather than allocating it separately, since a Lua userdata
 *   block's address never changes for its lifetime. This avoids a second
 *   allocation and a second lifetime to track.
 * - Registering a type: opentlv_lua_new_type() below assembles a
 *   metatable's __index/__gc/__tostring/__call from plain lua_CFunction
 *   pointers, so every type is registered the same shape.
 * - Errors: never raised directly by anything invoked as a callback from a
 *   C library function (see error.h and visitor.c's trampoline for why); every
 *   other C function calls lua_error() directly through error.h's helpers.
 */

/* One row per method added to a type's __index table; a NULL `name` ends
 * the list. */
typedef struct opentlv_lua_method {
    const char*   name;
    lua_CFunction fn;
} opentlv_lua_method_t;

/*
 * Registers the metatable named `name` (via luaL_newmetatable(), so a type
 * is registered at most once even if this is called again): builds
 * `__index` from `methods` (NULL or a list with no rows to skip it), and
 * sets `__gc`/`__tostring`/`__call` to the given functions, each of which
 * may be NULL to leave that metamethod unset. Leaves the stack as it found
 * it.
 *
 * Every OpenTLV Lua userdata type is registered through this one function
 * instead of hand-assembling its metatable, so the shape (which
 * metamethods exist, how methods are listed) stays identical as more types
 * are added.
 */
void opentlv_lua_new_type(lua_State* L, const char* name, const opentlv_lua_method_t* methods,
                          lua_CFunction gc, lua_CFunction tostring, lua_CFunction call);

/*
 * Pushes {tag = <string>, length = <integer>, value = <string>, offset =
 * <integer>} for one decoded element: the one generic conversion from a
 * borrowed C tlv_element_t to a Lua value every reading or traversal
 * operation needs, shared so Reader (reader.c) and tree traversal (visitor.c)
 * produce identically shaped entries. `offset` is the absolute position of
 * the element's tag within the buffer the tag/value were read from.
 *
 * Never explicitly calls lua_error(): narrowing tlv_value_t's 64-bit length to size_t
 * can fail (only on builds where size_t is narrower than the value
 * actually read), and this may be called from contexts, such as the
 * tlv_tree_reader_visit() visitor trampoline in visitor.c, where raising directly
 * would unsafely unwind through library frames not prepared for it. On
 * failure it leaves the stack as it found it and returns the narrowing
 * failure's tlv_result_t instead of TLV_OK, and the caller decides how to
 * report it. Lua allocations can still raise errors; native visitor adapters
 * must invoke this helper inside a protected Lua call.
 */
int opentlv_lua_push_element(lua_State* L, const tlv_element_t* element, size_t offset);

#endif /* OPENTLV_LUA_COMMON_H */
