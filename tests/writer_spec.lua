-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

local tlv = require("opentlv")
local describe = describe or function(_, run) run() end
local it = it or function(_, run) run() end
local bytes = string.char
local fixed = tlv.formats.fixed(1, 1, "big")

local function failure(run, code)
    local ok, err = pcall(run)
    assert(not ok)
    if code then assert(err.code == code, tostring(err)) end
    return err
end

describe("Lua Writer", function()
    it("writes exact bytes, binary values and Reader elements", function()
        local w = tlv.writer(fixed, {capacity = 12})
        assert(w:bytes() == "" and w:size() == 0 and w:remaining() == 12)
        assert(w:format() == fixed)
        w:write(bytes(4), bytes(0, 255))
        local snapshot = w:bytes()
        local element = tlv.reader(snapshot, fixed):next()
        w:write_element(element)
        w:write(bytes(5), "")
        assert(snapshot == bytes(4, 2, 0, 255))
        assert(w:bytes() == snapshot .. snapshot .. bytes(5, 0))
        assert(w:size() == 10 and w:remaining() == 2)
    end)

    it("preserves output and reports native capacity diagnostics", function()
        local w = tlv.writer(fixed, {capacity = 4})
        w:write(bytes(4), "x")
        local err = failure(function() w:write(bytes(5), "yz") end,
            tlv.errors.BUFFER_TOO_SHORT)
        assert(err.offset == 3 and err.required == 4 and err.available == 1)
        assert(err.severity == "error" and err.message == tlv.strerror(err.code))
        assert(err.tag == bytes(5) and err.length == 2 and err.operation == "value")
        assert(w:bytes() == bytes(4, 1) .. "x" and w:size() == 3)
        failure(function() tlv.writer(fixed, {capacity = 0}):write(bytes(4), "") end,
            tlv.errors.BUFFER_TOO_SHORT)
    end)

    it("retains configured formats and owns returned strings", function()
        local w = tlv.writer(tlv.formats.fixed(2, 1, "big"))
        collectgarbage("collect")
        w:write(bytes(0, 4), "x")
        local result = w:bytes()
        w = nil
        collectgarbage("collect")
        assert(result == bytes(0, 4, 1) .. "x")
    end)

    it("rejects unsafe conversions", function()
        for _, n in ipairs({-1, 1.5, math.huge, 0/0, "12"}) do
            failure(function() tlv.writer(fixed, {capacity = n}) end)
        end
        local w = tlv.writer(fixed)
        failure(function() w:write(4, "value") end)
        failure(function() w:write(bytes(4), 123) end)
        failure(function() w:write(bytes(4), {}) end)
        assert(w:size() == 0)
        failure(function()
            tlv.tree_writer(fixed, {frame_capacity = 2^62})
        end)
        collectgarbage("collect")
    end)

    it("propagates format errors without consuming output", function()
        local w = tlv.writer(fixed)
        local err = failure(function() w:write(bytes(4, 5), "x") end)
        assert(type(err.code) == "number" and err.tag == bytes(4, 5))
        assert(w:size() == 0)
        failure(function() w:write(nil, "x") end)
        failure(function() w:write("", "x") end)
        w:write(bytes(4), "x")
        collectgarbage("collect")
        assert(err.tag == bytes(4, 5))
    end)

    it("uses enabled presets and requires a format without BER", function()
        local ok = pcall(tlv.writer)
        assert(ok == (tlv.formats.ber ~= nil))
        ok = pcall(tlv.tree_writer)
        assert(ok == (tlv.formats.ber ~= nil))
        local wires = {
            ber = bytes(4, 1, 42), der = bytes(4, 1, 42), cer = bytes(4, 1, 42),
            emv = bytes(4, 1, 42), bluetooth_ltv = bytes(2, 4, 42), lldp = bytes(8, 1, 42)
        }
        for name, wire in pairs(wires) do
            if tlv.formats[name] then
                local w = tlv.writer(tlv.formats[name])
                w:write(bytes(4), bytes(42))
                assert(w:bytes() == wire, name)
            end
        end
    end)

    it("makes explicit finalization safe and idempotent", function()
        local w = tlv.writer(fixed)
        getmetatable(w).__gc(w)
        getmetatable(w).__gc(w)
        failure(function() w:write(bytes(4), "x") end)
        failure(function() w:bytes() end)
    end)
end)

describe("Lua Tree Writer", function()
    it("writes flat elements with any writable format", function()
        local w = tlv.tree_writer(fixed)
        w:write_element({tag = bytes(4), value = "x"})
        assert(w:finish() == bytes(4, 1) .. "x")
        assert(w:format() == fixed)
    end)

    if not tlv.formats.ber then return end

    it("delegates nested framing and retains temporary open tags", function()
        local w = tlv.tree_writer(tlv.formats.ber)
        w:begin(bytes(0xE1))
        collectgarbage("collect")
        w:begin(bytes(0xE2))
        w:write(bytes(4), bytes(0, 255))
        assert(w:size() == 0)
        failure(function() w:bytes() end, tlv.errors.INVALID_STATE)
        w:end_element()
        w:end_element()
        assert(w:finish() == bytes(0xE1, 6, 0xE2, 4, 4, 2, 0, 255))
        assert(w:size() == 8)
        failure(function() w:end_element() end, tlv.errors.INVALID_STATE)
    end)

    it("uses each Format's constructed encoding", function()
        for _, name in ipairs({"ber", "der", "cer", "emv"}) do
            if tlv.formats[name] then
                local w = tlv.tree_writer(tlv.formats[name])
                w:begin(bytes(0xE1))
                w:write(bytes(4), "x")
                w:end_element()
                local expected = name == "cer" and bytes(0xE1, 0x80, 4, 1, 120, 0, 0)
                    or bytes(0xE1, 3, 4, 1, 120)
                assert(w:finish() == expected, name)
            end
        end
        if tlv.formats.bluetooth_ltv then
            local w = tlv.tree_writer(tlv.formats.bluetooth_ltv)
            failure(function() w:begin(bytes(4)) end)
        end
    end)

    it("lets the core expand long-form lengths", function()
        local w = tlv.tree_writer(tlv.formats.ber)
        w:begin(bytes(0xE1))
        w:write(bytes(4), string.rep("x", 128))
        w:end_element()
        assert(w:finish() == bytes(0xE1, 0x81, 131, 4, 0x81, 128) .. string.rep("x", 128))
    end)

    it("preserves children on failed parent closure", function()
        local w = tlv.tree_writer(tlv.formats.ber, {capacity = 3})
        w:begin(bytes(0xE1))
        w:write(bytes(4), "x")
        for _ = 1, 2 do
            local err = failure(function() w:end_element() end, tlv.errors.BUFFER_TOO_SHORT)
            assert(err.required == 5 and err.available == 3)
        end
        assert(w:size() == 0)
    end)

    it("reports workspace, tag, depth, frame and element limits", function()
        local w = tlv.tree_writer(tlv.formats.ber, {scratch_capacity = 0})
        w:begin(bytes(0xE1))
        w:write(bytes(4), "x")
        local err = failure(function() w:end_element() end, tlv.errors.BUFFER_TOO_SHORT)
        assert(err.operation == "end" and err.required == 3 and err.available == 0)
        w = tlv.tree_writer(tlv.formats.ber, {tag_capacity = 0})
        failure(function() w:begin(bytes(0xE1)) end)
        for _, opts in ipairs({{frame_capacity = 0}, {max_elements = 0}}) do
            w = tlv.tree_writer(tlv.formats.ber, opts)
            failure(function() w:begin(bytes(0xE1)) end,
                    opts.frame_capacity == 0 and tlv.errors.BUFFER_TOO_SHORT or tlv.errors.LIMIT)
        end
        w = tlv.tree_writer(tlv.formats.ber, {max_depth = 0})
        w:begin(bytes(0xE1))
        failure(function() w:write(bytes(4), "x") end, tlv.errors.LIMIT)
        w:end_element()
        assert(w:finish() == bytes(0xE1, 0))
    end)
end)
