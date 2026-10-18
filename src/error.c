// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "error.h"
#include "codec.h"

#include <stdio.h>

int opentlv_lua_raise_writer_error(lua_State* L, tlv_result_t code,
                                   const tlv_writer_diagnostic_t* diag) {
    tlv_diagnostic_t diagnostic = diag->diagnostic;
    diagnostic.code = code;
    opentlv_lua_push_diagnostic(L, &diagnostic);
    lua_pushstring(L, tlv_writer_operation_string(diag->operation));
    lua_setfield(L, -2, "operation");
    if (diag->has_tag) {
        lua_pushlstring(L, diag->tag.data ? (const char*)diag->tag.data : "", diag->tag.size);
        lua_setfield(L, -2, "tag");
    }
    if (diag->has_length) {
        opentlv_lua_codec_push_uint(L, diag->length);
        lua_setfield(L, -2, "length");
    }
    if (diag->has_required) {
        opentlv_lua_codec_push_uint(L, diag->required);
        lua_setfield(L, -2, "required");
    }
    if (diag->has_available) {
        opentlv_lua_codec_push_uint(L, diag->available);
        lua_setfield(L, -2, "available");
    }
    return lua_error(L);
}

static int error_tostring(lua_State* L) {
    /* Error objects are plain tables with this metatable attached (see
     * opentlv_lua_push_diagnostic()), not userdata, so this is reached only as
     * the __tostring metamethod itself; no argument check is needed. */
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "message");
    const char* message = lua_tostring(L, -1);
    lua_getfield(L, 1, "code");
    lua_Integer code = lua_tointeger(L, -1);
    lua_getfield(L, 1, "offset");
    int has_offset = !lua_isnil(L, -1);
    /* Large offsets are decimal strings; lua_tostring() renders both forms. */
    const char* offset = has_offset ? lua_tostring(L, -1) : NULL;

    char buffer[128];
    if (has_offset) {
        snprintf(buffer, sizeof buffer, "opentlv: %s (code %lld, offset %s)",
                 message != NULL ? message : "unknown error", (long long)code,
                 offset != NULL ? offset : "?");
    } else {
        snprintf(buffer, sizeof buffer, "opentlv: %s (code %lld)",
                 message != NULL ? message : "unknown error", (long long)code);
    }
    lua_pop(L, 3);
    lua_pushstring(L, buffer);
    return 1;
}

void opentlv_lua_open_error(lua_State* L) {
    luaL_newmetatable(L, OPENTLV_LUA_ERROR_MT);
    lua_pushcfunction(L, error_tostring);
    lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);
}

void opentlv_lua_register_error_codes(lua_State* L, int module_table_index) {
    lua_newtable(L);
    lua_pushinteger(L, TLV_OK);
    lua_setfield(L, -2, "OK");
    lua_pushinteger(L, TLV_NEED_MORE_DATA);
    lua_setfield(L, -2, "NEED_MORE_DATA");
    lua_pushinteger(L, TLV_ERR_INVALID_STATE);
    lua_setfield(L, -2, "INVALID_STATE");
    lua_pushinteger(L, TLV_ERR_CALLBACK);
    lua_setfield(L, -2, "CALLBACK");
    lua_pushinteger(L, TLV_ERR_BUFFER_TOO_SHORT);
    lua_setfield(L, -2, "BUFFER_TOO_SHORT");
    lua_pushinteger(L, TLV_ERR_INVALID_LENGTH);
    lua_setfield(L, -2, "INVALID_LENGTH");
    lua_pushinteger(L, TLV_ERR_NULL_ARG);
    lua_setfield(L, -2, "NULL_ARG");
    lua_pushinteger(L, TLV_ERR_OUT_OF_MEMORY);
    lua_setfield(L, -2, "OUT_OF_MEMORY");
    lua_pushinteger(L, TLV_END);
    lua_setfield(L, -2, "END");
    lua_pushinteger(L, TLV_ERR_TRUNCATED);
    lua_setfield(L, -2, "TRUNCATED");
    lua_pushinteger(L, TLV_ERR_INVALID_TAG);
    lua_setfield(L, -2, "INVALID_TAG");
    lua_pushinteger(L, TLV_ERR_VISITOR);
    lua_setfield(L, -2, "VISITOR");
    lua_pushinteger(L, TLV_ERR_LIMIT);
    lua_setfield(L, -2, "LIMIT");
    lua_pushinteger(L, TLV_ERR_SCHEMA);
    lua_setfield(L, -2, "SCHEMA");
    lua_pushinteger(L, TLV_ERR_INVALID_ARG);
    lua_setfield(L, -2, "INVALID_ARG");
    lua_pushinteger(L, TLV_ERR_INVALID_TAG_SIZE);
    lua_setfield(L, -2, "INVALID_TAG_SIZE");
    lua_pushinteger(L, TLV_ERR_SYNTAX);
    lua_setfield(L, -2, "SYNTAX");
    lua_pushinteger(L, TLV_ERR_OVERFLOW);
    lua_setfield(L, -2, "OVERFLOW");
    lua_pushinteger(L, TLV_ERR_INVALID_VALUE);
    lua_setfield(L, -2, "INVALID_VALUE");
    lua_pushinteger(L, TLV_ERR_UNSUPPORTED);
    lua_setfield(L, -2, "UNSUPPORTED");
    lua_pushinteger(L, TLV_ERR_NATIVE_SIZE);
    lua_setfield(L, -2, "NATIVE_SIZE");
    lua_pushinteger(L, TLV_ERR_INVALID_SCHEMA);
    lua_setfield(L, -2, "INVALID_SCHEMA");
    lua_setfield(L, module_table_index, "errors");
}

void opentlv_lua_push_error(lua_State* L, tlv_result_t code, int has_offset, size_t offset) {
    tlv_diagnostic_t diagnostic;
    tlv_diagnostic_init(&diagnostic, code, TLV_DIAGNOSTIC_SEVERITY_ERROR);
    if (has_offset)
        tlv_diagnostic_set_location(&diagnostic, TLV_LOCATION_INPUT, TLV_LOCATION_POINT, offset,
                                    offset);
    opentlv_lua_push_diagnostic(L, &diagnostic);
}

void opentlv_lua_push_reader_error(lua_State* L, tlv_result_t code,
                                   const tlv_reader_diagnostic_t* diag) {
    if (diag == NULL) {
        opentlv_lua_push_error(L, code, 0, 0);
        return;
    }
    tlv_diagnostic_t diagnostic = diag->diagnostic;
    diagnostic.code = code;
    opentlv_lua_push_diagnostic(L, &diagnostic);
    opentlv_lua_add_reader_detail(L, &diag->detail);
}

void opentlv_lua_add_reader_detail(lua_State* L, const tlv_reader_detail_t* detail) {
    lua_pushstring(L, tlv_reader_operation_string(detail->operation));
    lua_setfield(L, -2, "operation");
    if (detail->has_tag) {
        lua_pushlstring(L, (const char*)detail->tag.data, detail->tag.size);
        lua_setfield(L, -2, "tag");
    }
    if (detail->has_tag_offset) {
        opentlv_lua_codec_push_uint(L, detail->tag_offset);
        lua_setfield(L, -2, "tag_offset");
    }
    if (detail->has_length_offset) {
        opentlv_lua_codec_push_uint(L, detail->length_offset);
        lua_setfield(L, -2, "length_offset");
    }
    if (detail->has_value_offset) {
        opentlv_lua_codec_push_uint(L, detail->value_offset);
        lua_setfield(L, -2, "value_offset");
    }
    if (detail->has_declared_length) {
        opentlv_lua_codec_push_uint(L, detail->declared_length);
        lua_setfield(L, -2, "declared_length");
    }
    if (detail->has_required) {
        opentlv_lua_codec_push_uint(L, detail->required);
        lua_setfield(L, -2, "required");
    }
    if (detail->has_available) {
        opentlv_lua_codec_push_uint(L, detail->available);
        lua_setfield(L, -2, "available");
    }
    if (detail->has_enclosing_end) {
        opentlv_lua_codec_push_uint(L, detail->enclosing_end);
        lua_setfield(L, -2, "enclosing_end");
    }
    if (detail->has_raw_length) {
        lua_pushlstring(L, detail->raw_length.data ? (const char*)detail->raw_length.data : "",
                        detail->raw_length.size);
        lua_setfield(L, -2, "raw_length");
    }
}

void opentlv_lua_push_location(lua_State* L, const tlv_location_t* location) {
    lua_newtable(L);
    lua_pushstring(L, tlv_location_domain_string(location->domain));
    lua_setfield(L, -2, "domain");
    lua_pushstring(L, tlv_location_kind_string(location->kind));
    lua_setfield(L, -2, "kind");
    if (location->kind != TLV_LOCATION_UNKNOWN) {
        opentlv_lua_codec_push_uint(L, location->begin);
        lua_setfield(L, -2, "begin");
        opentlv_lua_codec_push_uint(L, location->end);
        lua_setfield(L, -2, "end");
    }
}

void opentlv_lua_push_diagnostic(lua_State* L, const tlv_diagnostic_t* diagnostic) {
    const tlv_location_t* location = &diagnostic->location;
    lua_newtable(L);
    lua_pushinteger(L, (lua_Integer)diagnostic->code);
    lua_setfield(L, -2, "code");
    lua_pushstring(L, tlv_result_string(diagnostic->code));
    lua_setfield(L, -2, "message");
    opentlv_lua_push_location(L, location);
    lua_setfield(L, -2, "location");
    if (location->kind != TLV_LOCATION_UNKNOWN) {
        opentlv_lua_codec_push_uint(L, location->begin);
        lua_setfield(L, -2, "offset");
    }
    luaL_getmetatable(L, OPENTLV_LUA_ERROR_MT);
    lua_setmetatable(L, -2);
    lua_pushstring(L, tlv_diagnostic_severity_string(diagnostic->severity));
    lua_setfield(L, -2, "severity");
    if (diagnostic->expected) {
        lua_pushstring(L, diagnostic->expected);
        lua_setfield(L, -2, "expected");
    }
    if (diagnostic->actual) {
        lua_pushstring(L, diagnostic->actual);
        lua_setfield(L, -2, "actual");
    }
    if (diagnostic->has_path) {
        lua_newtable(L);
        for (size_t i = 0; i < diagnostic->path.length; ++i) {
            tlv_tag_t tag = diagnostic->path.tags[i];
            lua_pushlstring(L, tag.data ? (const char*)tag.data : "", tag.size);
            lua_rawseti(L, -2, (int)i + 1);
        }
        lua_setfield(L, -2, "path");
        lua_pushinteger(L, (lua_Integer)diagnostic->path.omitted);
        lua_setfield(L, -2, "path_omitted");
    }
    if (diagnostic->contexts) {
        lua_newtable(L);
        int index = 1;
        for (const tlv_diagnostic_context_t* ctx = diagnostic->contexts; ctx; ctx = ctx->next) {
            lua_newtable(L);
            lua_pushstring(L, ctx->layer);
            lua_setfield(L, -2, "layer");
            lua_pushstring(L, ctx->key);
            lua_setfield(L, -2, "key");
            lua_pushstring(L, ctx->value);
            lua_setfield(L, -2, "value");
            lua_rawseti(L, -2, index++);
        }
        lua_setfield(L, -2, "contexts");
    }
}

int opentlv_lua_raise(lua_State* L, tlv_result_t code, int has_offset, size_t offset) {
    opentlv_lua_push_error(L, code, has_offset, offset);
    return lua_error(L);
}

int opentlv_lua_raise_reader_error(lua_State* L, tlv_result_t code,
                                   const tlv_reader_diagnostic_t* diag) {
    opentlv_lua_push_reader_error(L, code, diag);
    return lua_error(L);
}

void opentlv_lua_push_codec_detail(lua_State* L, const tlv_codec_detail_t* d) {
    lua_newtable(L);
    lua_pushinteger(L, d->operation);
    lua_setfield(L, -2, "operation");
    lua_pushstring(L, tlv_codec_operation_string(d->operation));
    lua_setfield(L, -2, "operation_name");
    lua_pushinteger(L, d->reported);
    lua_setfield(L, -2, "reported");
    lua_pushinteger(L, d->violation);
    lua_setfield(L, -2, "violation");
    lua_pushstring(L, tlv_codec_violation_string(d->violation));
    lua_setfield(L, -2, "violation_name");
    lua_pushinteger(L, d->reason);
    lua_setfield(L, -2, "reason");
    lua_pushstring(L, tlv_codec_reason_string(d->reason));
    lua_setfield(L, -2, "reason_name");
    lua_pushinteger(L, d->cause);
    lua_setfield(L, -2, "cause");
    lua_pushstring(L, tlv_codec_cause_string(d->cause));
    lua_setfield(L, -2, "cause_name");
    if (d->representation) {
        lua_pushstring(L, d->representation);
        lua_setfield(L, -2, "representation");
    }
    if (d->cause == TLV_CODEC_CAUSE_READER) {
        lua_newtable(L);
        opentlv_lua_add_reader_detail(L, &d->detail.reader);
        lua_setfield(L, -2, "reader");
    }
    if (d->cause == TLV_CODEC_CAUSE_SCHEMA) {
        const tlv_schema_detail_t* v = &d->detail.schema;
        lua_newtable(L);
        lua_pushnumber(L, (lua_Number)v->kind);
        lua_setfield(L, -2, "kind");
        lua_pushstring(L, tlv_schema_issue_kind_string(v->kind));
        lua_setfield(L, -2, "kind_name");
        lua_pushlstring(L, (const char*)v->tag.data, v->tag.size);
        lua_setfield(L, -2, "tag");
        lua_pushinteger(L, v->definition.kind);
        lua_setfield(L, -2, "definition_kind");
        lua_pushstring(L, tlv_schema_definition_kind_string(v->definition.kind));
        lua_setfield(L, -2, "definition_kind_name");
        lua_pushnumber(L, (lua_Number)v->definition.index);
        lua_setfield(L, -2, "definition_index");
        lua_pushstring(L, v->field);
        lua_setfield(L, -2, "field");
        lua_pushnumber(L, (lua_Number)v->is_group);
        lua_setfield(L, -2, "is_group");
        lua_pushnumber(L, (lua_Number)v->has_occurs);
        lua_setfield(L, -2, "has_occurs");
        lua_pushnumber(L, (lua_Number)v->min_occurs);
        lua_setfield(L, -2, "min_occurs");
        lua_pushnumber(L, (lua_Number)v->max_occurs);
        lua_setfield(L, -2, "max_occurs");
        lua_pushnumber(L, (lua_Number)v->occurs);
        lua_setfield(L, -2, "occurs");
        lua_pushnumber(L, (lua_Number)v->has_length);
        lua_setfield(L, -2, "has_length");
        lua_pushnumber(L, (lua_Number)v->min_length);
        lua_setfield(L, -2, "min_length");
        lua_pushnumber(L, (lua_Number)v->max_length);
        lua_setfield(L, -2, "max_length");
        lua_pushnumber(L, (lua_Number)v->actual_length);
        lua_setfield(L, -2, "actual_length");
        lua_pushnumber(L, (lua_Number)v->has_form);
        lua_setfield(L, -2, "has_form");
        lua_pushnumber(L, (lua_Number)v->expected_form);
        lua_setfield(L, -2, "expected_form");
        lua_pushnumber(L, (lua_Number)v->actual_constructed);
        lua_setfield(L, -2, "actual_constructed");
        lua_pushnumber(L, (lua_Number)v->length_multiple);
        lua_setfield(L, -2, "length_multiple");
        lua_pushnumber(L, (lua_Number)v->length_flags);
        lua_setfield(L, -2, "length_flags");
        lua_setfield(L, -2, "schema");
    }
}
