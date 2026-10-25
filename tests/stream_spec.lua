-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

-- Streaming Reader, Source, Tree Reader and Writer operations. Core owns the
-- parser and writer semantics; these cases check the binding's conversion,
-- resumability, ownership and diagnostic contract.
local tlv = require("opentlv")
local describe = describe or function(_, run) run() end
local it = it or function(_, run) run() end
local bytes = string.char
local fixed = tlv.formats.fixed(1, 1, "big")

-- An FCI Template (6F) holding a DF Name (84) and an FCI Proprietary
-- Template (A5) holding an Application Label (50).
local BER_DATA = bytes(0x6F, 0x0A, 0x84, 0x03, 0x41, 0x42, 0x43, 0xA5, 0x03, 0x50, 0x01, 0x01)
-- One OCTET STRING with a valid, non-minimal BER long-form length.
local LONG_FORM = bytes(0x04, 0x81, 0x01, 0x41)

local inject_failure
if arg and arg[1] == "--allocator" then
    inject_failure = require("opentlv_test_allocator")
else
    local ok, module = pcall(require, "opentlv_test_allocator")
    if ok then inject_failure = module end
end

local function failure(run, code)
    local ok, err = pcall(run)
    assert(not ok)
    if code then assert(type(err) == "table" and err.code == code, tostring(err)) end
    return err
end

describe("incremental Reader", function()
    it("reports need-more-data, resumes and keeps absolute offsets after discard", function()
        local r = tlv.reader(bytes(1, 2, 0xAA), fixed, {final_input = false})
        local err = failure(function() r:next() end, tlv.errors.NEED_MORE_DATA)
        assert(err.severity == "info" and err.operation == "value" and err.required == 2)
        assert(r:consumed() == 0 and r:offset() == 0 and not r:at_end())
        r:set_input(bytes(1, 2, 0xAA, 0xBB, 2, 0), 0, false)
        local first = r:next()
        assert(first.tag == bytes(1) and first.value == bytes(0xAA, 0xBB) and first.offset == 0)
        assert(r:consumed() == 4 and r:position() == 4 and r:offset() == 4)
        r:set_input(bytes(2, 0, 3), 4, false)
        local second = r:next()
        assert(second.tag == bytes(2) and second.offset == 4)
        assert(r:consumed() == 2 and r:offset() == 6)
        err = failure(function() r:next() end, tlv.errors.NEED_MORE_DATA)
        assert(err.offset == 7 and err.tag == bytes(3))
        r:set_input(bytes(3, 1, 7), 2, true)
        local third = r:next()
        assert(third.value == bytes(7) and third.offset == 6)
        assert(r:next() == nil and r:at_end())
    end)

    it("rejects changed windows and extension of final input without changing state", function()
        local r = tlv.reader(bytes(1, 1), fixed, {final_input = false})
        failure(function() r:set_input(bytes(9, 9, 9), 0, false) end, tlv.errors.INVALID_ARG)
        failure(function() r:set_input(bytes(1), 0, false) end, tlv.errors.INVALID_ARG)
        failure(function() r:set_input(bytes(1, 1), 1, false) end, tlv.errors.INVALID_ARG)
        r:set_input(bytes(1, 1, 5), 0, true)
        assert(r:next().value == bytes(5) and r:at_end())
        failure(function() r:set_input(bytes(1, 1, 5, 2, 0), 0, true) end,
            tlv.errors.INVALID_STATE)
        assert(r:next() == nil)
        failure(function() r:set_input(bytes(1, 1, 5), -1) end)
        failure(function() r:set_input(bytes(1, 1, 5), 0, "yes") end)
        failure(function() tlv.reader("", fixed, {final_input = 0}) end)
    end)

    it("keeps generic-for iteration over final input", function()
        local count = 0
        for element in tlv.reader(bytes(1, 0, 2, 0), fixed, {final_input = true}) do
            count = count + 1
            assert(element.offset == (count - 1) * 2)
        end
        assert(count == 2)
    end)
end)

describe("Source", function()
    it("publishes the original encoding and layout from one decode", function()
        local r = tlv.reader(LONG_FORM .. bytes(5, 0))
        local element, source = r:next_source()
        assert(element.tag == bytes(4) and element.value == "A" and element.offset == 0)
        assert(source:bytes() == LONG_FORM and source:offset() == 0)
        assert(source:format() == tlv.formats.ber)
        local layout = source:layout()
        assert(layout.size == 4 and layout.header.offset == 0 and layout.header.size == 3)
        assert(layout.tag.size == 1 and layout.length.offset == 1 and layout.length.size == 2)
        assert(layout.value.offset == 3 and layout.value.size == 1 and layout.trailer.size == 0)
        assert(source:element().value == "A")
        local next_element, next_source = r:next_source()
        assert(next_element.offset == 4 and next_source:offset() == 4)
        assert(r:next_source() == nil)
    end)

    it("preserves unchanged content and rejects changed content", function()
        local element, source = tlv.read_source(LONG_FORM)
        assert(source:preserve() == LONG_FORM)
        assert(source:preserve(element) == LONG_FORM)
        assert(tlv.formats.ber:encode(element.tag, element.value) == bytes(4, 1, 0x41))
        local err = failure(function() source:preserve({tag = bytes(4), value = "B"}) end,
            tlv.errors.INVALID_ARG)
        assert(err.operation == "preserve")
        failure(function() source:preserve({tag = nil, value = "A"}) end, tlv.errors.INVALID_ARG)
        failure(function() source:preserve({tag = bytes(4)}) end)
    end)

    it("retains its input and Format beyond the Reader and input replacement", function()
        local r = tlv.reader(bytes(1, 1) .. "x", tlv.formats.fixed(1, 1, "big"),
            {final_input = false})
        local _, source = r:next_source()
        r:set_input("", 3, true)
        r = nil
        collectgarbage("collect")
        collectgarbage("collect")
        assert(source:bytes() == bytes(1, 1) .. "x" and source:preserve() == source:bytes())
        getmetatable(source).__gc(source)
        getmetatable(source).__gc(source)
        failure(function() source:bytes() end)
    end)

    it("reads one element with or without a Source", function()
        local element, size = tlv.read(LONG_FORM .. bytes(9))
        assert(element.value == "A" and size == 4)
        local sourced, source = tlv.read_source(LONG_FORM .. bytes(9), tlv.formats.ber)
        assert(sourced.value == "A" and source:bytes() == LONG_FORM)
        assert(tlv.read("") == nil and tlv.read_source("") == nil)
        local err = failure(function() tlv.read(bytes(4, 5, 1)) end, tlv.errors.TRUNCATED)
        assert(err.operation == "value" and err.declared_length == 5 and err.available == 1)
        failure(function() tlv.read_source(bytes(1, 5), fixed) end, tlv.errors.TRUNCATED)
    end)
end)

describe("Reader visit", function()
    it("resumes after a stop without replaying visited elements", function()
        local r = tlv.reader(bytes(1, 0, 2, 0, 3, 0), fixed)
        local seen = {}
        local visited, stopped = r:visit(function(element)
            seen[#seen + 1] = element
            return false
        end)
        assert(visited == 1 and stopped and seen[1].tag == bytes(1))
        visited, stopped = r:visit(function(element) seen[#seen + 1] = element end)
        assert(visited == 2 and not stopped)
        assert(seen[2].tag == bytes(2) and seen[2].offset == 2)
        assert(seen[3].tag == bytes(3) and seen[3].offset == 4 and r:at_end())
    end)

    it("stops for more input and continues after set_input", function()
        local r = tlv.reader(bytes(1, 0, 2), fixed, {final_input = false})
        local tags = {}
        local function collect(element) tags[#tags + 1] = element.tag end
        failure(function() r:visit(collect) end, tlv.errors.NEED_MORE_DATA)
        assert(#tags == 1)
        r:set_input(bytes(2, 1, 9), 2, true)
        assert(r:visit(collect) == 1 and tags[2] == bytes(2))
    end)

    it("rejects reentry and propagates callback errors", function()
        local r = tlv.reader(bytes(1, 0, 2, 0, 3, 0), fixed)
        local err = failure(function() r:visit(function() r:next() end) end)
        assert(tostring(err):find("visitor callback", 1, true))
        failure(function() r:visit(function() getmetatable(r).__gc(r) end) end)
        local token = {}
        local ok, raised = pcall(r.visit, r, function() error(token) end)
        assert(not ok and raised == token)
        assert(r:next() == nil)
        failure(function() r:visit() end)
    end)
end)

describe("Tree Reader", function()
    it("pulls preorder items with depth, offset and constructed", function()
        local r = tlv.tree_reader(BER_DATA)
        assert(r:format() == tlv.formats.ber)
        local seen = {}
        for item in r do seen[#seen + 1] = item end
        assert(#seen == 4 and r:at_end())
        assert(seen[1].tag == bytes(0x6F) and seen[1].depth == 0 and seen[1].constructed)
        assert(seen[2].tag == bytes(0x84) and seen[2].depth == 1 and seen[2].offset == 2)
        assert(seen[3].tag == bytes(0xA5) and seen[3].constructed and seen[3].offset == 7)
        assert(seen[4].tag == bytes(0x50) and seen[4].depth == 2 and not seen[4].constructed)
        assert(r:consumed() == #BER_DATA and r:offset() == #BER_DATA)
    end)

    it("pulls balanced events that Tree Writer reproduces", function()
        local r = tlv.tree_reader(BER_DATA)
        local kinds, events = {}, {}
        for event in function() return r:next_event() end do
            kinds[#kinds + 1] = event.kind .. event.depth
            events[#events + 1] = event
        end
        assert(table.concat(kinds, " ") == "begin0 element1 begin1 element2 end1 end0")
        assert(events[1].constructed and events[1].tag == bytes(0x6F))
        assert(events[5].offset == 12 and events[5].tag == nil and events[5].skipped == false)
        local w = tlv.tree_writer(tlv.formats.ber)
        for _, event in ipairs(events) do w:write_event(event) end
        assert(w:finish() == BER_DATA)
    end)

    it("skips subtrees and recovers from descent limits", function()
        local r = tlv.tree_reader(BER_DATA)
        assert(r:next().tag == bytes(0x6F))
        r:skip_subtree()
        assert(r:next() == nil)
        failure(function() r:skip_subtree() end, tlv.errors.INVALID_STATE)
        r = tlv.tree_reader(BER_DATA)
        assert(r:next_event().kind == "begin")
        r:skip_subtree()
        local closed = r:next_event()
        assert(closed.kind == "end" and closed.skipped and closed.depth == 0)
        for _, case in ipairs({{{max_depth = 0}, tlv.errors.LIMIT},
                               {{frame_capacity = 0}, tlv.errors.BUFFER_TOO_SHORT}}) do
            r = tlv.tree_reader(BER_DATA, nil, case[1])
            assert(r:next().constructed)
            failure(function() r:next() end, case[2])
            r:skip_subtree()
            assert(r:next() == nil and r:at_end())
        end
        r = tlv.tree_reader(BER_DATA, nil, {max_elements = 1})
        r:next()
        failure(function() r:next() end, tlv.errors.LIMIT)
    end)

    it("publishes parents only when complete incremental input arrives", function()
        local r = tlv.tree_reader(BER_DATA:sub(1, 5), nil, {final_input = false})
        local err = failure(function() r:next() end, tlv.errors.NEED_MORE_DATA)
        assert(err.severity == "info" and err.tag == bytes(0x6F))
        r:set_input(BER_DATA .. bytes(0x04), 0, false)
        local count = 0
        while count < 4 do
            r:next()
            count = count + 1
        end
        failure(function() r:next() end, tlv.errors.NEED_MORE_DATA)
        assert(r:consumed() == 12 and r:offset() == 12)
        r:set_input(bytes(0x04, 0x00), 12, true)
        local last = r:next()
        assert(last.tag == bytes(4) and last.offset == 12 and last.depth == 0)
        assert(r:next() == nil and r:at_end())
        failure(function() r:set_input(bytes(1)) end, tlv.errors.INVALID_ARG)
    end)

    it("visits the remaining items and resumes into a stopped parent", function()
        local r = tlv.tree_reader(BER_DATA)
        local depths = {}
        local visited, stopped = r:visit(function(element, depth)
            depths[#depths + 1] = depth
            return element.tag ~= bytes(0x6F) or false
        end)
        assert(visited == 1 and stopped)
        visited, stopped = r:visit(function(element, depth)
            depths[#depths + 1] = depth
            assert(type(element.constructed) == "boolean")
        end)
        assert(visited == 3 and not stopped and table.concat(depths, ",") == "0,1,1,2")
        r = tlv.tree_reader(BER_DATA)
        failure(function() r:visit(function() r:skip_subtree() end) end)
        assert(r:next().tag == bytes(0x84))
    end)

    it("rejects invalid options and collected readers", function()
        for _, options in ipairs({{max_depth = -1}, {frame_capacity = 1.5},
                                  {frame_capacity = 2^62}, {final_input = "no"}}) do
            failure(function() tlv.tree_reader(BER_DATA, nil, options) end)
        end
        failure(function() tlv.tree_reader(BER_DATA, nil, 5) end)
        local r = tlv.tree_reader(BER_DATA)
        getmetatable(r).__gc(r)
        getmetatable(r).__gc(r)
        failure(function() r:next() end)
        failure(function() r:consumed() end)
    end)
end)

describe("streaming Writer", function()
    it("copies raw encoded bytes and keeps output on shortage", function()
        local w = tlv.writer(fixed, {capacity = 4})
        w:copy_encoded(bytes(1, 1, 7))
        w:copy_encoded("")
        local err = failure(function() w:copy_encoded(bytes(2, 0)) end,
            tlv.errors.BUFFER_TOO_SHORT)
        assert(err.operation == "copy" and err.required == 2 and err.available == 1)
        assert(w:bytes() == bytes(1, 1, 7))
        failure(function() w:copy_encoded(5) end)
        assert(tlv.tree_writer(fixed).copy_encoded == nil)
    end)

    it("preserves unchanged sources and rejects changed elements", function()
        local element, source = tlv.read_source(LONG_FORM)
        local w = tlv.writer(tlv.formats.ber)
        w:preserve(source)
        w:preserve(source, element)
        w:write_element(element)
        assert(w:bytes() == LONG_FORM .. LONG_FORM .. bytes(4, 1, 0x41))
        local err = failure(function() w:preserve(source, {tag = bytes(4), value = "B"}) end,
            tlv.errors.INVALID_ARG)
        assert(err.operation == "preserve")
        assert(w:size() == 11)
        w = tlv.writer(tlv.formats.ber, {capacity = 3})
        err = failure(function() w:preserve(source) end, tlv.errors.BUFFER_TOO_SHORT)
        assert(err.required == 4 and err.available == 3 and w:size() == 0)
        failure(function() w:preserve({}) end)
    end)

    it("measures Elements and content-independent lengths", function()
        assert(tlv.encoded_size(bytes(4), "abc") == 5)
        assert(tlv.encoded_size(bytes(4), 300) == 304)
        assert(tlv.encoded_size(bytes(4), 3, fixed) == 5)
        assert(tlv.encoded_size(bytes(4), "", tlv.formats.ber) == 2)
        local err = failure(function() tlv.encoded_size(bytes(4, 5), "x", fixed) end)
        assert(err.tag == bytes(4, 5))
        failure(function() tlv.encoded_size(bytes(4), 256, fixed) end)
        failure(function() tlv.encoded_size(bytes(4), -1) end)
        failure(function() tlv.encoded_size(4, "x") end)
    end)
end)

describe("Tree Writer events and measurement", function()
    local ITEMS = {
        {tag = bytes(0x6F), depth = 0, constructed = true},
        {tag = bytes(0x84), value = "ABC", depth = 1},
        {tag = bytes(0xA5), value = "ignored", depth = 1, constructed = true},
        {tag = bytes(0x50), value = bytes(1), depth = 2},
    }

    it("writes events and rejects malformed ones", function()
        local w = tlv.tree_writer(tlv.formats.ber)
        w:write_event({kind = "begin", tag = bytes(0xE1), depth = 0})
        w:write_event({kind = "element", tag = bytes(4), value = "x", depth = 1})
        failure(function() w:write_event({kind = "element", tag = bytes(4), value = "x",
                                          depth = 3}) end)
        failure(function() w:write_event({kind = "end", depth = 0, skipped = true}) end,
            tlv.errors.INVALID_VALUE)
        w:write_event({kind = "end", depth = 0})
        assert(w:finish() == bytes(0xE1, 3, 4, 1) .. "x")
        for _, event in ipairs({{kind = "open", depth = 0}, {kind = "end"},
                                {kind = "element", tag = bytes(4), depth = 0},
                                {kind = "end", depth = 0, skipped = 1}}) do
            failure(function() w:write_event(event) end)
        end
        assert(tlv.writer().write_event == nil)
    end)

    it("stages preorder items from arrays, functions and Tree Readers", function()
        assert(tlv.measure_tree(nil, ITEMS) == BER_DATA)
        local index = 0
        assert(tlv.measure_tree(tlv.formats.ber, function()
            index = index + 1
            return ITEMS[index]
        end) == BER_DATA)
        assert(tlv.measure_tree(nil, tlv.tree_reader(BER_DATA)) == BER_DATA)
        assert(tlv.measure_tree(fixed, {}) == "")
        local r = tlv.tree_reader(BER_DATA)
        assert(tlv.measure_events(nil, function() return r:next_event() end) == BER_DATA)
    end)

    it("reports workspace requirements and invalid sequences", function()
        local err = failure(function() tlv.measure_tree(nil, ITEMS, {capacity = 4}) end,
            tlv.errors.BUFFER_TOO_SHORT)
        assert(err.required_data and err.required_data > 4)
        err = failure(function() tlv.measure_tree(nil, ITEMS, {scratch_capacity = 2}) end,
            tlv.errors.BUFFER_TOO_SHORT)
        assert(err.required_scratch and err.required_scratch > 2)
        failure(function() tlv.measure_tree(nil, ITEMS, {max_elements = 2}) end, tlv.errors.LIMIT)
        failure(function() tlv.measure_events(nil, {{kind = "begin", tag = bytes(0xE1),
                                                     depth = 0}}) end, tlv.errors.INVALID_VALUE)
        failure(function() tlv.measure_tree(nil, {{tag = bytes(4), depth = 0}}) end)
        failure(function() tlv.measure_tree(nil, "items") end)
        failure(function() tlv.measure_tree(nil, ITEMS, {capacity = -1}) end)
    end)

    it("propagates source errors unchanged and retains tags until completion", function()
        local token = {}
        local ok, raised = pcall(tlv.measure_tree, nil, function() error(token) end)
        assert(not ok and raised == token)
        local index = 0
        local result = tlv.measure_tree(nil, function()
            index = index + 1
            collectgarbage("collect")
            if index == 1 then return {tag = bytes(0xE1), depth = 0, constructed = true} end
            if index == 2 then return {tag = bytes(4), value = string.rep("v", 3), depth = 1} end
        end)
        assert(result == bytes(0xE1, 5, 4, 3) .. "vvv")
    end)

    if inject_failure then
        it("recovers from allocation failures without retaining sources", function()
            local failures, successes = 0, 0
            for allowance = 0, 120 do
                local weak = setmetatable({}, {__mode = "v"})
                local function attempt()
                    local items = {
                        {tag = bytes(0xE1), depth = 0, constructed = true},
                        {tag = bytes(4), value = "x", depth = 1},
                    }
                    local callback = function() end
                    weak[1], weak[2] = items, callback
                    local ok = inject_failure(function()
                        tlv.measure_tree(nil, items)
                        local r = tlv.reader(bytes(1, 0, 2, 0), fixed)
                        local _, source = r:next_source()
                        r:visit(callback)
                        tlv.tree_reader(BER_DATA):visit(callback)
                        source:preserve()
                        tlv.read_source(LONG_FORM)
                    end, allowance)
                    if ok then successes = successes + 1 else failures = failures + 1 end
                end
                attempt()
                collectgarbage("collect")
                collectgarbage("collect")
                assert(weak[1] == nil and weak[2] == nil, "source retained after failure")
            end
            assert(failures > 0 and successes > 0)
        end)
    end
end)
