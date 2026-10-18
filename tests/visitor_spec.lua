-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

local opentlv = require("opentlv")
local describe = describe or function(_, run) run() end
local it = it or function(_, run) run() end

-- An FCI Template (6F) holding a DF Name (84) and an FCI Proprietary
-- Template (A5) holding an Application Label (50).
local BER_DATA = string.char(0x6F, 0x0A, 0x84, 0x03, 0x41, 0x42, 0x43, 0xA5, 0x03, 0x50, 0x01, 0x01)

if opentlv.formats.ber then
    describe("opentlv.visit_tree", function()
        it("visits every element in preorder with depth, offset and constructed", function()
            local seen = {}
            local visited, stopped = opentlv.visit_tree(BER_DATA, opentlv.formats.ber,
                function(element, depth)
                    seen[#seen + 1] = {tag = element.tag, depth = depth, offset = element.offset,
                                       constructed = element.constructed}
                end)
            assert(visited == 4)
            assert(stopped == false)
            assert(#seen == 4)

            assert(seen[1].tag == string.char(0x6F) and seen[1].depth == 0 and seen[1].offset == 0)
            assert(seen[1].constructed == true)
            assert(seen[2].tag == string.char(0x84) and seen[2].depth == 1)
            assert(seen[2].constructed == false)
            assert(seen[3].tag == string.char(0xA5) and seen[3].depth == 1)
            assert(seen[3].constructed == true)
            assert(seen[4].tag == string.char(0x50) and seen[4].depth == 2)
            assert(seen[4].constructed == false)
        end)

        it("stops early when the callback returns false", function()
            local visited, stopped = opentlv.visit_tree(BER_DATA, opentlv.formats.ber,
                function(element, depth)
                    return false
                end)
            assert(visited == 1)
            assert(stopped == true)
        end)

        it("continues when the callback returns nothing or true", function()
            local visited, stopped = opentlv.visit_tree(BER_DATA, opentlv.formats.ber, function() end)
            assert(visited == 4)
            assert(stopped == false)
        end)

        it("propagates a Lua error raised by the callback", function()
            local ok, err = pcall(opentlv.visit_tree, BER_DATA, opentlv.formats.ber, function()
                error("boom")
            end)
            assert(not ok)
            assert(tostring(err):find("boom", 1, true) ~= nil)
        end)

        it("raises a structured error when max_elements is exceeded", function()
            local ok, err = pcall(opentlv.visit_tree, BER_DATA, opentlv.formats.ber, function() end,
                {max_elements = 1})
            assert(not ok)
            assert(err.code == opentlv.errors.LIMIT)
        end)

        it("raises a structured error when max_depth is exceeded", function()
            local ok, err = pcall(opentlv.visit_tree, BER_DATA, opentlv.formats.ber, function() end,
                {max_depth = 0})
            assert(not ok)
            assert(err.code == opentlv.errors.LIMIT)
        end)

        it("validates structure only when the callback is omitted", function()
            local visited, stopped = opentlv.visit_tree(BER_DATA, opentlv.formats.ber, nil)
            assert(visited == 0)
            assert(stopped == false)
        end)

        if opentlv.formats.der then
            it("uses tlv_der_visit() for opentlv.formats.der", function()
                local visited = opentlv.visit_tree(BER_DATA, opentlv.formats.der, function() end)
                assert(visited == 4)
            end)
        end
    end)
end

local fixed = opentlv.formats.fixed(1, 1, "big")
local inject_failure
if arg and arg[1] == "--allocator" then
    inject_failure = require("opentlv_test_allocator")
else
    local ok, module = pcall(require, "opentlv_test_allocator")
    if ok then inject_failure = module end
end
local DATA = string.char(1, 2, 0, 255, 2, 0)
local function failure(run, code)
    local ok, err = pcall(run)
    assert(not ok)
    if code then assert(err.code == code, tostring(err)) end
    return err
end

describe("Lua visitors", function()
    if inject_failure then
        it("recovers from allocation failures without retaining callback references", function()
            local failures, successes = 0, 0
            for _, visit in ipairs({opentlv.visit, opentlv.visit_tree}) do
                for allowance = 0, 100 do
                    local weak = setmetatable({}, {__mode = "v"})
                    local function attempt()
                        local token = {true}
                        local callback = function() return token[1] end
                        weak[1] = callback
                        local ok = inject_failure(function() visit(DATA, fixed, callback) end, allowance)
                        if ok then successes = successes + 1 else failures = failures + 1 end
                    end
                    attempt()
                    collectgarbage("collect")
                    collectgarbage("collect")
                    assert(weak[1] == nil, "callback retained after allocation failure")
                    assert(visit(DATA, fixed, function() end) == 2)
                end
            end
            assert(failures > 0 and successes > 0)
        end)
    end
    for _, visit in ipairs({opentlv.visit, opentlv.visit_tree}) do
        it("copies binary entries and preserves wire offsets", function()
            local seen = {}
            local n, stopped = visit(DATA, fixed, function(entry)
                seen[#seen + 1] = entry
                return true
            end)
            collectgarbage("collect")
            assert(n == 2 and not stopped)
            assert(seen[1].tag == string.char(1) and seen[1].value == string.char(0, 255))
            assert(seen[1].length == 2 and seen[1].offset == 0)
            assert(seen[2].value == "" and seen[2].offset == 4)
            assert(seen[1].constructed == false)
            assert(visit("", fixed, function() error("unexpected") end) == 0)
        end)

        it("supports stop, reentrant calls and garbage collection", function()
            local n, stopped = visit(DATA, fixed, function()
                collectgarbage("collect")
                assert(visit(DATA, fixed, function() end) == 2)
                return false
            end)
            assert(n == 1 and stopped)
            assert(visit(DATA, fixed, function() return 0 end) == 2)
        end)

        it("preserves error objects and releases callbacks on every exit", function()
            local weak = setmetatable({}, {__mode = "v"})
            local marker = {}
            local function run(mode)
                local callback = function()
                    if mode == "error" then error(marker) end
                    if mode == "stop" then return false end
                end
                weak[1] = callback
                if mode == "error" then
                    assert(failure(function() visit(DATA, fixed, callback) end) == marker)
                elseif mode == "parse" then
                    failure(function() visit(DATA .. string.char(3), fixed, callback) end)
                else
                    visit(DATA, fixed, callback)
                end
            end
            for _, mode in ipairs({"error", "stop", "parse", "success"}) do
                run(mode)
                collectgarbage("collect")
                collectgarbage("collect")
                assert(weak[1] == nil)
            end
            for _ = 1, 200 do
                local ok, err = pcall(visit, DATA, fixed, function() error(marker) end)
                assert(not ok and err == marker)
                assert(visit(DATA, fixed, function() end) == 2)
            end
        end)
    end

    it("requires a sequential callback and preserves Reader diagnostics", function()
        failure(function() opentlv.visit(DATA, fixed) end)
        local err = failure(function()
            opentlv.visit(DATA .. string.char(3, 2, 0), fixed, function() end)
        end, opentlv.errors.TRUNCATED)
        assert(err.offset ~= nil and err.operation == "value")
    end)

    it("checks limits without lossy integer conversions", function()
        for _, key in ipairs({"max_depth", "max_elements"}) do
            for _, value in ipairs({-1, 0.5, math.huge, 0/0, "1", false}) do
                failure(function() opentlv.visit_tree(DATA, fixed, nil, {[key] = value}) end)
            end
        end
        assert(opentlv.visit_tree("", fixed, nil, {max_elements = 0, max_depth = 0}) == 0)
        failure(function() opentlv.visit_tree(DATA, fixed, nil, {max_elements = 0}) end,
            opentlv.errors.LIMIT)
    end)

    if opentlv.formats.ber then
        it("distinguishes sequential and nested traversal and includes EOC in offsets", function()
            local data = string.char(0x30, 0x80, 4, 1, 7, 0, 0, 4, 0)
            local seen = {}
            assert(opentlv.visit(data, opentlv.formats.ber, function(e)
                seen[#seen + 1] = e
            end) == 2)
            assert(seen[1].constructed and seen[2].offset == 7)
            assert(opentlv.visit_tree(data, opentlv.formats.ber, function() end) == 3)
        end)
    end

    if opentlv.formats.der then
        it("honors DER limits with and without callbacks", function()
            for _, callback in ipairs({false, function() end}) do
                for _, opts in ipairs({{max_elements = 1}, {max_depth = 0}}) do
                    failure(function()
                        opentlv.visit_tree(BER_DATA, opentlv.formats.der, callback or nil, opts)
                    end, opentlv.errors.LIMIT)
                end
            end
        end)
    end
end)
