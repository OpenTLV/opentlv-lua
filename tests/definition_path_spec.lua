-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

-- Definition registries and diagnostic path text against the pinned Core.
-- Core owns lookup and path formatting; these cases check the binding's
-- snapshot, ownership and diagnostic contract.
local tlv = require("opentlv")
local describe = describe or function(_, run) run() end
local it = it or function(_, run) run() end
local b = string.char

local inject_failure
if arg and arg[1] == "--allocator" then
    inject_failure = require("opentlv_test_allocator")
else
    local ok, module = pcall(require, "opentlv_test_allocator")
    if ok then inject_failure = module end
end

local function failure(run, code)
    local ok, err = pcall(run)
    assert(not ok, "expected failure")
    if code then assert(type(err) == "table" and err.code == code, tostring(err)) end
    return err
end

describe("Definition registry", function()
    it("finds the first entry with an equal Tag", function()
        local registry = tlv.definition_registry {
            {tag = b(0x5A), name = "PAN"},
            {tag = b(0x9F, 0x02), name = "amount"},
            {tag = b(0x5A), name = "duplicate"},
            {tag = b(0x9F)},
        }
        local found, index = registry:find(b(0x5A))
        assert(found.tag == b(0x5A) and found.name == "PAN" and index == 1)
        found, index = registry:find(b(0x9F, 0x02))
        assert(found.name == "amount" and index == 2)
        -- An unnamed entry has no name; a prefix or longer Tag is a different identifier.
        found, index = registry:find(b(0x9F))
        assert(found.tag == b(0x9F) and found.name == nil and index == 4)
        assert(registry:find(b(0x9F, 0x02, 0x00)) == nil and registry:find(b(0x01)) == nil)
        assert(registry:find("") == nil)
        assert(tlv.definition_registry {}:find(b(0x5A)) == nil)
    end)

    it("compares empty Tags as equal", function()
        local registry = tlv.definition_registry {{tag = b(1)}, {tag = "", name = "empty"}}
        local found, index = registry:find("")
        assert(found.tag == "" and found.name == "empty" and index == 2)
    end)

    it("snapshots entries and returns owned copies", function()
        local entries = {{tag = b(1), name = "one"}, {tag = b(2), name = "two"}}
        local registry = tlv.definition_registry(entries)
        entries[1].tag, entries[1].name, entries[2] = b(9), "changed", nil
        entries = nil
        for _ = 1, 3 do collectgarbage("collect") end
        local found = registry:find(b(1))
        assert(found.name == "one" and registry:find(b(9)) == nil)
        found.name = "mutated"
        assert(registry:find(b(1)).name == "one")
        local all = registry:definitions()
        assert(#all == 2 and all[1].tag == b(1) and all[1].name == "one")
        assert(all[2].tag == b(2) and all[2].name == "two")
        all[2].tag = b(3)
        assert(registry:find(b(2)).name == "two" and #registry:definitions() == 2)
        assert(#tlv.definition_registry {}:definitions() == 0)
    end)

    it("rejects malformed definitions", function()
        failure(function() tlv.definition_registry() end)
        failure(function() tlv.definition_registry {b(1)} end)
        failure(function() tlv.definition_registry {{}} end)
        failure(function() tlv.definition_registry {{tag = 1}} end)
        failure(function() tlv.definition_registry {{tag = b(1), name = false}} end)
        -- Core names are NUL-terminated.
        failure(function() tlv.definition_registry {{tag = b(1), name = "a\0b"}} end,
                tlv.errors.INVALID_ARG)
        local registry = tlv.definition_registry {{tag = b(1)}}
        failure(function() registry:find() end)
        failure(function() registry.find({}, b(1)) end)
    end)
end)

describe("Diagnostic path text", function()
    if tlv.formats.ber then
        local child = tlv.schema {rules = {{tag = b(4), min_occurs = 1}}}
        local schema = tlv.schema {rules = {{tag = b(0x30), kind = "constructed",
            children = tlv.schema {rules = {{tag = b(0xA5), kind = "constructed",
                                             children = child}}}}}}

        it("formats enclosing Tags through Core", function()
            local result = schema:validate(b(0x30, 0x02, 0xA5, 0x00))
            local d = result.diagnostics[1]
            assert(d.kind == "missing" and d.tag == b(4))
            assert(#d.path == 2 and d.path_omitted == 0 and d.path_text == "30 > A5")
            -- The text is copied with the table and survives the input and Schema.
            schema, result = nil, nil
            for _ = 1, 3 do collectgarbage("collect") end
            assert(d.path_text == "30 > A5")
        end)

        it("marks omitted Tags in Query Schema failures", function()
            local f = tlv.formats.ber
            local rules = {{context = tlv.query_program("//5A", f),
                            assertion = tlv.query_program("num(.) = 1", f), name = "one"}}
            local bad = failure(function()
                tlv.query_schema_validate(rules, b(0x70, 3, 0x5A, 1, 2))
            end, tlv.errors.SCHEMA)
            assert(bad.path_text == "70")
            local deep = b(0x5A, 1, 2)
            for level = 1, 35 do deep = b(level == 35 and 0x70 or 0x30, #deep) .. deep end
            local truncated = failure(function() tlv.query_schema_validate(rules, deep) end,
                                      tlv.errors.SCHEMA)
            assert(#truncated.path == 32 and truncated.path_omitted == 3)
            assert(truncated.path_text == "70" .. string.rep(" > 30", 31) .. " > ...")
        end)
    end

    it("is empty for a tracked root path and absent without a path", function()
        local fixed = tlv.formats.fixed(1, 1, "big")
        local schema = tlv.schema {rules = {{tag = b(1), min_occurs = 1}}}
        local missing = schema:validate("", fixed).diagnostics[1]
        assert(#missing.path == 0 and missing.path_text == "")
        local err = failure(function() tlv.reader(b(1, 5), fixed):next() end)
        assert(err.path == nil and err.path_text == nil)
    end)
end)

if inject_failure then
    describe("allocation failures", function()
        it("leave registries and diagnostics usable", function()
            local registry = tlv.definition_registry {{tag = b(1), name = "one"}}
            local schema = tlv.schema {rules = {{tag = b(1), min_occurs = 1}}}
            local fixed = tlv.formats.fixed(1, 1, "big")
            local failures, successes = 0, 0
            for budget = 0, 150 do
                local ok = inject_failure(function()
                    local created = tlv.definition_registry {{tag = b(2), name = "two"},
                                                             {tag = b(3)}}
                    assert(created:find(b(2)).name == "two" and #created:definitions() == 2)
                    assert(registry:find(b(1)).name == "one")
                    assert(schema:validate("", fixed).diagnostics[1].path_text == "")
                end, budget)
                if ok then successes = successes + 1 else failures = failures + 1 end
                for _ = 1, 3 do collectgarbage("collect") end
            end
            assert(failures > 0 and successes > 0)
            assert(registry:find(b(1)).name == "one")
        end)
    end)
end
