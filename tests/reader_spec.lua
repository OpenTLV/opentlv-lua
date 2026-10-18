-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

local opentlv = require("opentlv")

-- tag 0x01 length 2 value 0xAA 0xBB, then tag 0x02 length 0 (empty value).
local DEFAULT_DATA = string.char(0x01, 0x02, 0xAA, 0xBB, 0x02, 0x00)

-- An FCI Template (6F) holding a DF Name (84) and an FCI Proprietary
-- Template (A5) holding an Application Label (50); same bytes the parse.lua
-- example and every other binding's "parse" example use.
local BER_DATA = string.char(0x6F, 0x0A, 0x84, 0x03, 0x41, 0x42, 0x43, 0xA5, 0x03, 0x50, 0x01, 0x01)

describe("opentlv.reader", function()
    it("iterates default-format elements with generic for", function()
        local elements = {}
        for element in opentlv.reader(DEFAULT_DATA) do
            elements[#elements + 1] = element
        end
        assert(#elements == 2)
        assert(elements[1].tag == string.char(0x01))
        assert(elements[1].length == 2)
        assert(elements[1].value == string.char(0xAA, 0xBB))
        assert(elements[1].offset == 0)
        assert(elements[2].tag == string.char(0x02))
        assert(elements[2].length == 0)
        assert(elements[2].value == "")
        assert(elements[2].offset == 4)
    end)

    it("defaults to opentlv.formats.ber when format is omitted", function()
        local a, b = 0, 0
        for element in opentlv.reader(DEFAULT_DATA) do
            a = a + 1
        end
        for element in opentlv.reader(DEFAULT_DATA, opentlv.formats.ber) do
            b = b + 1
        end
        assert(a == b)
    end)

    it("supports the explicit :next()/:at_end() method form", function()
        local reader = opentlv.reader(DEFAULT_DATA)
        assert(reader:at_end() == false)
        assert(reader:position() == 0)
        local first = reader:next()
        assert(first.tag == string.char(0x01))
        assert(reader:position() == 4)
        local second = reader:next()
        assert(second.tag == string.char(0x02))
        assert(reader:at_end() == true)
        assert(reader:next() == nil)
    end)

    it("exposes the format a reader was constructed with", function()
        local reader = opentlv.reader(DEFAULT_DATA, opentlv.formats.ber)
        assert(reader:format() == opentlv.formats.ber)
    end)

    it("reads a nested BER document, descending via a new reader per level", function()
        local function count_elements(data)
            local n = 0
            for element in opentlv.reader(data, opentlv.formats.ber) do
                n = n + 1
                if math.floor(string.byte(element.tag, 1) / 0x20) % 2 == 1 then
                    n = n + count_elements(element.value)
                end
            end
            return n
        end
        assert(count_elements(BER_DATA) == 4)
    end)

    it("treats empty input as already at end", function()
        local reader = opentlv.reader("", opentlv.formats.ber)
        assert(reader:at_end() == true)
        assert(reader:next() == nil)
    end)

    it("reads a fixed-width format with configurable tag and length sizes", function()
        local format = opentlv.formats.fixed(2, 1, "big")
        local data = string.char(0x10, 0x20, 0x02, 0xAA, 0xBB)
        local reader = opentlv.reader(data, format)
        local element = reader:next()
        assert(element.tag == string.char(0x10, 0x20))
        assert(element.value == string.char(0xAA, 0xBB))
        assert(reader:next() == nil)
    end)

    it("raises a structured error with code, offset and operation on truncated input", function()
        local truncated = BER_DATA:sub(1, #BER_DATA - 2)
        local ok, err = pcall(function()
            for _ in opentlv.reader(truncated, opentlv.formats.ber) do
            end
        end)
        assert(not ok)
        assert(err.code == opentlv.errors.TRUNCATED)
        assert(type(err.message) == "string")
        assert(err.offset ~= nil)
        assert(err.operation ~= nil)
    end)
end)
