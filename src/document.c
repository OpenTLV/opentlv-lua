// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "document.h"
#include "common.h"
#include "error.h"
#include "format.h"
#include "program.h"
#include "query.h"

#include <limits.h>
#include <tlv/document/document.h>

#define DOCUMENT_MT "opentlv.Document"
#define NODE_MT "opentlv.Node"

typedef struct {
    tlv_document_t* document;
    int             format_ref;
    /* Advanced by applied Query edits, which retire every earlier handle. */
    uint64_t generation;
    int      query_active;
} document_t;

/* A borrowed node address checked against Core's never-reused identity. The
 * retirement epoch tells when the address may be stale; only then is the
 * identity looked up again. Each handle roots its Document. */
typedef struct {
    tlv_node_t* node;
    document_t* owner;
    int         owner_ref;
    uint64_t    identity, epoch, generation;
} node_handle_t;

static document_t* check_document(lua_State* L, int arg) {
    document_t* self = (document_t*)luaL_checkudata(L, arg, DOCUMENT_MT);
    if (!self->document) luaL_argerror(L, arg, "document has been released");
    return self;
}

static node_handle_t* check_node(lua_State* L, int arg) {
    node_handle_t* self = (node_handle_t*)luaL_checkudata(L, arg, NODE_MT);
    document_t*    owner = self->owner;
    if (self->node && owner && owner->document && self->generation == owner->generation) {
        uint64_t epoch = tlv_document_retire_epoch(owner->document);
        if (epoch == self->epoch) return self;
        if (tlv_document_node_identity(owner->document, self->node) == self->identity) {
            self->epoch = epoch;
            return self;
        }
    }
    self->node = NULL;
    luaL_argerror(L, arg, "node has been invalidated");
    return NULL;
}

/* Allocates and roots a handle before `node` is known, so a mutation can fill
 * it without a Lua allocation after the native edit. */
static node_handle_t* new_handle(lua_State* L, int owner_index, document_t* owner) {
    node_handle_t* self = (node_handle_t*)lua_newuserdata(L, sizeof(*self));
    *self = (node_handle_t){0};
    self->owner_ref = LUA_NOREF;
    luaL_getmetatable(L, NODE_MT);
    lua_setmetatable(L, -2);
    lua_pushvalue(L, owner_index);
    self->owner_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    self->owner = owner;
    return self;
}

static void bind_handle(node_handle_t* self, tlv_node_t* node) {
    self->node = node;
    self->identity = tlv_node_identity(node);
    self->epoch = tlv_document_retire_epoch(self->owner->document);
    self->generation = self->owner->generation;
}

static int push_node(lua_State* L, int owner_index, tlv_node_t* node) {
    if (!node) {
        lua_pushnil(L);
        return 1;
    }
    document_t* owner = check_document(L, owner_index);
    bind_handle(new_handle(L, owner_index, owner), node);
    return 1;
}

/* Pushes an owning Document for `format_index` before its native tree exists. */
static document_t* new_document(lua_State* L, int format_index) {
    document_t* self = (document_t*)lua_newuserdata(L, sizeof(*self));
    *self = (document_t){0};
    self->format_ref = LUA_NOREF;
    luaL_getmetatable(L, DOCUMENT_MT);
    lua_setmetatable(L, -2);
    lua_pushvalue(L, format_index);
    self->format_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return self;
}

tlv_document_t* opentlv_lua_document_native(lua_State* L, int index) {
    return check_document(L, index)->document;
}
tlv_document_t** opentlv_lua_document_push_owner(lua_State* L, int format_index) {
    return &new_document(L, format_index)->document;
}
void opentlv_lua_document_invalidate(lua_State* L, int index) {
    ++check_document(L, index)->generation;
}
void opentlv_lua_document_query_guard(lua_State* L, int index, int begin) {
    document_t* owner = check_document(L, index);
    owner->query_active += begin ? 1 : -1;
}
tlv_node_t* opentlv_lua_node_native(lua_State* L, int index, int document_index) {
    if (lua_isnoneornil(L, index)) return NULL;
    node_handle_t* handle = check_node(L, index);
    if (handle->owner != check_document(L, document_index))
        luaL_argerror(L, index, "node belongs to another document");
    return handle->node;
}
int opentlv_lua_document_push_node(lua_State* L, int document_index, tlv_node_t* node) {
    return push_node(L, document_index, node);
}

static int push_u64(lua_State* L, uint64_t value) {
#if LUA_VERSION_NUM >= 503
    if (value > (uint64_t)LUA_MAXINTEGER) return opentlv_lua_raise(L, TLV_ERR_NATIVE_SIZE, 0, 0);
    lua_pushinteger(L, (lua_Integer)value);
#else
    lua_Number number = (lua_Number)value;
    if ((uint64_t)number != value) return opentlv_lua_raise(L, TLV_ERR_NATIVE_SIZE, 0, 0);
    lua_pushnumber(L, number);
#endif
    return 1;
}

static int node_identity(lua_State* L) {
    return push_u64(L, tlv_node_identity(check_node(L, 1)->node));
}

static int node_source_location(lua_State* L) {
    tlv_document_source_location_t location = tlv_node_source_location(check_node(L, 1)->node);
    if (!location.has_offset && !location.has_header_size) {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);
    if (location.has_offset) {
        lua_pushinteger(L, (lua_Integer)location.offset);
        lua_setfield(L, -2, "offset");
    }
    if (location.has_header_size) {
        lua_pushinteger(L, (lua_Integer)location.header_size);
        lua_setfield(L, -2, "header_size");
    }
    return 1;
}

static int node_gc(lua_State* L) {
    node_handle_t* self = (node_handle_t*)luaL_checkudata(L, 1, NODE_MT);
    self->owner = NULL;
    self->node = NULL;
    luaL_unref(L, LUA_REGISTRYINDEX, self->owner_ref);
    self->owner_ref = LUA_NOREF;
    return 0;
}

static int document_gc(lua_State* L) {
    document_t* self = (document_t*)luaL_checkudata(L, 1, DOCUMENT_MT);
    if (self->query_active) return opentlv_lua_raise(L, TLV_ERR_INVALID_STATE, 0, 0);
    tlv_document_free(self->document);
    self->document = NULL;
    luaL_unref(L, LUA_REGISTRYINDEX, self->format_ref);
    self->format_ref = LUA_NOREF;
    return 0;
}

static int document_new(lua_State* L) {
    size_t         length = 0;
    const uint8_t* data = NULL;
    if (!lua_isnoneornil(L, 1)) data = (const uint8_t*)luaL_checklstring(L, 1, &length);
    lua_settop(L, 3);
    if (lua_isnil(L, 2)) {
        lua_getfield(L, LUA_REGISTRYINDEX, OPENTLV_LUA_DEFAULT_FORMAT_KEY);
        lua_replace(L, 2);
    }
    tlv_lua_format_t*      format = opentlv_lua_check_format(L, 2);
    tlv_document_options_t options;
    tlv_result_t           code = tlv_document_options_init(&options, &format->format);
    if (code != TLV_OK) return opentlv_lua_raise(L, code, 0, 0);
    options.max_depth = opentlv_lua_query_limit(L, 3, "max_depth", options.max_depth);
    options.max_elements = opentlv_lua_query_limit(L, 3, "max_elements", options.max_elements);
    if (lua_istable(L, 3)) {
        lua_getfield(L, 3, "retain_source_locations");
        if (!lua_isnil(L, -1)) luaL_checktype(L, -1, LUA_TBOOLEAN);
        options.retain_source_locations = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    document_t*             self = new_document(L, 2);
    tlv_reader_diagnostic_t offset = {0};
    code = data ? tlv_document_parse(data, length, &options, &self->document, &offset)
                : tlv_document_create(&options, &self->document);
    if (code != TLV_OK) return opentlv_lua_raise_reader_error(L, code, &offset);
    return 1;
}

static tlv_node_t* find_path(lua_State* L, document_t* self, int arg) {
    tlv_query_t        scratch;
    const tlv_query_t* query = opentlv_lua_check_query(L, arg, &scratch);
    tlv_node_t*        node = NULL;
    tlv_result_t       code = tlv_document_find_path(self->document, query, &node);
    if (code != TLV_OK) {
        opentlv_lua_raise(L, code, 0, 0);
        return NULL;
    }
    return node;
}

/* Mutations accept either a path/Query or a live node owned by this document. */
static tlv_node_t* resolve_node(lua_State* L, document_t* self, int arg) {
    if (lua_type(L, arg) == LUA_TUSERDATA && lua_getmetatable(L, arg)) {
        luaL_getmetatable(L, NODE_MT);
        int is_node = lua_rawequal(L, -1, -2);
        lua_pop(L, 2);
        if (is_node) {
            node_handle_t* node = check_node(L, arg);
            if (node->owner != self) luaL_argerror(L, arg, "node belongs to another document");
            return node->node;
        }
    }
    return find_path(L, self, arg);
}

static int document_find(lua_State* L) {
    document_t* self = check_document(L, 1);
    return push_node(L, 1, find_path(L, self, 2));
}

/* find_child(binary_tag, parent?) */
static int document_find_child(lua_State* L) {
    document_t*    self = check_document(L, 1);
    size_t         size;
    const uint8_t* tag = (const uint8_t*)luaL_checklstring(L, 2, &size);
    tlv_node_t*    parent = NULL;
    if (!lua_isnoneornil(L, 3)) {
        parent = resolve_node(L, self, 3);
        if (!parent) return luaL_argerror(L, 3, "parent was not found");
    }
    return push_node(L, 1, tlv_document_find(self->document, parent, tlv_tag(tag, size)));
}

static int document_first(lua_State* L) {
    return push_node(L, 1, tlv_document_first(check_document(L, 1)->document));
}

static int document_count(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)tlv_document_count(check_document(L, 1)->document));
    return 1;
}

static int document_revision(lua_State* L) {
    return push_u64(L, tlv_document_revision(check_document(L, 1)->document));
}

typedef struct {
    lua_State*  L;
    tlv_node_t* node;
    int         invoke, document, target, error, failed, stopped, count;
} visit_t;

/* Node projection and the callback (or result append) run under pcall: no Lua
 * error unwinds through tlv_document_query_visit(). Arguments are the visit,
 * the Document and the callback or result table. */
static int visit_invoke(lua_State* L) {
    visit_t* visit = (visit_t*)lua_touserdata(L, 1);
    if (lua_isfunction(L, 3)) {
        lua_pushvalue(L, 3);
        push_node(L, 2, visit->node);
        lua_call(L, 1, 1);
        return 1;
    }
    if (visit->count == INT_MAX) return opentlv_lua_raise(L, TLV_ERR_LIMIT, 0, 0);
    push_node(L, 2, visit->node);
    lua_rawseti(L, 3, visit->count + 1);
    return 0;
}

static tlv_visit_result_t visit_node(tlv_node_t* node, void* context) {
    visit_t*   visit = (visit_t*)context;
    lua_State* L = visit->L;
    visit->node = node;
    lua_pushvalue(L, visit->invoke);
    lua_pushlightuserdata(L, visit);
    lua_pushvalue(L, visit->document);
    lua_pushvalue(L, visit->target);
    if (lua_pcall(L, 3, 1, 0) != LUA_OK) {
        lua_replace(L, visit->error);
        visit->failed = 1;
        return TLV_VISIT_ERROR;
    }
    ++visit->count;
    visit->stopped = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
    lua_pop(L, 1);
    return visit->stopped ? TLV_VISIT_STOP : TLV_VISIT_CONTINUE;
}

/* Visits path matches through tlv_document_query_visit(); `target` is a callback
 * or a result table. Mutation and release are rejected while it runs. */
static visit_t visit_matches(lua_State* L, document_t* self, int query_arg, int target) {
    tlv_query_t        scratch;
    const tlv_query_t* query = opentlv_lua_check_query(L, query_arg, &scratch);
    luaL_checkstack(L, 8, "Document visit stack");
    visit_t visit = {L, NULL, 0, 1, target, 0, 0, 0, 0};
    lua_pushcfunction(L, visit_invoke);
    visit.invoke = lua_gettop(L);
    lua_pushnil(L);
    visit.error = lua_gettop(L);
    tlv_query_diagnostic_t diagnostic;
    ++self->query_active;
    tlv_result_t code =
        tlv_document_query_visit(self->document, query, visit_node, &visit, &diagnostic);
    --self->query_active;
    if (visit.failed) {
        lua_pushvalue(L, visit.error);
        lua_error(L);
    }
    if (code != TLV_OK) {
        opentlv_lua_push_query_error(L, code, &diagnostic);
        lua_error(L);
    }
    return visit;
}

static int document_query(lua_State* L) {
    document_t* self = check_document(L, 1);
    lua_settop(L, 2);
    lua_newtable(L);
    visit_matches(L, self, 2, 3);
    lua_settop(L, 3);
    return 1;
}

/* visit(path_or_query, callback) -> visited, stopped */
static int document_visit(lua_State* L) {
    document_t* self = check_document(L, 1);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    lua_settop(L, 3);
    visit_t visit = visit_matches(L, self, 2, 3);
    lua_pushinteger(L, visit.count);
    lua_pushboolean(L, visit.stopped);
    return 2;
}

static int set_value(lua_State* L, document_t* owner, tlv_node_t* node, int arg) {
    if (owner->query_active) return opentlv_lua_raise(L, TLV_ERR_INVALID_STATE, 0, 0);
    size_t         length;
    const uint8_t* value = (const uint8_t*)luaL_checklstring(L, arg, &length);
    tlv_result_t   code = tlv_node_set_value(node, value, length);
    if (code != TLV_OK) return opentlv_lua_raise(L, code, 0, 0);
    lua_pushboolean(L, 1);
    return 1;
}

static int document_set(lua_State* L) {
    document_t* self = check_document(L, 1);
    tlv_node_t* node = resolve_node(L, self, 2);
    if (!node) {
        lua_pushboolean(L, 0);
        return 1;
    }
    return set_value(L, self, node, 3);
}

static int document_erase(lua_State* L) {
    document_t* self = check_document(L, 1);
    if (self->query_active) return opentlv_lua_raise(L, TLV_ERR_INVALID_STATE, 0, 0);
    tlv_node_t* node = resolve_node(L, self, 2);
    if (!node) {
        lua_pushboolean(L, 0);
        return 1;
    }
    tlv_node_erase(node);
    lua_pushboolean(L, 1);
    return 1;
}

/* insert(binary_tag, value, parent?, before?) */
static int document_insert(lua_State* L) {
    document_t* self = check_document(L, 1);
    if (self->query_active) return opentlv_lua_raise(L, TLV_ERR_INVALID_STATE, 0, 0);
    size_t         tag_size, length;
    const uint8_t* tag_data = (const uint8_t*)luaL_checklstring(L, 2, &tag_size);
    const uint8_t* value = (const uint8_t*)luaL_checklstring(L, 3, &length);
    tlv_node_t*    parent = NULL;
    tlv_node_t*    before = NULL;
    if (!lua_isnoneornil(L, 4)) {
        parent = resolve_node(L, self, 4);
        if (!parent) return luaL_argerror(L, 4, "parent was not found");
    }
    if (!lua_isnoneornil(L, 5)) {
        before = resolve_node(L, self, 5);
        if (!before) return luaL_argerror(L, 5, "insertion point was not found");
    }
    /* Prepare the returned handle before mutation, including its Lua root. */
    lua_settop(L, 5);
    node_handle_t* handle = new_handle(L, 1, self);
    tlv_tag_t      tag = {tag_data, tag_size};
    tlv_node_t*    node = NULL;
    tlv_result_t   code =
        tlv_document_insert(self->document, parent, before, tag, value, length, &node);
    if (code != TLV_OK) return opentlv_lua_raise(L, code, 0, 0);
    bind_handle(handle, node);
    return 1;
}

static int serialize(lua_State* L, const tlv_document_t* document, const tlv_node_t* node) {
    const tlv_format_t* format = NULL;
    if (!lua_isnoneornil(L, 2)) format = &opentlv_lua_check_format(L, 2)->format;
    size_t       size = 0, written = 0;
    tlv_result_t code;
    if (node)
        code = format ? tlv_node_encoded_size_as(node, format, &size)
                      : tlv_node_encoded_size(node, &size);
    else
        code = format ? tlv_document_encoded_size_as(document, format, &size)
                      : tlv_document_encoded_size(document, &size);
    if (code != TLV_OK) return opentlv_lua_raise(L, code, 0, 0);
    uint8_t* buffer = (uint8_t*)lua_newuserdata(L, size);
    if (node)
        code = format ? tlv_node_encode_as(node, format, buffer, size, &written)
                      : tlv_node_encode(node, buffer, size, &written);
    else
        code = format ? tlv_document_encode_as(document, format, buffer, size, &written)
                      : tlv_document_encode(document, buffer, size, &written);
    if (code != TLV_OK) return opentlv_lua_raise(L, code, 0, 0);
    lua_pushlstring(L, (const char*)buffer, written);
    return 1;
}

static int document_serialize(lua_State* L) {
    return serialize(L, check_document(L, 1)->document, NULL);
}
static int node_serialize(lua_State* L) {
    return serialize(L, NULL, check_node(L, 1)->node);
}
static int node_tag(lua_State* L) {
    tlv_tag_t tag = tlv_node_tag(check_node(L, 1)->node);
    lua_pushlstring(L, (const char*)tag.data, tag.size);
    return 1;
}
static int node_value(lua_State* L) {
    tlv_node_t* node = check_node(L, 1)->node;
    if (tlv_node_is_constructed(node)) {
        lua_pushnil(L);
        return 1;
    }
    size_t size = tlv_node_value_size(node);
    lua_pushlstring(L, size ? (const char*)tlv_node_value_data(node) : "", size);
    return 1;
}
static int node_constructed(lua_State* L) {
    lua_pushboolean(L, tlv_node_is_constructed(check_node(L, 1)->node));
    return 1;
}
static int node_navigate(lua_State* L, int direction) {
    node_handle_t* self = check_node(L, 1);
    tlv_node_t*    node = direction == 0   ? tlv_node_first_child(self->node)
                          : direction == 1 ? tlv_node_next(self->node)
                          : direction == 2 ? tlv_node_parent(self->node)
                                           : tlv_node_next_same_tag(self->node);
    lua_rawgeti(L, LUA_REGISTRYINDEX, self->owner_ref);
    return push_node(L, lua_gettop(L), node);
}
static int node_first_child(lua_State* L) {
    return node_navigate(L, 0);
}
static int node_next(lua_State* L) {
    return node_navigate(L, 1);
}
static int node_parent(lua_State* L) {
    return node_navigate(L, 2);
}
static int node_next_same_tag(lua_State* L) {
    return node_navigate(L, 3);
}
static int node_set(lua_State* L) {
    node_handle_t* self = check_node(L, 1);
    return set_value(L, self->owner, self->node, 2);
}
static int node_erase(lua_State* L) {
    node_handle_t* self = check_node(L, 1);
    if (self->owner->query_active) return opentlv_lua_raise(L, TLV_ERR_INVALID_STATE, 0, 0);
    tlv_node_erase(self->node);
    return 0;
}

void opentlv_lua_open_document(lua_State* L, int module_index) {
    static const opentlv_lua_method_t document_methods[] = {{"close", document_gc},
                                                            {"find", document_find},
                                                            {"find_child", document_find_child},
                                                            {"query", document_query},
                                                            {"visit", document_visit},
                                                            {"first", document_first},
                                                            {"count", document_count},
                                                            {"revision", document_revision},
                                                            {"set", document_set},
                                                            {"insert", document_insert},
                                                            {"erase", document_erase},
                                                            {"serialize", document_serialize},
                                                            {NULL, NULL}};
    static const opentlv_lua_method_t node_methods[] = {{"tag", node_tag},
                                                        {"identity", node_identity},
                                                        {"source_location", node_source_location},
                                                        {"value", node_value},
                                                        {"is_constructed", node_constructed},
                                                        {"first_child", node_first_child},
                                                        {"next", node_next},
                                                        {"parent", node_parent},
                                                        {"next_same_tag", node_next_same_tag},
                                                        {"set", node_set},
                                                        {"erase", node_erase},
                                                        {"serialize", node_serialize},
                                                        {NULL, NULL}};
    opentlv_lua_new_type(L, DOCUMENT_MT, document_methods, document_gc, NULL, NULL);
    opentlv_lua_new_type(L, NODE_MT, node_methods, node_gc, NULL, NULL);
    lua_pushcfunction(L, document_new);
    lua_setfield(L, module_index, "document");
}
