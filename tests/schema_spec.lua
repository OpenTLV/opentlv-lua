-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

local tlv = require("opentlv")
local describe = describe or function(_, run) run() end
local it = it or function(_, run) run() end
local bytes = string.char
local fixed = tlv.formats.fixed(1, 1, "big")

local function failure(run)
    local ok, err = pcall(run)
    assert(not ok, "expected argument error")
    return err
end

local function find(result, kind, tag)
    for _, diagnostic in ipairs(result.diagnostics) do
        if diagnostic.kind == kind and (tag == nil or diagnostic.tag == tag) then
            return diagnostic
        end
    end
    error("missing diagnostic: " .. kind)
end

describe("Lua Schema", function()
    it("returns successful results, including empty schemas and binary tags", function()
        local schema = tlv.schema {rules = {{tag = bytes(0), min_length = 1, max_length = 1}}}
        local result = schema:validate(bytes(0, 1, 255), fixed)
        assert(result.ok and result.code == tlv.errors.OK)
        assert(result.total_count == 0 and #result.diagnostics == 0 and not result.truncated)
        assert(tlv.schema {}:validate("", fixed).ok)
        local two = tlv.schema {rules = {{tag = bytes(0, 255), min_occurs = 1}}}
        assert(two:validate(bytes(0, 255, 0), tlv.formats.fixed(2, 1, "big")).ok)
    end)

    it("copies multiple native diagnostics and bounds their stored prefix", function()
        local schema = tlv.schema {rules = {
            {tag = bytes(1), name = "required", min_occurs = 1, max_occurs = 1},
            {tag = bytes(2), min_length = 2, max_length = 4, length_multiple = 2},
        }}
        local data = bytes(2, 1, 255, 3, 0)
        local result = schema:validate(data, fixed)
        assert(not result.ok and result.code == tlv.errors.SCHEMA)
        assert(result.total_count == 3 and #result.diagnostics == 3 and not result.truncated)
        local missing = find(result, "missing")
        assert(missing.code == tlv.errors.SCHEMA and missing.offset == #data and missing.location.kind == "scope_end")
        assert(missing.tag == bytes(1) and missing.field == "required")
        assert(missing.severity == "error" and missing.message == tlv.strerror(missing.code))
        assert(#missing.path == 0 and not missing.is_group)
        assert(missing.occurrences.minimum == 1 and missing.occurrences.maximum == 1)
        assert(missing.occurrences.actual == 0 and missing.length == nil and missing.form == nil)
        assert(missing.expected == nil and missing.actual == nil and missing.contexts == nil)
        local length = find(result, "length")
        assert(length.code == tlv.errors.SCHEMA and length.offset == 0)
        assert(length.length.minimum == 2 and length.length.maximum == 4 and length.length.actual == 1)
        assert(length.length_multiple == 2 and length.length_flags == 0)
        local unexpected = find(result, "unexpected")
        assert(unexpected.tag == bytes(3) and unexpected.offset == 3 and unexpected.field == nil)
        for _, capacity in ipairs({0, 1, 2, 3, 10}) do
            local limited = schema:validate(data, fixed, {capacity = capacity})
            assert(not limited.ok and limited.total_count == 3)
            assert(#limited.diagnostics == math.min(capacity, 3))
            assert(limited.truncated == (capacity < 3))
        end
    end)

    it("supports length endpoint flags, multiples and unrestricted bounds", function()
        local schema = tlv.schema {rules = {{tag = bytes(1), min_length = 2,
            max_length = 4, flags = tlv.SCHEMA_LENGTH_ENDPOINTS, length_multiple = 2}}}
        for _, n in ipairs({2, 4}) do
            assert(schema:validate(bytes(1, n) .. string.rep("a", n), fixed).ok)
        end
        local d = find(schema:validate(bytes(1, 3) .. "abc", fixed), "length")
        assert(d.length_flags == tlv.SCHEMA_LENGTH_ENDPOINTS and d.length_multiple == 2)
        local multiple = tlv.schema {rules = {{tag = bytes(1), length_multiple = 2}}}
        d = find(multiple:validate(bytes(1, 1, 0), fixed), "length")
        assert(d.length.maximum == math.huge and d.length.actual == 1)
        assert(multiple:validate(bytes(1, 0), fixed).ok)
        assert(tlv.schema {rules = {{tag = bytes(1), max_occurs = math.huge,
            max_length = math.huge}}}:validate(bytes(1, 0, 1, 0), fixed).ok)
    end)

    it("reports duplicates and enforces ordering and alternative groups", function()
        local schema = tlv.schema {order = "sequence", rules = {
            {tag = bytes(1), max_occurs = 1}, {tag = bytes(2)},
        }}
        local result = schema:validate(bytes(2, 0, 1, 0, 1, 0), fixed)
        local duplicate = find(result, "duplicate")
        assert(duplicate.offset == 4 and duplicate.occurrences.actual == 2)
        assert(duplicate.occurrences.maximum == 1)
        assert(find(result, "order").offset == 2)
        local choice = tlv.schema {groups = {{id = 7, name = "choice", min_occurs = 1, max_occurs = 1}},
            rules = {{tag = bytes(1), group = 7}, {tag = bytes(2), group = 7}}}
        assert(choice:validate(bytes(2, 0), fixed).ok)
        local missing = find(choice:validate("", fixed), "missing")
        assert(missing.is_group and missing.field == "choice" and missing.occurrences.actual == 0)
        duplicate = find(choice:validate(bytes(1, 0, 2, 0), fixed), "duplicate")
        assert(duplicate.is_group and duplicate.tag == bytes(2) and duplicate.offset == 2)
    end)

    it("applies the C unknown-tag policy", function()
        local strict = tlv.schema {}
        local permissive = tlv.schema {allow_unknown = true}
        assert(not strict:validate(bytes(1, 0), fixed).ok)
        assert(strict:validate(bytes(1, 0), fixed, {unknown = "allow"}).ok)
        assert(permissive:validate(bytes(1, 0), fixed).ok)
        assert(not permissive:validate(bytes(1, 0), fixed, {unknown = "reject"}).ok)
    end)

    it("returns fatal status and native offsets without invented schema details", function()
        local schema = tlv.schema {rules = {{tag = bytes(1), min_occurs = 1}}}
        local result = schema:validate(bytes(2, 0, 3, 2, 0), fixed, {capacity = 0})
        assert(not result.ok and result.code == tlv.errors.TRUNCATED)
        assert(result.total_count == 1 and #result.diagnostics == 1 and not result.truncated)
        local diagnostic = result.diagnostics[1]
        assert(diagnostic.code == result.code and diagnostic.offset ~= nil)
        assert(diagnostic.tag == nil and diagnostic.path == nil and diagnostic.kind == nil)
        assert(diagnostic.severity == "error")
        result = schema:validate(bytes(1, 0), fixed, {max_elements = 0})
        assert(result.code == tlv.errors.LIMIT and result.diagnostics[1].location.kind == "unknown")
        assert(tlv.schema {}:validate("", fixed, {max_elements = 0, max_depth = 0}).ok)
    end)

    it("leaves invalid schema rule validation to C", function()
        local configs = {
            {rules = {{tag = bytes(1), min_length = 2, max_length = 1}}},
            {rules = {{tag = bytes(1), min_occurs = 2, max_occurs = 1}}},
            {rules = {{tag = bytes(1)}, {tag = bytes(1)}}},
            {rules = {{tag = bytes(1), group = 7}}},
            {groups = {{id = 0}}},
            {rules = {{tag = bytes(1), children = tlv.schema {}}}},
        }
        for _, config in ipairs(configs) do
            local result = tlv.schema(config):validate("", fixed)
            assert(not result.ok and result.code == tlv.errors.INVALID_SCHEMA)
            assert(result.diagnostics[1].offset == nil)
        end
    end)

    it("checks Lua types, enum values and numeric narrowing", function()
        failure(function() tlv.schema() end)
        failure(function() tlv.schema {rules = false} end)
        failure(function() tlv.schema {rules = {{tag = 1}}} end)
        failure(function() tlv.schema {rules = {{tag = bytes(1), name = "a\0b"}}} end)
        failure(function() tlv.schema {allow_unknown = 1} end)
        failure(function() tlv.schema {order = "wrong"} end)
        failure(function() tlv.schema {rules = {{tag = bytes(1), kind = "wrong"}}} end)
        failure(function() tlv.schema {rules = {{tag = bytes(1), children = {}}}} end)
        for _, key in ipairs({"min_length", "max_length", "min_occurs", "max_occurs",
                              "length_multiple", "flags", "group"}) do
            for _, value in ipairs({-1, 1.5, 0/0, "2", false}) do
                failure(function() tlv.schema {rules = {{tag = bytes(1), [key] = value}}} end)
            end
        end
        failure(function() tlv.schema {groups = {{id = 2^32}}} end)
        local schema = tlv.schema {}
        for _, key in ipairs({"capacity", "max_depth", "max_elements"}) do
            for _, value in ipairs({-1, 1.5, 0/0, "2", false}) do
                failure(function() schema:validate("", fixed, {[key] = value}) end)
            end
        end
        failure(function() schema:validate("", fixed, {capacity = math.huge}) end)
        failure(function() schema:validate("", fixed, {capacity = 2^62}) end)
        failure(function() schema:validate("", fixed, {unknown = "wrong"}) end)
        failure(function() schema:validate(1, fixed) end)
        failure(function() schema:validate("", {}) end)
        failure(function() schema:validate("", fixed, false) end)
        collectgarbage("collect")
    end)

    it("snapshots rule tables and returns diagnostics that survive collection", function()
        local rules = {{tag = bytes(1), name = string.rep("field", 20), min_occurs = 1}}
        local config = {rules = rules}
        local schema = tlv.schema(config)
        rules[1].tag, rules[1].name, rules[1].min_occurs = bytes(2), "changed", 0
        config.rules = {}
        collectgarbage("collect")
        local result = schema:validate("", fixed)
        schema, config, rules = nil, nil, nil
        collectgarbage("collect")
        local d = find(result, "missing")
        assert(d.tag == bytes(1) and d.field == string.rep("field", 20))
    end)

    if tlv.formats.ber then
        it("retains child schemas and reports paths and scope-end missing offsets", function()
            local weak = setmetatable({}, {__mode = "v"})
            local function create()
                local child = tlv.schema {rules = {{tag = bytes(4), min_occurs = 1}}}
                weak[1] = child
                return tlv.schema {rules = {{tag = bytes(48), kind = "constructed", children = child},
                    {tag = bytes(4)}}}
            end
            local parent = create()
            collectgarbage("collect")
            assert(weak[1] ~= nil)
            assert(parent:validate(bytes(48, 2, 4, 0)).ok)
            local result = parent:validate(bytes(48, 0, 4, 0))
            parent = nil
            for _ = 1, 3 do collectgarbage("collect") end
            assert(weak[1] == nil)
            local d = find(result, "missing")
            assert(d.offset == 2 and d.location.kind == "scope_end" and d.tag == bytes(4))
            assert(#d.path == 1 and d.path[1] == bytes(48))
            assert(tlv.schema {rules = {{tag = bytes(48), kind = "constructed"}}}
                :validate(bytes(48, 2, 4, 0)).ok)
        end)

        it("reports forms, nested limits and indefinite-length paths", function()
            local schema = tlv.schema {rules = {{tag = bytes(48), kind = "primitive"}}}
            local d = find(schema:validate(bytes(48, 0)), "kind")
            assert(d.form.expected == "primitive" and d.form.actual_constructed)
            local parent = tlv.schema {rules = {{tag = bytes(48), kind = "constructed",
                children = tlv.schema {rules = {{tag = bytes(4), min_length = 1}}}}}}
            d = find(parent:validate(bytes(48, 128, 4, 0, 0, 0)), "length")
            assert(d.offset == 2 and d.path[1] == bytes(48))
            assert(parent:validate(bytes(48, 2, 4, 0), nil, {max_depth = 0}).code == tlv.errors.LIMIT)
        end)
    else
        it("requires explicit Format when BER is disabled", function()
            failure(function() tlv.schema {}:validate("") end)
        end)
    end

    local allocator_available, fail_alloc = pcall(require, "opentlv_test_allocator")
    if allocator_available then
        it("cleans partial constructors and report conversion after allocation failure", function()
            local failures, successes = 0, 0
            for budget = 0, 100 do
                local weak = setmetatable({}, {__mode = "v"})
                local function attempt()
                    local child = tlv.schema {}
                    weak[1] = child
                    local config = {rules = {
                        {tag = bytes(48), kind = "constructed", children = child},
                        {tag = bytes(4), min_occurs = 1},
                    }}
                    local ok = fail_alloc(function()
                        tlv.schema(config):validate("", fixed)
                    end, budget)
                    if ok then successes = successes + 1 else failures = failures + 1 end
                end
                attempt()
                for _ = 1, 4 do collectgarbage("collect") end
                assert(weak[1] == nil, "child schema retained after allocation failure")
                assert(tlv.schema {}:validate("", fixed).ok)
            end
            assert(failures > 0 and successes > 0)
        end)
    end
end)
