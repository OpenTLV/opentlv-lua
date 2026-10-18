// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel
#include "program.h"
#include "common.h"
#include "document.h"
#include "error.h"
#include "format.h"
#include "query.h"
#include <tlv/query/adapters.h>
#if OPENTLV_EMV
#include <tlv/builtins/emv/query.h>
#endif
#include <tlv/writer/tree.h>
#if OPENTLV_FORMAT_BER
#include <tlv/builtins/asn1/query.h>
#endif
#include <string.h>
#include <limits.h>
#include "../../common/query_schema.h"

#define PROGRAM_MT "opentlv.QueryProgram"
#define EXECUTION_MT "opentlv.QueryExecution"
#define PROVIDER_ACTIVE "opentlv.QueryProviderActive"
typedef struct program_t program_t;
typedef struct {
    program_t* owner;
    int        callback_ref;
} provider_t;
struct program_t {
    const tlv_query_program_t* program;
    tlv_query_program_info_t   info;
    tlv_query_environment_t    environment;
    tlv_query_hook_t           hooks[4];
    tlv_query_tag_adapter_t    tags;
    int                        image_ref, format_ref;
    int        tag_class_ref, tag_number_ref, resolve_ref, resolve_value_ref, adapter_invoke_ref;
    provider_t providers[4];
    lua_State* callback_state;
    int        provider_invoke_ref, callback_active, callback_failed, callback_error_index;
};
typedef struct {
    program_t*        program;
    tlv_query_exec_t* exec;
    void*             storage;
    size_t            capacity, depth, nodes, work, pinned;
    tlv_tree_reader_t reader;
    tlv_tree_frame_t* frames;
    int               program_ref, storage_ref, frames_ref, pins_ref, document_ref, values_ref;
    int               retained, has_reader, busy, fed;
} execution_t;
typedef struct {
    const char* name;
    size_t      size;
    tlv_tag_t   tag;
} name_t;
typedef struct {
    name_t* names;
    size_t  count;
} names_t;

static void* arena(lua_State* L, size_t bytes, size_t alignment) {
    if (bytes > SIZE_MAX - alignment + 1) {
        opentlv_lua_raise(L, TLV_ERR_OVERFLOW, 0, 0);
        return NULL;
    }
    uintptr_t raw = (uintptr_t)lua_newuserdata(L, bytes + alignment - 1);
    return (void*)((raw + alignment - 1) & ~(uintptr_t)(alignment - 1));
}
static void field(lua_State* L, const char* name, size_t value) {
    lua_pushnumber(L, (lua_Number)value);
    lua_setfield(L, -2, name);
}
void opentlv_lua_push_query_error(lua_State* L, tlv_result_t code,
                                  const tlv_query_diagnostic_t* d) {
    tlv_query_diagnostic_t state = {0};
    if (code == TLV_ERR_INVALID_STATE) {
        if (d) state = *d;
        state.kind = TLV_QUERY_ERROR_STATE;
        d = &state;
    }
    if (!d) {
        opentlv_lua_push_error(L, code, 0, 0);
        return;
    }
    opentlv_lua_push_diagnostic(L, &d->diagnostic);
    field(L, "code", code);
    lua_pushstring(L, tlv_result_string(code));
    lua_setfield(L, -2, "message");
    if (d->cause == TLV_QUERY_CAUSE_READER) opentlv_lua_add_reader_detail(L, &d->detail.reader);
    lua_newtable(L);
    field(L, "kind", d->kind);
    lua_pushstring(L, tlv_query_error_kind_string(d->kind));
    lua_setfield(L, -2, "kind_name");
    /* Related span only: an expression span that is primary is already "location". */
    opentlv_lua_push_location(L, &d->expression);
    lua_setfield(L, -2, "expression");
    field(L, "configured", d->configured);
    if (d->cause == TLV_QUERY_CAUSE_CODEC) {
        opentlv_lua_push_codec_detail(L, &d->detail.codec);
        lua_setfield(L, -2, "codec_detail");
    }
    if (d->limit) {
        lua_pushstring(L, d->limit);
        lua_setfield(L, -2, "limit");
    }
    lua_setfield(L, -2, "query");
}
static int query_error(lua_State* L, tlv_result_t code, const tlv_query_diagnostic_t* d) {
    opentlv_lua_push_query_error(L, code, d);
    return lua_error(L);
}
static int provider_active(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, PROVIDER_ACTIVE);
    int active = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return active;
}
static program_t* program(lua_State* L, int index) {
    if (provider_active(L)) {
        query_error(L, TLV_ERR_INVALID_STATE, NULL);
        return NULL;
    }
    program_t* p = luaL_checkudata(L, index, PROGRAM_MT);
    if (p->callback_active) {
        query_error(L, TLV_ERR_INVALID_STATE, NULL);
        return NULL;
    }
    return p;
}
static execution_t* execution(lua_State* L) {
    if (provider_active(L)) {
        query_error(L, TLV_ERR_INVALID_STATE, NULL);
        return NULL;
    }
    if (!lua_checkstack(L, 16)) luaL_error(L, "Query callback stack unavailable");
    execution_t* q = luaL_checkudata(L, 1, EXECUTION_MT);
    if (!q->exec || q->busy || q->program->callback_active) {
        query_error(L, TLV_ERR_INVALID_STATE, NULL);
        return NULL;
    }
#if OPENTLV_DOCUMENT
    if (q->document_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, q->document_ref);
        opentlv_lua_document_native(L, lua_gettop(L));
        lua_pop(L, 1);
    }
#endif
    q->program->callback_state = L;
    q->program->callback_failed = 0;
    lua_pushnil(L);
    q->program->callback_error_index = lua_gettop(L);
    return q;
}
static int provider_error(lua_State* L, program_t* p, tlv_result_t code,
                          const tlv_query_diagnostic_t* diagnostic) {
    if (p->callback_failed) {
        p->callback_failed = 0;
        lua_pushvalue(L, p->callback_error_index);
        return lua_error(L);
    }
    return query_error(L, code, diagnostic);
}
static execution_t* protected_execution(lua_State* L) {
    execution_t* q = lua_touserdata(L, -1);
    lua_pop(L, 1);
    q->program->callback_state = L;
    q->program->callback_failed = 0;
    lua_pushnil(L);
    q->program->callback_error_index = lua_gettop(L);
    return q;
}
/* Retain both execution storage and any Document through argument conversion,
 * native calls and owned-result projection, including Lua finalizers and OOM. */
static int execution_call(lua_State* L, lua_CFunction function, int reset) {
    int count = lua_gettop(L);
    luaL_checkstack(L, count + 18, "Query protected operation");
    lua_pushcfunction(L, function);
    execution_t* q;
    if (reset) {
        if (provider_active(L)) return query_error(L, TLV_ERR_INVALID_STATE, NULL);
        q = luaL_checkudata(L, 1, EXECUTION_MT);
        if (!q->exec || q->busy || q->program->callback_active)
            return query_error(L, TLV_ERR_INVALID_STATE, NULL);
    } else {
        q = execution(L);
        lua_pop(L, 1);
    }
#if OPENTLV_DOCUMENT
    int document = !reset && q->document_ref != LUA_NOREF;
    if (document) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, q->document_ref);
        opentlv_lua_document_query_guard(L, lua_gettop(L), 1);
        lua_pop(L, 1);
    }
#endif
    q->busy = 1;
    for (int i = 1; i <= count; ++i) lua_pushvalue(L, i);
    lua_pushlightuserdata(L, q);
    int status = lua_pcall(L, count + 1, LUA_MULTRET, 0);
    q->busy = 0;
#if OPENTLV_DOCUMENT
    if (document) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, q->document_ref);
        opentlv_lua_document_query_guard(L, lua_gettop(L), 0);
        lua_pop(L, 1);
    }
#endif
    return status == LUA_OK ? lua_gettop(L) - count : lua_error(L);
}
static int program_gc(lua_State* L) {
    program_t* p = luaL_checkudata(L, 1, PROGRAM_MT);
    if (p->callback_active) return query_error(L, TLV_ERR_INVALID_STATE, NULL);
    luaL_unref(L, LUA_REGISTRYINDEX, p->image_ref);
    luaL_unref(L, LUA_REGISTRYINDEX, p->format_ref);
    luaL_unref(L, LUA_REGISTRYINDEX, p->provider_invoke_ref);
    int* adapters[] = {&p->tag_class_ref, &p->tag_number_ref, &p->resolve_ref,
                       &p->resolve_value_ref, &p->adapter_invoke_ref};
    for (size_t i = 0; i < sizeof adapters / sizeof *adapters; ++i) {
        luaL_unref(L, LUA_REGISTRYINDEX, *adapters[i]);
        *adapters[i] = LUA_NOREF;
    }
    p->provider_invoke_ref = LUA_NOREF;
    for (size_t i = 0; i < 4; ++i) {
        luaL_unref(L, LUA_REGISTRYINDEX, p->providers[i].callback_ref);
        p->providers[i].callback_ref = LUA_NOREF;
    }
    p->image_ref = p->format_ref = LUA_NOREF;
    return 0;
}
typedef struct {
    provider_t*             provider;
    const tlv_tree_event_t* event;
    const uint8_t*          data;
    size_t                  size;
} provider_call_t;
static int provider_invoke(lua_State* L) {
    provider_call_t* call = lua_touserdata(L, 1);
    lua_rawgeti(L, LUA_REGISTRYINDEX, call->provider->callback_ref);
    lua_pushlstring(L, call->size ? (const char*)call->data : "", call->size);
    if (call->event) {
        lua_newtable(L);
        field(L, "kind", call->event->kind);
        field(L, "depth", call->event->depth);
        field(L, "offset", call->event->offset);
        lua_pushlstring(L, (const char*)call->event->element.tag.data,
                        call->event->element.tag.size);
        lua_setfield(L, -2, "tag");
        lua_pushlstring(
            L, call->event->element.value.size ? (const char*)call->event->element.value.data : "",
            call->event->element.value.size);
        lua_setfield(L, -2, "value");
    } else
        lua_pushnil(L);
    lua_call(L, 2, 1);
    return 1;
}
static tlv_result_t provider_decode(const void* context, const tlv_tree_event_t* event,
                                    const uint8_t* data, size_t size, void* scratch,
                                    size_t capacity, tlv_query_result_t* result,
                                    tlv_codec_diagnostic_t* diagnostic) {
    tlv_codec_diagnostic_init(diagnostic, TLV_CODEC_OP_DECODE);
    provider_t*     provider = (provider_t*)context;
    program_t*      p = provider->owner;
    lua_State*      L = p->callback_state;
    provider_call_t call = {provider, event, data, size};
    lua_rawgeti(L, LUA_REGISTRYINDEX, p->provider_invoke_ref);
    lua_pushlightuserdata(L, &call);
    p->callback_active = 1;
    lua_pushboolean(L, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, PROVIDER_ACTIVE);
    int failed = lua_pcall(L, 1, 1, 0);
    lua_pushboolean(L, 0);
    lua_setfield(L, LUA_REGISTRYINDEX, PROVIDER_ACTIVE);
    p->callback_active = 0;
    if (failed) {
        lua_replace(L, p->callback_error_index);
        p->callback_failed = 1;
        return tlv_codec_diagnostic_result(diagnostic, TLV_ERR_INVALID_VALUE);
    }
    memset(result, 0, sizeof *result);
    tlv_result_t code = TLV_OK;
    if (lua_type(L, -1) == LUA_TNUMBER) {
#if LUA_VERSION_NUM >= 503
        int         valid;
        lua_Integer value = lua_tointegerx(L, -1, &valid);
#else
        lua_Number number = lua_tonumber(L, -1);
        lua_Number lower = sizeof(lua_Integer) >= 8 ? (lua_Number)INT64_MIN : (lua_Number)INT32_MIN;
        lua_Number exact = sizeof(lua_Number) == sizeof(float) ? (lua_Number)16777215
                                                               : (lua_Number)9007199254740991.0;
        int valid = number >= lower && number < -lower && number >= -exact && number <= exact;
        lua_Integer value = valid ? lua_tointeger(L, -1) : 0;
        valid = valid && (lua_Number)value == number;
#endif
        if (!valid)
            code = TLV_ERR_INVALID_VALUE;
        else {
            result->kind = TLV_QUERY_RESULT_INTEGER;
            result->integer = (int64_t)value;
        }
    } else if (lua_type(L, -1) == LUA_TSTRING) {
        size_t      length;
        const char* value = lua_tolstring(L, -1, &length);
        if (length > capacity)
            code = TLV_ERR_BUFFER_TOO_SHORT;
        else {
            if (length) memcpy(scratch, value, length);
            result->kind = TLV_QUERY_RESULT_STRING;
            result->data = scratch;
            result->size = length;
        }
    } else
        code = TLV_ERR_INVALID_VALUE;
    lua_pop(L, 1);
    return tlv_codec_diagnostic_result(diagnostic, code);
}
typedef struct {
    program_t*     owner;
    const uint8_t* data;
    size_t         size;
    const char*    scope;
    size_t         scope_size;
    int            selector;
} adapter_call_t;
static int adapter_invoke(lua_State* L) {
    adapter_call_t* call = lua_touserdata(L, 1);
    program_t*      p = call->owner;
    lua_rawgeti(L, LUA_REGISTRYINDEX,
                call->selector == 0   ? p->resolve_ref
                : call->selector == 1 ? p->tag_class_ref
                                      : p->tag_number_ref);
    if (call->selector == 0)
        lua_pushlstring(L, call->scope_size ? call->scope : "", call->scope_size);
    lua_pushlstring(L, call->size ? (const char*)call->data : "", call->size);
    lua_call(L, call->selector == 0 ? 2 : 1, 1);
    if (call->selector == 0) {
        if (!lua_isnil(L, -1)) luaL_checktype(L, -1, LUA_TSTRING);
        lua_pushvalue(L, -1);
        int retained = luaL_ref(L, LUA_REGISTRYINDEX);
        luaL_unref(L, LUA_REGISTRYINDEX, p->resolve_value_ref);
        p->resolve_value_ref = retained;
    } else {
#if LUA_VERSION_NUM >= 503
        (void)luaL_checkinteger(L, -1);
#else
        lua_Number value = luaL_checknumber(L, -1);
        lua_Number bound =
            sizeof(lua_Integer) >= 8 ? (lua_Number)9223372036854775808.0 : (lua_Number)2147483648.0;
        lua_Number exact = sizeof(lua_Number) == sizeof(float) ? (lua_Number)16777215
                                                               : (lua_Number)9007199254740991.0;
        if (value < -bound || value >= bound || value < -exact || value > exact ||
            (lua_Number)lua_tointeger(L, -1) != value)
            return luaL_error(L, "Tag adapter must return an exact integer");
#endif
    }
    return 1;
}
static tlv_result_t adapter_call(adapter_call_t* call) {
    program_t* p = call->owner;
    lua_State* L = p->callback_state;
    lua_rawgeti(L, LUA_REGISTRYINDEX, p->adapter_invoke_ref);
    lua_pushlightuserdata(L, call);
    p->callback_active = 1;
    lua_pushboolean(L, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, PROVIDER_ACTIVE);
    int failed = lua_pcall(L, 1, 1, 0);
    lua_pushboolean(L, 0);
    lua_setfield(L, LUA_REGISTRYINDEX, PROVIDER_ACTIVE);
    p->callback_active = 0;
    if (failed) {
        lua_replace(L, p->callback_error_index);
        p->callback_failed = 1;
        return TLV_ERR_INVALID_ARG;
    }
    return TLV_OK;
}
static tlv_result_t dynamic_resolve(const void* context, const char* scope, size_t scope_size,
                                    const char* name, size_t size, tlv_tag_t* tag) {
    program_t*     p = (program_t*)context;
    adapter_call_t call = {p, (const uint8_t*)name, size, scope, scope_size, 0};
    tlv_result_t   rc = adapter_call(&call);
    if (rc != TLV_OK) return rc;
    lua_State* L = p->callback_state;
    if (lua_isnil(L, -1))
        rc = TLV_ERR_INVALID_TAG;
    else {
        const char* data = lua_tolstring(L, -1, &size);
        *tag = tlv_tag((const uint8_t*)data, size);
    }
    lua_pop(L, 1);
    return rc;
}
static tlv_result_t tag_decompose(const void* context, const tlv_tag_t* tag, int64_t* value,
                                  int selector) {
    program_t*     p = (program_t*)context;
    adapter_call_t call = {p, tag->data, tag->size, NULL, 0, selector};
    tlv_result_t   rc = adapter_call(&call);
    if (rc != TLV_OK) return rc;
    *value = (int64_t)lua_tointeger(p->callback_state, -1);
    lua_pop(p->callback_state, 1);
    return TLV_OK;
}
static tlv_result_t tag_class(const void* context, const tlv_tag_t* tag, int64_t* value) {
    return tag_decompose(context, tag, value, 1);
}
static tlv_result_t tag_number(const void* context, const tlv_tag_t* tag, int64_t* value) {
    return tag_decompose(context, tag, value, 2);
}
static tlv_result_t resolve(const void* context, const char* scope, size_t scope_size,
                            const char* name, size_t size, tlv_tag_t* tag) {
    const names_t* lookup = context;
    for (size_t i = 0; i < lookup->count; ++i) {
        name_t* item = lookup->names + i;
        size_t  prefix = scope_size ? scope_size + 1 : 0;
        if (item->size != prefix + size) continue;
        if (scope_size && (memcmp(item->name, scope, scope_size) || item->name[scope_size] != ':'))
            continue;
        if (!memcmp(item->name + prefix, name, size)) {
            *tag = item->tag;
            return TLV_OK;
        }
    }
    return TLV_ERR_INVALID_TAG;
}
static size_t map_count(lua_State* L, int index) {
    size_t count = 0;
    lua_pushnil(L);
    while (lua_next(L, index)) {
        ++count;
        lua_pop(L, 1);
    }
    return count;
}
static int definition_lookup(lua_State* L) {
    const tlv_query_definition_resolver_t* resolver = lua_touserdata(L, lua_upvalueindex(1));
    size_t                                 space_size, name_size;
    const char*                            space = luaL_checklstring(L, 1, &space_size);
    const char*                            name = luaL_checklstring(L, 2, &name_size);
    tlv_tag_t                              tag;
    tlv_result_t                           rc =
        tlv_query_definition_resolve(resolver, space, space_size, name, name_size, &tag);
    if (rc != TLV_OK) return query_error(L, rc, NULL);
    lua_pushlstring(L, (const char*)tag.data, tag.size);
    return 1;
}
static void keep_definition_value(lua_State* L, int keepers, int* index) {
    if (*index == INT_MAX) luaL_error(L, "Definition registry is too large");
    lua_rawseti(L, keepers, ++*index);
}
static int definition_resolver(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    size_t count = map_count(L, 1);
    if (count > SIZE_MAX / sizeof(tlv_query_definition_scope_t) ||
        count > SIZE_MAX / sizeof(tlv_definition_registry_t))
        return query_error(L, TLV_ERR_OVERFLOW, NULL);
    tlv_query_definition_resolver_t* resolver = lua_newuserdata(L, sizeof *resolver);
    int                              owner = lua_gettop(L);
    lua_newtable(L);
    int                           keepers = lua_gettop(L), kept = 0;
    tlv_query_definition_scope_t* scopes = lua_newuserdata(L, count * sizeof *scopes);
    keep_definition_value(L, keepers, &kept);
    tlv_definition_registry_t* registries = lua_newuserdata(L, count * sizeof *registries);
    keep_definition_value(L, keepers, &kept);
    resolver->scopes = scopes;
    resolver->count = count;
    size_t i = 0;
    lua_pushnil(L);
    while (lua_next(L, 1)) {
        size_t namespace_size;
        scopes[i].namespace_name = luaL_checklstring(L, -2, &namespace_size);
        if (memchr(scopes[i].namespace_name, 0, namespace_size))
            return query_error(L, TLV_ERR_INVALID_ARG, NULL);
        lua_pushvalue(L, -2);
        keep_definition_value(L, keepers, &kept);
        luaL_checktype(L, -1, LUA_TTABLE);
        int definitions = lua_gettop(L);
#if LUA_VERSION_NUM >= 502
        size_t length = lua_rawlen(L, definitions);
#else
        size_t length = lua_objlen(L, definitions);
#endif
        if (length > INT_MAX || length > SIZE_MAX / sizeof(tlv_definition_t))
            return query_error(L, TLV_ERR_OVERFLOW, NULL);
        tlv_definition_t* entries = lua_newuserdata(L, length * sizeof *entries);
        keep_definition_value(L, keepers, &kept);
        registries[i] = (tlv_definition_registry_t){entries, length};
        scopes[i].definitions = &registries[i];
        for (size_t j = 0; j < length; ++j) {
            lua_rawgeti(L, definitions, (int)j + 1);
            luaL_checktype(L, -1, LUA_TTABLE);
            lua_getfield(L, -1, "tag");
            size_t         size;
            const uint8_t* bytes = (const uint8_t*)luaL_checklstring(L, -1, &size);
            entries[j].tag = tlv_tag(bytes, size);
            keep_definition_value(L, keepers, &kept);
            lua_getfield(L, -1, "name");
            entries[j].name = lua_isnil(L, -1) ? NULL : luaL_checklstring(L, -1, &size);
            if (entries[j].name && memchr(entries[j].name, 0, size))
                return query_error(L, TLV_ERR_INVALID_ARG, NULL);
            keep_definition_value(L, keepers, &kept);
            lua_pop(L, 1);
        }
        ++i;
        lua_pop(L, 1);
    }
    lua_pushvalue(L, owner);
    lua_pushvalue(L, keepers);
    lua_pushcclosure(L, definition_lookup, 2);
    return 1;
}
static int compile(lua_State* L, int image) {
    if (provider_active(L)) return query_error(L, TLV_ERR_INVALID_STATE, NULL);
    size_t      length;
    const char* text = luaL_checklstring(L, 1, &length);
    lua_settop(L, 3);
    if (lua_isnil(L, 2)) {
        lua_getfield(L, LUA_REGISTRYINDEX, OPENTLV_LUA_DEFAULT_FORMAT_KEY);
        lua_replace(L, 2);
    }
    tlv_lua_format_t* format = opentlv_lua_check_format(L, 2);
    if (!lua_isnil(L, 3)) luaL_checktype(L, 3, LUA_TTABLE);
    program_t* p = lua_newuserdata(L, sizeof *p);
    memset(p, 0, sizeof *p);
    p->image_ref = p->format_ref = LUA_NOREF;
    p->provider_invoke_ref = LUA_NOREF;
    p->tag_class_ref = p->tag_number_ref = p->resolve_ref = p->resolve_value_ref =
        p->adapter_invoke_ref = LUA_NOREF;
    p->callback_state = L;
    for (size_t i = 0; i < 4; ++i) {
        p->providers[i].owner = p;
        p->providers[i].callback_ref = LUA_NOREF;
    }
    luaL_getmetatable(L, PROGRAM_MT);
    lua_setmetatable(L, -2);
    int result_index = lua_gettop(L);
    lua_pushnil(L);
    p->callback_error_index = lua_gettop(L);
    lua_pushcfunction(L, adapter_invoke);
    p->adapter_invoke_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushvalue(L, 2);
    p->format_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    p->environment.format = &format->format;
    size_t                  hooks;
    const tlv_query_hook_t* builtin = tlv_query_builtin_hooks(&hooks);
    memcpy(p->hooks, builtin, hooks * sizeof *builtin);
#if OPENTLV_FORMAT_BER
    if (format->name && (!strcmp(format->name, "ber") || !strcmp(format->name, "cer") ||
                         !strcmp(format->name, "der"))) {
        p->environment.tags = &tlv_asn1_query_tags;
        p->hooks[hooks++] = tlv_asn1_query_date;
    }
#endif
    p->environment.hooks = p->hooks;
    p->environment.hook_count = hooks;
    tlv_query_compile_options_t options;
    tlv_query_compile_options_init(&options);
    options.environment = &p->environment;
    if (!lua_isnil(L, 3)) {
        lua_getfield(L, 3, "tags");
        if (!lua_isnil(L, -1)) {
            luaL_checktype(L, -1, LUA_TTABLE);
            int    tags = lua_gettop(L);
            size_t id = opentlv_lua_query_limit(L, tags, "id", 0);
            if (!id || id > UINT32_MAX) return query_error(L, TLV_ERR_INVALID_ARG, NULL);
            lua_getfield(L, tags, "class_of");
            if (!lua_isnil(L, -1)) luaL_checktype(L, -1, LUA_TFUNCTION);
            p->tag_class_ref = luaL_ref(L, LUA_REGISTRYINDEX);
            lua_getfield(L, tags, "number_of");
            if (!lua_isnil(L, -1)) luaL_checktype(L, -1, LUA_TFUNCTION);
            p->tag_number_ref = luaL_ref(L, LUA_REGISTRYINDEX);
            p->tags =
                (tlv_query_tag_adapter_t){(uint32_t)id, p, p->tag_class_ref < 0 ? NULL : tag_class,
                                          p->tag_number_ref < 0 ? NULL : tag_number};
            p->environment.tags = &p->tags;
        }
        lua_pop(L, 1);
        lua_getfield(L, 3, "resolve");
        if (!lua_isnil(L, -1)) luaL_checktype(L, -1, LUA_TFUNCTION);
        p->resolve_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        lua_getfield(L, 3, "providers");
        if (!lua_isnil(L, -1)) {
            luaL_checktype(L, -1, LUA_TTABLE);
            const char* selectors[] = {"num", "bcd", "text", "date"};
            int         table = lua_gettop(L);
            size_t      installed = 0;
            for (int function = 0; function < 4; ++function) {
                lua_getfield(L, table, selectors[function]);
                if (!lua_isnil(L, -1)) {
                    ++installed;
                    luaL_checktype(L, -1, LUA_TTABLE);
                    int    record = lua_gettop(L);
                    size_t id = opentlv_lua_query_limit(L, record, "id", 0);
                    if (!id || id > UINT32_MAX) return query_error(L, TLV_ERR_INVALID_ARG, NULL);
                    size_t capacity = opentlv_lua_query_limit(L, record, "max_result_bytes", 0);
                    lua_getfield(L, record, "decode");
                    luaL_checktype(L, -1, LUA_TFUNCTION);
                    p->providers[function].callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
                    size_t slot = 0;
                    while (slot < p->environment.hook_count &&
                           p->hooks[slot].function != (tlv_query_conversion_t)function)
                        ++slot;
                    if (slot == p->environment.hook_count) ++p->environment.hook_count;
                    p->hooks[slot] = (tlv_query_hook_t){(uint32_t)id,
                                                        (tlv_query_conversion_t)function,
                                                        capacity,
                                                        1,
                                                        &p->providers[function],
                                                        provider_decode};
                }
                lua_pop(L, 1);
            }
            if (installed != map_count(L, table)) return query_error(L, TLV_ERR_INVALID_ARG, NULL);
            lua_pushcfunction(L, provider_invoke);
            p->provider_invoke_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        }
        lua_pop(L, 1);
    }
    options.max_text = opentlv_lua_query_limit(L, 3, "max_text", options.max_text);
    options.max_tokens = opentlv_lua_query_limit(L, 3, "max_tokens", options.max_tokens);
    options.max_nesting = opentlv_lua_query_limit(L, 3, "max_nesting", options.max_nesting);
    options.max_states = opentlv_lua_query_limit(L, 3, "max_states", options.max_states);
    options.max_pattern = opentlv_lua_query_limit(L, 3, "max_pattern", options.max_pattern);
    options.max_resolved_tag =
        opentlv_lua_query_limit(L, 3, "max_resolved_tag", options.max_resolved_tag);
    names_t names = {0};
    if (!lua_isnil(L, 3)) {
        lua_getfield(L, 3, "optimize");
        if (!lua_isnil(L, -1)) options.optimize = lua_toboolean(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 3, "variables");
        if (!lua_isnil(L, -1)) {
            luaL_checktype(L, -1, LUA_TTABLE);
            int    table = lua_gettop(L);
            size_t count = map_count(L, table);
            if (count > SIZE_MAX / sizeof(tlv_query_variable_t))
                return query_error(L, TLV_ERR_OVERFLOW, NULL);
            tlv_query_variable_t* vars = lua_newuserdata(L, count * sizeof *vars);
            options.variables = vars;
            options.variable_count = count;
            size_t i = 0;
            lua_pushnil(L);
            while (lua_next(L, table)) {
                size_t      size;
                const char* name = luaL_checklstring(L, -2, &size);
                if (memchr(name, 0, size)) return query_error(L, TLV_ERR_INVALID_ARG, NULL);
                const char* type = luaL_checkstring(L, -1);
                vars[i].name = name;
                vars[i++].type = !strcmp(type, "integer")  ? TLV_QUERY_RESULT_INTEGER
                                 : !strcmp(type, "bytes")  ? TLV_QUERY_RESULT_BYTES
                                 : !strcmp(type, "string") ? TLV_QUERY_RESULT_STRING
                                                           : TLV_QUERY_RESULT_NODES;
                lua_pop(L, 1);
            }
        }
        lua_getfield(L, 3, "names");
        if (!lua_isnil(L, -1)) {
            luaL_checktype(L, -1, LUA_TTABLE);
            int table = lua_gettop(L);
            names.count = map_count(L, table);
            if (names.count > SIZE_MAX / sizeof(name_t))
                return query_error(L, TLV_ERR_OVERFLOW, NULL);
            names.names = lua_newuserdata(L, names.count * sizeof *names.names);
            size_t i = 0;
            lua_pushnil(L);
            while (lua_next(L, table)) {
                name_t* item = names.names + i++;
                item->name = luaL_checklstring(L, -2, &item->size);
                size_t         size;
                const uint8_t* tag = (const uint8_t*)luaL_checklstring(L, -1, &size);
                item->tag = tlv_tag(tag, size);
                lua_pop(L, 1);
            }
        }
    }
    options.resolve = resolve;
    options.resolve_context = &names;
    if (p->resolve_ref >= 0) {
        if (names.count) return query_error(L, TLV_ERR_INVALID_ARG, NULL);
        options.resolve = dynamic_resolve;
        options.resolve_context = p;
    }
    size_t                 bytes, alignment;
    tlv_query_diagnostic_t diagnostic = {0};
    tlv_result_t rc = image ? tlv_query_program_load_scratch(text, length, &options, &bytes,
                                                             &alignment, &diagnostic)
                            : tlv_query_compile_prepare_size(text, length, &options, &bytes,
                                                             &alignment, &diagnostic);
    if (rc != TLV_OK) return provider_error(L, p, rc, &diagnostic);
    void*                      scratch = arena(L, bytes, alignment);
    const tlv_query_program_t* prepared = NULL;
    p->info.struct_size = sizeof p->info;
    if (!image)
        rc = tlv_query_compile_prepare(text, length, &options, scratch, bytes, &prepared, &p->info,
                                       &diagnostic);
    if (rc != TLV_OK) return provider_error(L, p, rc, &diagnostic);
    size_t image_size = image ? length : p->info.program_size;
    void*  validation = NULL;
    size_t validation_size = 0;
    if (!image) {
        rc = tlv_query_program_load_scratch(prepared, image_size, &options, &validation_size,
                                            &alignment, &diagnostic);
        if (rc != TLV_OK) return provider_error(L, p, rc, &diagnostic);
        validation = arena(L, validation_size, alignment);
    }
    void* storage = arena(L, image_size, 16);
    p->image_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    if (image) {
        memcpy(storage, text, length);
        rc = tlv_query_program_load(storage, length, &options, scratch, bytes, &p->program,
                                    &p->info, &diagnostic);
    } else {
        rc = tlv_query_compile_commit(prepared, image_size, &options, validation, validation_size,
                                      storage, image_size, &p->info, &diagnostic);
        p->program = storage;
    }
    if (rc != TLV_OK) return provider_error(L, p, rc, &diagnostic);
    lua_pushvalue(L, result_index);
    return 1;
}
static int program_compile(lua_State* L) {
    return compile(L, 0);
}
static int program_load(lua_State* L) {
    return compile(L, 1);
}
static int program_image(lua_State* L) {
    program_t* p = program(L, 1);
    lua_pushlstring(L, (const char*)p->program, p->info.program_size);
    return 1;
}
static int render(lua_State* L, int explain) {
    program_t*   p = program(L, 1);
    size_t       size;
    tlv_result_t rc = explain ? tlv_query_program_explain(p->program, NULL, 0, &size)
                              : tlv_query_program_format(p->program, NULL, 0, &size);
    if (rc != TLV_OK) return query_error(L, rc, NULL);
    char* output = lua_newuserdata(L, size);
    rc = explain ? tlv_query_program_explain(p->program, output, size, &size)
                 : tlv_query_program_format(p->program, output, size, &size);
    if (rc != TLV_OK) return query_error(L, rc, NULL);
    lua_pushlstring(L, output, size - 1);
    return 1;
}
static int program_format(lua_State* L) {
    return render(L, 0);
}
static int program_explain(lua_State* L) {
    return render(L, 1);
}
static int program_info(lua_State* L) {
    program_t* p = program(L, 1);
    lua_newtable(L);
#define INFO(name) field(L, #name, p->info.name)
    INFO(program_size);
    INFO(program_alignment);
    INFO(scratch_size);
    INFO(scratch_alignment);
    INFO(states);
    INFO(language_version);
    INFO(level);
    INFO(result_kind);
    INFO(expression_values);
    INFO(instructions);
    INFO(variable_slots);
    INFO(codec_scratch);
    INFO(pattern_bytes);
    INFO(optimized_states);
    INFO(expression_stack);
    INFO(candidate_size);
    INFO(candidate_alignment);
    INFO(frame_states);
    INFO(decision_timing);
    INFO(stable_input_required);
    INFO(constructed_values_required);
#undef INFO
    return 1;
}
static int program_variables(lua_State* L) {
    program_t* p = program(L, 1);
    lua_newtable(L);
    for (size_t i = 0; i < tlv_query_program_variable_count(p->program); ++i) {
        tlv_query_variable_info_t value;
        tlv_result_t              rc = tlv_query_program_variable(p->program, i, &value);
        if (rc != TLV_OK) return query_error(L, rc, NULL);
        lua_pushlstring(L, value.name, value.name_size);
        lua_pushinteger(L, value.type);
        lua_rawset(L, -3);
    }
    return 1;
}
static int execution_gc(lua_State* L) {
    execution_t* q = luaL_checkudata(L, 1, EXECUTION_MT);
    if (q->busy) return query_error(L, TLV_ERR_INVALID_STATE, NULL);
    int* refs[] = {&q->program_ref, &q->storage_ref,  &q->frames_ref,
                   &q->pins_ref,    &q->document_ref, &q->values_ref};
    for (size_t i = 0; i < sizeof refs / sizeof *refs; ++i) {
        luaL_unref(L, LUA_REGISTRYINDEX, *refs[i]);
        *refs[i] = LUA_NOREF;
    }
    q->exec = NULL;
    q->program = NULL;
    return 0;
}
static tlv_result_t initialize(execution_t* q) {
    return q->retained
               ? tlv_query_eval_init(q->program->program, &q->program->environment, q->storage,
                                     q->capacity, q->depth, q->nodes, q->work, &q->exec)
               : tlv_query_exec_init(q->program->program, q->storage, q->capacity, q->depth,
                                     q->nodes, q->work, &q->exec);
}
static int program_execution(lua_State* L) {
    program_t* p = program(L, 1);
    lua_settop(L, 2);
    execution_t* q = lua_newuserdata(L, sizeof *q);
    memset(q, 0, sizeof *q);
    q->program_ref = q->storage_ref = q->frames_ref = q->pins_ref = q->document_ref =
        q->values_ref = LUA_NOREF;
    luaL_getmetatable(L, EXECUTION_MT);
    lua_setmetatable(L, -2);
    int result = lua_gettop(L);
    q->program = p;
    lua_pushvalue(L, 1);
    q->program_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    q->depth = opentlv_lua_query_limit(L, 2, "max_depth", 64);
    q->nodes = opentlv_lua_query_limit(L, 2, "max_nodes", 1024);
    q->work = opentlv_lua_query_limit(L, 2, "max_work", 100000000);
    q->retained = 1;
    if (!lua_isnil(L, 2)) {
        lua_getfield(L, 2, "retained");
        if (!lua_isnil(L, -1)) q->retained = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    size_t       alignment;
    tlv_result_t rc =
        q->retained ? tlv_query_eval_size(p->program, q->depth, q->nodes, &q->capacity, &alignment)
                    : tlv_query_exec_size(p->program, q->depth, &q->capacity, &alignment);
    if (rc != TLV_OK) return query_error(L, rc, NULL);
    q->storage = arena(L, q->capacity, alignment);
    q->storage_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    if (q->depth > SIZE_MAX / sizeof *q->frames) return query_error(L, TLV_ERR_OVERFLOW, NULL);
    q->frames = lua_newuserdata(L, q->depth * sizeof *q->frames);
    q->frames_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_newtable(L);
    q->pins_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    rc = initialize(q);
    if (rc != TLV_OK) return query_error(L, rc, NULL);
    lua_pushvalue(L, result);
    return 1;
}
static int execution_reset_run(lua_State* L) {
    execution_t* q = protected_execution(L);
    // Allocate replacement roots before changing native state.
    lua_newtable(L);
    int          pins = luaL_ref(L, LUA_REGISTRYINDEX);
    tlv_result_t rc = initialize(q);
    if (rc != TLV_OK) {
        luaL_unref(L, LUA_REGISTRYINDEX, pins);
        return query_error(L, rc, NULL);
    }
    luaL_unref(L, LUA_REGISTRYINDEX, q->pins_ref);
    q->pins_ref = pins;
    luaL_unref(L, LUA_REGISTRYINDEX, q->document_ref);
    q->document_ref = LUA_NOREF;
    luaL_unref(L, LUA_REGISTRYINDEX, q->values_ref);
    q->values_ref = LUA_NOREF;
    q->has_reader = 0;
    q->fed = 0;
    q->pinned = 0;
    return 0;
}
static int execution_reset(lua_State* L) {
    return execution_call(L, execution_reset_run, 1);
}
static void pin(lua_State* L, execution_t* q, int index) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, q->pins_ref);
    lua_pushvalue(L, index);
    lua_rawseti(L, -2, (int)++q->pinned);
    lua_pop(L, 1);
}
static int execution_input_run(lua_State* L) {
    execution_t* q = protected_execution(L);
    if (q->fed) return query_error(L, TLV_ERR_INVALID_STATE, NULL);
    size_t         size;
    const uint8_t* data = (const uint8_t*)luaL_checklstring(L, 2, &size);
    size_t         discard = 0;
    if (!lua_isnoneornil(L, 3)) {
        lua_Integer value = luaL_checkinteger(L, 3);
        if (value < 0) return query_error(L, TLV_ERR_INVALID_ARG, NULL);
        discard = (size_t)value;
    }
    int final = lua_isnoneornil(L, 4) || lua_toboolean(L, 4);
    if (q->document_ref != LUA_NOREF) return query_error(L, TLV_ERR_INVALID_STATE, NULL);
    if (!q->has_reader && discard) return query_error(L, TLV_ERR_INVALID_ARG, NULL);
    pin(L, q, 2);
    tlv_result_t rc =
        q->has_reader ? tlv_tree_reader_set_input(&q->reader, data, size, discard, final)
        : final       ? tlv_tree_reader_init(&q->reader, data, size, q->program->environment.format,
                                             q->frames, q->depth, q->depth, q->nodes)
                      : tlv_tree_reader_init_incremental(&q->reader, data, size,
                                                         q->program->environment.format, q->frames,
                                                         q->depth, q->depth, q->nodes);
    if (rc != TLV_OK) return query_error(L, rc, NULL);
    q->has_reader = 1;
    return 0;
}
static int execution_input(lua_State* L) {
    return execution_call(L, execution_input_run, 0);
}
static int execution_bind_run(lua_State* L) {
    execution_t*            q = protected_execution(L);
    const char*             name = luaL_checkstring(L, 2);
    const char*             type = luaL_checkstring(L, 3);
    tlv_query_result_kind_t kind;
    int64_t                 integer = 0;
    size_t                  size = 0;
    const uint8_t*          data = NULL;
    if (!strcmp(type, "integer")) {
        kind = TLV_QUERY_RESULT_INTEGER;
        integer = (int64_t)luaL_checkinteger(L, 4);
    } else {
        kind = !strcmp(type, "bytes")    ? TLV_QUERY_RESULT_BYTES
               : !strcmp(type, "string") ? TLV_QUERY_RESULT_STRING
                                         : TLV_QUERY_RESULT_NODES;
        data = (const uint8_t*)luaL_checklstring(L, 4, &size);
        pin(L, q, 4);
    }
    tlv_query_diagnostic_t diagnostic = {0};
    tlv_result_t rc = tlv_query_exec_bind(q->exec, name, kind, integer, data, size, &diagnostic);
    return rc == TLV_OK ? 0 : query_error(L, rc, &diagnostic);
}
static int execution_bind(lua_State* L) {
    return execution_call(L, execution_bind_run, 0);
}
typedef struct {
    tlv_tree_event_t event;
    int              found;
} pull_t;
static int push_match(lua_State* L, const tlv_tree_event_t* event) {
    tlv_result_t rc = (tlv_result_t)opentlv_lua_push_element(L, &event->element, event->offset);
    if (rc != TLV_OK) return query_error(L, rc, NULL);
    field(L, "depth", event->depth);
    lua_pushboolean(L, event->kind == TLV_TREE_BEGIN);
    lua_setfield(L, -2, "constructed");
    return 1;
}
static int execution_feed_run(lua_State* L) {
    execution_t* q = lua_touserdata(L, 3);
    lua_pushnil(L);
    q->program->callback_state = L;
    q->program->callback_failed = 0;
    q->program->callback_error_index = lua_gettop(L);
    if (q->has_reader || q->document_ref != LUA_NOREF)
        return query_error(L, TLV_ERR_INVALID_STATE, NULL);
    luaL_checktype(L, 2, LUA_TTABLE);
    tlv_tree_event_t event = {0};
    lua_getfield(L, 2, "kind");
    const char* kind = luaL_checkstring(L, -1);
    if (!strcmp(kind, "begin"))
        event.kind = TLV_TREE_BEGIN;
    else if (!strcmp(kind, "element"))
        event.kind = TLV_TREE_ELEMENT;
    else if (!strcmp(kind, "end"))
        event.kind = TLV_TREE_END;
    else
        return query_error(L, TLV_ERR_INVALID_ARG, NULL);
    lua_pop(L, 1);
    size_t size = 0;
    lua_getfield(L, 2, "source");
    int has_source = !lua_isnil(L, -1);
    if (has_source) {
        if (event.kind == TLV_TREE_END) return query_error(L, TLV_ERR_INVALID_ARG, NULL);
        const uint8_t*          data = (const uint8_t*)luaL_checklstring(L, -1, &size);
        size_t                  consumed = 0;
        tlv_reader_diagnostic_t detail;
        tlv_reader_diagnostic_init(&detail);
        tlv_result_t rc = tlv_read_source_diag(data, size, q->program->environment.format,
                                               &event.element, &consumed, &event.source, &detail);
        if (rc != TLV_OK) return opentlv_lua_raise_reader_error(L, rc, &detail);
        if (consumed != size) return query_error(L, TLV_ERR_INVALID_ARG, NULL);
        pin(L, q, lua_gettop(L));
    }
    lua_pop(L, 1);
    if (event.kind != TLV_TREE_END && !has_source) {
        lua_getfield(L, 2, "tag");
        const uint8_t* tag = (const uint8_t*)luaL_checklstring(L, -1, &size);
        event.element.tag = tlv_tag(tag, size);
        pin(L, q, lua_gettop(L));
        lua_pop(L, 1);
        lua_getfield(L, 2, "value");
        if (!lua_isnil(L, -1)) {
            event.element.value.data = (const uint8_t*)luaL_checklstring(L, -1, &size);
            event.element.value.size = size;
            pin(L, q, lua_gettop(L));
        }
        lua_pop(L, 1);
    }
    event.depth = opentlv_lua_query_limit(L, 2, "depth", 0);
    event.offset = opentlv_lua_query_limit(L, 2, "offset", 0);
    lua_getfield(L, 2, "skipped");
    event.skipped = lua_toboolean(L, -1);
    lua_pop(L, 1);
    int                    matched = 0;
    tlv_query_diagnostic_t diagnostic = {0};
    q->fed = 1;
    tlv_result_t rc = tlv_query_exec_feed(q->exec, &event, &matched, &diagnostic);
    if (rc != TLV_OK) return provider_error(L, q->program, rc, &diagnostic);
    if (!matched) {
        lua_pushnil(L);
        return 1;
    }
    if (q->program->info.level == TLV_QUERY_S1) {
        rc = tlv_query_exec_selected(q->exec, &event);
        if (rc != TLV_OK) return query_error(L, rc, NULL);
    }
    return push_match(L, &event);
}
static int execution_feed(lua_State* L) {
    lua_settop(L, 2);
    lua_pushcfunction(L, execution_feed_run);
    execution_t* q = execution(L);
    lua_pop(L, 1);
    q->busy = 1;
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_pushlightuserdata(L, q);
    int status = lua_pcall(L, 3, 1, 0);
    q->busy = 0;
    return status == LUA_OK ? 1 : lua_error(L);
}
static tlv_visit_result_t drain_event(const tlv_tree_event_t* event, void* context) {
    (void)event;
    (void)context;
    return TLV_VISIT_CONTINUE;
}
static int execution_finish_run(lua_State* L) {
    execution_t*           q = protected_execution(L);
    tlv_query_diagnostic_t diagnostic = {0};
    tlv_result_t           rc =
        q->has_reader ? tlv_query_program_visit(&q->reader, q->exec, drain_event, NULL, &diagnostic)
                      : tlv_query_exec_finish(q->exec, &diagnostic);
    return rc == TLV_OK ? 0 : provider_error(L, q->program, rc, &diagnostic);
}
static int execution_finish(lua_State* L) {
    return execution_call(L, execution_finish_run, 0);
}
static tlv_visit_result_t pull(const tlv_tree_event_t* event, void* context) {
    pull_t* p = context;
    p->event = *event;
    p->found = 1;
    return TLV_VISIT_STOP;
}
static int execution_next_run(lua_State* L) {
    execution_t*           q = protected_execution(L);
    tlv_query_diagnostic_t diagnostic = {0};
#if OPENTLV_DOCUMENT
    if (q->document_ref != LUA_NOREF) {
        tlv_node_t*  node = NULL;
        tlv_result_t rc = tlv_document_query_next(q->exec, &node, &diagnostic);
        if (rc == TLV_END) {
            lua_pushnil(L);
            return 1;
        }
        if (rc != TLV_OK) return query_error(L, rc, &diagnostic);
        lua_rawgeti(L, LUA_REGISTRYINDEX, q->document_ref);
        return opentlv_lua_document_push_node(L, lua_gettop(L), node);
    }
#endif
    if (q->fed && q->retained) {
        tlv_tree_event_t event;
        tlv_result_t     rc = tlv_query_result_next(q->exec, &event);
        if (rc == TLV_END) {
            lua_pushnil(L);
            return 1;
        }
        if (rc != TLV_OK) return query_error(L, rc, NULL);
        return push_match(L, &event);
    }
    if (!q->has_reader) return query_error(L, TLV_ERR_INVALID_STATE, NULL);
    pull_t       item = {0};
    tlv_result_t rc = tlv_query_program_visit(&q->reader, q->exec, pull, &item, &diagnostic);
    if (rc != TLV_OK) return provider_error(L, q->program, rc, &diagnostic);
    if (!item.found) {
        lua_pushnil(L);
        return 1;
    }
    rc = (tlv_result_t)opentlv_lua_push_element(L, &item.event.element, item.event.offset);
    if (rc != TLV_OK) return query_error(L, rc, NULL);
    field(L, "depth", item.event.depth);
    lua_pushboolean(L, item.event.kind == TLV_TREE_BEGIN);
    lua_setfield(L, -2, "constructed");
    return 1;
}
static int execution_next(lua_State* L) {
    return execution_call(L, execution_next_run, 0);
}
static int execution_ordinal_run(lua_State* L) {
    execution_t*     q = lua_touserdata(L, 1);
    tlv_tree_event_t event;
    size_t           ordinal;
    tlv_result_t     rc = tlv_query_result_next_ordinal(q->exec, &event, &ordinal);
    if (rc == TLV_END) return 0;
    if (rc != TLV_OK) return query_error(L, rc, NULL);
    push_match(L, &event);
    lua_pushnumber(L, (lua_Number)ordinal);
    return 2;
}
static int execution_next_ordinal(lua_State* L) {
    lua_settop(L, 1);
    lua_pushcfunction(L, execution_ordinal_run);
    execution_t* q = execution(L);
    lua_pop(L, 1);
#if OPENTLV_DOCUMENT
    if (q->document_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, q->document_ref);
        opentlv_lua_document_query_guard(L, lua_gettop(L), 1);
        lua_pop(L, 1);
    }
#endif
    q->busy = 1;
    lua_pushlightuserdata(L, q);
    int status = lua_pcall(L, 1, 2, 0);
    q->busy = 0;
#if OPENTLV_DOCUMENT
    if (q->document_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, q->document_ref);
        opentlv_lua_document_query_guard(L, lua_gettop(L), 0);
        lua_pop(L, 1);
    }
#endif
    return status == LUA_OK ? 2 : lua_error(L);
}
static int execution_result_run(lua_State* L) {
    execution_t*       q = protected_execution(L);
    tlv_query_result_t value;
    tlv_result_t       rc = tlv_query_exec_result(q->exec, &value);
    if (rc != TLV_OK) return query_error(L, rc, NULL);
    switch (value.kind) {
        case TLV_QUERY_RESULT_BOOL: lua_pushboolean(L, value.boolean != 0); break;
        case TLV_QUERY_RESULT_INTEGER: {
            lua_Integer integer = (lua_Integer)value.integer;
            if ((int64_t)integer != value.integer) return query_error(L, TLV_ERR_NATIVE_SIZE, NULL);
            lua_pushinteger(L, integer);
        } break;
        case TLV_QUERY_RESULT_BYTES:
        case TLV_QUERY_RESULT_STRING:
            lua_pushlstring(L, value.size ? (const char*)value.data : "", value.size);
            break;
        default: return query_error(L, TLV_ERR_INVALID_ARG, NULL);
    }
    return 1;
}
static int execution_result(lua_State* L) {
    return execution_call(L, execution_result_run, 0);
}
static int execution_exists_run(lua_State* L) {
    execution_t* q = protected_execution(L);
    if (q->fed) return query_error(L, TLV_ERR_INVALID_STATE, NULL);
    if (!q->has_reader || q->document_ref != LUA_NOREF)
        return query_error(L, TLV_ERR_INVALID_STATE, NULL);
    int                    found = 0;
    tlv_query_diagnostic_t diagnostic = {0};
    tlv_result_t           rc =
        tlv_query_program_exists(&q->reader, q->exec, lua_toboolean(L, 2), &found, &diagnostic);
    if (rc != TLV_OK) return provider_error(L, q->program, rc, &diagnostic);
    lua_pushboolean(L, found);
    return 1;
}
static int execution_exists(lua_State* L) {
    return execution_call(L, execution_exists_run, 0);
}

typedef struct {
    lua_State*              L;
    const tlv_tree_event_t* event;
    int                     invoke_index, error_index, failed;
} callback_t;

/* Match conversion and user code run under pcall. No Lua error may unwind
 * through the canonical C visitor or leave execution.busy set. */
static int invoke(lua_State* L) {
    callback_t* callback = lua_touserdata(L, 1);
    lua_pushvalue(L, 2);
    tlv_result_t rc = (tlv_result_t)opentlv_lua_push_element(L, &callback->event->element,
                                                             callback->event->offset);
    if (rc != TLV_OK) return query_error(L, rc, NULL);
    field(L, "depth", callback->event->depth);
    lua_pushboolean(L, callback->event->kind == TLV_TREE_BEGIN);
    lua_setfield(L, -2, "constructed");
    lua_call(L, 1, 1);
    return 1;
}
static tlv_visit_result_t callback_visit(const tlv_tree_event_t* event, void* context) {
    callback_t* callback = context;
    lua_State*  L = callback->L;
    callback->event = event;
    lua_pushvalue(L, callback->invoke_index);
    lua_pushlightuserdata(L, callback);
    lua_pushvalue(L, 2);
    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
        lua_replace(L, callback->error_index);
        callback->failed = 1;
        return TLV_VISIT_ERROR;
    }
    tlv_visit_result_t result =
        lua_isboolean(L, -1) && !lua_toboolean(L, -1) ? TLV_VISIT_STOP : TLV_VISIT_CONTINUE;
    lua_pop(L, 1);
    return result;
}
static int execution_visit_run(lua_State* L) {
    execution_t* q = protected_execution(L);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (!q->has_reader || q->document_ref != LUA_NOREF)
        return query_error(L, TLV_ERR_INVALID_STATE, NULL);
    lua_settop(L, 2);
    luaL_checkstack(L, 8, "Query callback stack");
    lua_pushcfunction(L, invoke);
    int invoke_index = lua_gettop(L);
    lua_pushnil(L);
    int error_index = lua_gettop(L);
    q->program->callback_error_index = error_index;
    callback_t             callback = {L, NULL, invoke_index, error_index, 0};
    tlv_query_diagnostic_t diagnostic = {0};
    tlv_result_t           rc =
        tlv_query_program_visit(&q->reader, q->exec, callback_visit, &callback, &diagnostic);
    if (callback.failed) {
        lua_pushvalue(L, error_index);
        return lua_error(L);
    }
    return rc == TLV_OK ? 0 : provider_error(L, q->program, rc, &diagnostic);
}
static int execution_visit(lua_State* L) {
    return execution_call(L, execution_visit_run, 0);
}
static int execution_info_run(lua_State* L) {
    execution_t*          q = protected_execution(L);
    tlv_query_exec_info_t info = {0};
    info.struct_size = sizeof info;
    tlv_result_t rc = tlv_query_exec_info(q->exec, &info);
    if (rc != TLV_OK) return query_error(L, rc, NULL);
    lua_newtable(L);
#define INFO(name) field(L, #name, info.name)
    INFO(elements);
    INFO(work);
    INFO(skipped_subtrees);
    INFO(full_validation);
    INFO(finished);
    INFO(invalid);
#undef INFO
    return 1;
}
static int execution_info(lua_State* L) {
    return execution_call(L, execution_info_run, 0);
}
static int execution_context_run(lua_State* L) {
    execution_t* q = protected_execution(L);
    lua_Integer  context = luaL_checkinteger(L, 2);
    if (context < 0) return query_error(L, TLV_ERR_INVALID_ARG, NULL);
    tlv_result_t rc = tlv_query_exec_context(q->exec, (size_t)context);
    return rc == TLV_OK ? 0 : query_error(L, rc, NULL);
}
static int execution_context(lua_State* L) {
    return execution_call(L, execution_context_run, 0);
}
static int execution_pruning_run(lua_State* L) {
    execution_t* q = protected_execution(L);
    tlv_result_t rc = tlv_query_exec_pruning(q->exec, lua_toboolean(L, 2));
    return rc == TLV_OK ? 0 : query_error(L, rc, NULL);
}
static int execution_pruning(lua_State* L) {
    return execution_call(L, execution_pruning_run, 0);
}
#if OPENTLV_DOCUMENT
static int execution_edit_run(lua_State* L) {
    execution_t* q = protected_execution(L);
    if (q->document_ref == LUA_NOREF) return query_error(L, TLV_ERR_INVALID_ARG, NULL);
    const char* operation = luaL_checkstring(L, 2);
    int         kind = !strcmp(operation, "remove")         ? 0
                       : !strcmp(operation, "replace")      ? 1
                       : !strcmp(operation, "insert_after") ? 2
                                                            : -1;
    if (kind < 0) return query_error(L, TLV_ERR_INVALID_ARG, NULL);
    size_t         tag_size = 0, value_size = 0;
    const uint8_t* tag =
        lua_isnoneornil(L, 3) ? NULL : (const uint8_t*)luaL_checklstring(L, 3, &tag_size);
    const uint8_t* value =
        lua_isnoneornil(L, 4) ? NULL : (const uint8_t*)luaL_checklstring(L, 4, &value_size);
    size_t capacity = q->nodes;
    if (!lua_isnoneornil(L, 5)) {
        lua_Integer bound = luaL_checkinteger(L, 5);
        if (bound < 0) return query_error(L, TLV_ERR_INVALID_ARG, NULL);
        capacity = (size_t)bound;
    }
    if (capacity > SIZE_MAX / sizeof(tlv_node_t*)) return query_error(L, TLV_ERR_OVERFLOW, NULL);
    tlv_node_t** targets = lua_newuserdata(L, capacity * sizeof *targets);
    lua_rawgeti(L, LUA_REGISTRYINDEX, q->document_ref);
    tlv_document_t*        document = opentlv_lua_document_native(L, lua_gettop(L));
    size_t                 applied = 0;
    tlv_query_diagnostic_t diagnostic = {0};
    tlv_result_t           rc = tlv_document_query_edit(
        document, q->exec, (tlv_document_query_edit_kind_t)kind, tlv_tag(tag, tag_size), value,
        value_size, targets, capacity, &applied, &diagnostic);
    if (applied) opentlv_lua_document_invalidate(L, lua_gettop(L));
    if (rc != TLV_OK) {
        opentlv_lua_push_query_error(L, rc, &diagnostic);
        field(L, "applied", applied);
        return lua_error(L);
    }
    lua_pushinteger(L, (lua_Integer)applied);
    return 1;
}
static int execution_edit(lua_State* L) {
    return execution_call(L, execution_edit_run, 0);
}
static int execution_document_run(lua_State* L) {
    execution_t* q = lua_touserdata(L, 5);
    lua_pushnil(L);
    q->program->callback_state = L;
    q->program->callback_failed = 0;
    q->program->callback_error_index = lua_gettop(L);
    tlv_document_t* document = opentlv_lua_document_native(L, 2);
    tlv_node_t*     context = opentlv_lua_node_native(L, 3, 2);
    size_t          capacity = 0;
    tlv_result_t    rc = TLV_OK;
    if (q->program->info.constructed_values_required) {
        if (lua_isnoneornil(L, 4))
            rc = tlv_document_encoded_size(document, &capacity);
        else {
            lua_Integer value = luaL_checkinteger(L, 4);
            if (value < 0) return query_error(L, TLV_ERR_INVALID_ARG, NULL);
            capacity = (size_t)value;
        }
    }
    if (rc != TLV_OK) return query_error(L, rc, NULL);
    /* Pin before allocating staging so an allocation failure requires reset
     * and cannot overwrite an earlier registry-owned Value snapshot on retry. */
    lua_pushvalue(L, 2);
    q->document_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    tlv_tree_writer_workspace_t writer = {0};
    void*                       values = NULL;
    if (q->program->info.constructed_values_required) {
        values = lua_newuserdata(L, capacity);
        q->values_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        if (q->depth == SIZE_MAX || q->depth + 1 > SIZE_MAX / sizeof *writer.frames)
            return query_error(L, TLV_ERR_OVERFLOW, NULL);
        writer.frames = lua_newuserdata(L, (q->depth + 1) * sizeof *writer.frames);
        writer.frame_capacity = q->depth + 1;
        writer.data = lua_newuserdata(L, capacity);
        writer.data_capacity = capacity;
        writer.scratch = lua_newuserdata(L, capacity);
        writer.scratch_capacity = capacity;
    }
    tlv_query_diagnostic_t diagnostic = {0};
    rc = tlv_document_query_evaluate(document, q->exec, context, values, capacity,
                                     q->program->info.constructed_values_required ? &writer : NULL,
                                     &diagnostic);
    return rc == TLV_OK ? 0 : provider_error(L, q->program, rc, &diagnostic);
}
static int execution_document(lua_State* L) {
    lua_settop(L, 4);
    lua_pushcfunction(L, execution_document_run);
    execution_t* q = execution(L);
    lua_pop(L, 1); /* execution()'s error slot; the protected call uses its own. */
    if (q->has_reader || q->document_ref != LUA_NOREF)
        return query_error(L, TLV_ERR_INVALID_STATE, NULL);
    if (!q->retained) return query_error(L, TLV_ERR_INVALID_ARG, NULL);
    opentlv_lua_document_query_guard(L, 2, 1);
    q->busy = 1;
    for (int i = 1; i <= 4; ++i) lua_pushvalue(L, i);
    lua_pushlightuserdata(L, q);
    int status = lua_pcall(L, 5, 0, 0);
    q->busy = 0;
    opentlv_lua_document_query_guard(L, 2, 0);
    return status == LUA_OK ? 0 : lua_error(L);
}
#endif
static int schema_error_projection(lua_State* L) {
    const tlv_schema_query_diagnostic_t* diagnostic = lua_touserdata(L, 1);
    tlv_result_t                         rc = (tlv_result_t)lua_tointeger(L, 2);
    opentlv_lua_push_query_error(L, rc, &diagnostic->query);
    field(L, "rule", diagnostic->rule);
    /* Only a failed assertion has Schema detail. Its common evidence (location,
     * path, expected text) is the error's own, so the table adds only that detail. */
    if (diagnostic->query.cause == TLV_QUERY_CAUSE_SCHEMA) {
        const tlv_schema_detail_t* schema = &diagnostic->query.detail.schema;
        lua_newtable(L);
        lua_pushstring(L, tlv_schema_issue_kind_string(schema->kind));
        lua_setfield(L, -2, "kind");
        lua_pushlstring(L, schema->tag.size ? (const char*)schema->tag.data : "", schema->tag.size);
        lua_setfield(L, -2, "tag");
        if (schema->field) {
            lua_pushstring(L, schema->field);
            lua_setfield(L, -2, "field");
        }
        lua_setfield(L, -2, "schema");
    }
    return 1;
}
static int query_schema_validate(lua_State* L) {
    if (provider_active(L)) return query_error(L, TLV_ERR_INVALID_STATE, NULL);
    lua_settop(L, 3);
    luaL_checktype(L, 1, LUA_TTABLE);
    size_t count;
#if LUA_VERSION_NUM >= 502
    count = lua_rawlen(L, 1);
#else
    count = lua_objlen(L, 1);
#endif
    if (count > INT_MAX / 3 || count > SIZE_MAX / sizeof(tlv_schema_query_rule_t) ||
        count > SIZE_MAX / (2 * sizeof(program_t*)))
        return query_error(L, TLV_ERR_OVERFLOW, NULL);
    size_t depth = opentlv_lua_query_limit(L, 3, "max_depth", 64);
    size_t nodes = opentlv_lua_query_limit(L, 3, "max_nodes", 1024);
    size_t work = opentlv_lua_query_limit(L, 3, "max_work", 100000000);
    size_t contexts = opentlv_lua_query_limit(L, 3, "max_contexts", nodes);
    size_t value_capacity = opentlv_lua_query_limit(L, 3, "value_capacity", SIZE_MAX);
    lua_pushnil(L);
    int                      error_index = lua_gettop(L);
    tlv_schema_query_rule_t* rules = lua_newuserdata(L, count * sizeof *rules);
    program_t**              owners = lua_newuserdata(L, 2 * count * sizeof *owners);
    lua_newtable(L);
    int                 keepers = lua_gettop(L);
    const tlv_format_t* format = NULL;
    for (size_t i = 0; i < count; ++i) {
        lua_rawgeti(L, 1, (int)i + 1);
        luaL_checktype(L, -1, LUA_TTABLE);
        int record = lua_gettop(L);
        lua_getfield(L, record, "context");
        program_t* context = program(L, -1);
        lua_rawseti(L, keepers, (int)(3 * i + 1));
        lua_getfield(L, record, "assertion");
        program_t* assertion = program(L, -1);
        lua_rawseti(L, keepers, (int)(3 * i + 2));
        lua_getfield(L, record, "name");
        size_t      length = 0;
        const char* label = "";
        if (!lua_isnil(L, -1)) {
            luaL_checktype(L, -1, LUA_TSTRING);
            label = lua_tolstring(L, -1, &length);
            if (memchr(label, 0, length)) return query_error(L, TLV_ERR_INVALID_ARG, NULL);
        }
        lua_rawseti(L, keepers, (int)(3 * i + 3));
        rules[i] = (tlv_schema_query_rule_t){context->program, assertion->program,
                                             &assertion->environment, label};
        owners[2 * i] = context;
        owners[2 * i + 1] = assertion;
        if (!format) format = context->environment.format;
        lua_pop(L, 1);
    }
    if (!format) {
        lua_getfield(L, LUA_REGISTRYINDEX, OPENTLV_LUA_DEFAULT_FORMAT_KEY);
        format = &opentlv_lua_check_format(L, -1)->format;
        lua_pop(L, 1);
    }
    size_t         size = 0;
    const uint8_t* data = NULL;
    const void*    document = NULL;
    if (lua_type(L, 2) == LUA_TSTRING)
        data = (const uint8_t*)lua_tolstring(L, 2, &size);
    else {
#if OPENTLV_DOCUMENT
        document = opentlv_lua_document_native(L, 2);
#else
        return query_error(L, TLV_ERR_UNSUPPORTED, NULL);
#endif
    }
    if (!lua_checkstack(L, 16)) return query_error(L, TLV_ERR_OUT_OF_MEMORY, NULL);
    /* Allocate the protected projection closure before acquiring the guard.
     * All later allocation, including diagnostic copying and user finalizers,
     * runs under pcall so allocation errors cannot strand an active guard. */
    lua_pushcfunction(L, schema_error_projection);
    for (size_t i = 0; i < 2 * count; ++i) {
        owners[i]->callback_state = L;
        owners[i]->callback_failed = 0;
        owners[i]->callback_error_index = error_index;
    }
#if OPENTLV_DOCUMENT
    if (document) {
        document = opentlv_lua_document_native(L, 2);
        opentlv_lua_document_query_guard(L, 2, 1);
    }
#endif
    tlv_schema_query_diagnostic_t diagnostic;
    tlv_result_t                  rc = opentlv_binding_schema_run(
        data, size, document, format, rules, count, depth, nodes, work, contexts,
        value_capacity == SIZE_MAX ? 0 : value_capacity, value_capacity == SIZE_MAX, &diagnostic);
    for (size_t i = 0; i < 2 * count; ++i) {
        if (owners[i]->callback_failed) {
#if OPENTLV_DOCUMENT
            if (document) opentlv_lua_document_query_guard(L, 2, 0);
#endif
            return provider_error(L, owners[i], rc, &diagnostic.query);
        }
    }
    if (rc != TLV_OK) {
        lua_pushlightuserdata(L, &diagnostic);
        lua_pushinteger(L, (lua_Integer)rc);
        (void)lua_pcall(L, 2, 1, 0);
#if OPENTLV_DOCUMENT
        if (document) opentlv_lua_document_query_guard(L, 2, 0);
#endif
        return lua_error(L);
    }
#if OPENTLV_DOCUMENT
    if (document) opentlv_lua_document_query_guard(L, 2, 0);
#endif
    return 0;
}
static int emv_resolve(lua_State* L) {
    size_t      space_size, name_size;
    const char* space = luaL_checklstring(L, 1, &space_size);
    const char* name = luaL_checklstring(L, 2, &name_size);
#if OPENTLV_EMV
    tlv_tag_t    tag;
    tlv_result_t rc = tlv_emv_query_resolve(NULL, space, space_size, name, name_size, &tag);
    if (rc != TLV_OK) return opentlv_lua_raise(L, rc, 0, 0);
    lua_pushlstring(L, (const char*)tag.data, tag.size);
    return 1;
#else
    return opentlv_lua_raise(L, TLV_ERR_UNSUPPORTED, 0, 0);
#endif
}
void opentlv_lua_open_program(lua_State* L, int module_index) {
    lua_pushboolean(L, 0);
    lua_setfield(L, LUA_REGISTRYINDEX, PROVIDER_ACTIVE);
    static const opentlv_lua_method_t program_methods[] = {{"info", program_info},
                                                           {"variables", program_variables},
                                                           {"image", program_image},
                                                           {"format", program_format},
                                                           {"explain", program_explain},
                                                           {"execution", program_execution},
                                                           {NULL, NULL}};
    static const opentlv_lua_method_t execution_methods[] = {
        {"close", execution_gc},
        {"reset", execution_reset},
        {"set_input", execution_input},
        {"feed", execution_feed},
        {"finish", execution_finish},
        {"bind", execution_bind},
        {"next", execution_next},
        {"next_ordinal", execution_next_ordinal},
        {"visit", execution_visit},
        {"result", execution_result},
        {"exists", execution_exists},
        {"info", execution_info},
        {"context", execution_context},
        {"pruning", execution_pruning},
#if OPENTLV_DOCUMENT
        {"evaluate_document", execution_document},
        {"edit_document", execution_edit},
#endif
        {NULL, NULL}};
    opentlv_lua_new_type(L, PROGRAM_MT, program_methods, program_gc, NULL, NULL);
    opentlv_lua_new_type(L, EXECUTION_MT, execution_methods, execution_gc, NULL, NULL);
    lua_pushcfunction(L, program_compile);
    lua_setfield(L, module_index, "query_program");
    lua_pushcfunction(L, program_load);
    lua_setfield(L, module_index, "query_program_load");
    lua_pushcfunction(L, query_schema_validate);
    lua_setfield(L, module_index, "query_schema_validate");
    lua_pushcfunction(L, definition_resolver);
    lua_setfield(L, module_index, "query_definition_resolver");
    lua_pushcfunction(L, emv_resolve);
    lua_setfield(L, module_index, "query_emv_resolve");
}
