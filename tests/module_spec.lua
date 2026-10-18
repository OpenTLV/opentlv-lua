-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

-- Minimal module-loading test: require("opentlv") must succeed (resolving
-- through lua/opentlv/init.lua to the native opentlv._core module, see
-- src/module.c) and expose the top-level surface every other spec file
-- exercises in depth. Detailed behavior belongs in
-- format_spec/reader_spec/visitor_spec/error_spec, not here.

describe("require(\"opentlv\")", function()
    it("loads successfully and returns a table", function()
        local opentlv = require("opentlv")
        assert(type(opentlv) == "table")
    end)

    it("exposes the top-level module surface", function()
        local opentlv = require("opentlv")
        assert(type(opentlv.reader) == "function")
        assert(type(opentlv.schema) == "function")
        assert(type(opentlv.visit_tree) == "function")
        assert(type(opentlv.version) == "function")
        assert(type(opentlv.strerror) == "function")
        assert(type(opentlv.formats) == "table")
        assert(type(opentlv.errors) == "table")
        assert(type(opentlv._VERSION) == "string")
    end)

    it("returns the same module table on every require()", function()
        assert(require("opentlv") == require("opentlv"))
        assert(require("opentlv") == require("opentlv._core"))
    end)
end)
