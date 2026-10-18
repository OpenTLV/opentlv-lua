-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

local opentlv = require("opentlv")

describe("opentlv.formats", function()
    if opentlv.formats.lldp then
        it("reads canonical types through the LLDP preset", function()
            assert(tostring(opentlv.formats.lldp) == "opentlv.Format<lldp>")
            local elements = {}
            for element in opentlv.reader(string.char(6, 2, 0, 120), opentlv.formats.lldp) do
                elements[#elements + 1] = element
            end
            assert(#elements == 1)
            assert(elements[1].tag == string.char(3))
            assert(elements[1].value == string.char(0, 120))
        end)
    end
    it("exposes the built-in stateless formats", function()
        assert(opentlv.formats.compact == nil)
        assert(opentlv.formats.ber ~= nil)
        assert(opentlv.formats.cer ~= nil)
        assert(opentlv.formats.der ~= nil)
        assert(opentlv.formats.bluetooth_ltv ~= nil)
    end)

    it("formats a built-in format for printing", function()
        assert(tostring(opentlv.formats.ber) == "opentlv.Format<ber>")
    end)

    it("builds a fixed-width format from tag size, length size and byte order", function()
        local fixed = opentlv.formats.fixed(1, 2, "big")
        assert(fixed ~= nil)
        assert(tostring(fixed) == "opentlv.Format<fixed>")
    end)

    it("accepts \"little\" as a byte order", function()
        assert(opentlv.formats.fixed(1, 1, "little") ~= nil)
    end)

    it("rejects a non-positive tag size", function()
        local ok = pcall(opentlv.formats.fixed, 0, 1, "big")
        assert(not ok)
    end)

    it("rejects a non-positive length size", function()
        local ok = pcall(opentlv.formats.fixed, 1, 0, "big")
        assert(not ok)
    end)

    it("rejects an unknown byte order", function()
        local ok = pcall(opentlv.formats.fixed, 1, 1, "sideways")
        assert(not ok)
    end)

    it("rejects a value that is not a format wherever a format is expected", function()
        local ok = pcall(opentlv.reader, "\1", "not a format")
        assert(not ok)
    end)
end)
