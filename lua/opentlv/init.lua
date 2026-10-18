-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

-- Pure-Lua entry point for require("opentlv"). Every OpenTLV concept
-- (Reader, Writer, Tree Writer, Schema, Element, Tag, traversal, errors) is bound in the
-- native opentlv._core module (bindings/lua/src/); this file only
-- re-exports it under the name callers request, the same native/pure split
-- the Python opentlv-core/opentlv and Rust opentlv-sys/opentlv
-- packages use. It stays this thin on purpose: functionality useful outside
-- Lua belongs in the OpenTLV C API, not in this binding (see
-- docs/development/lua.md).
return require("opentlv._core")
