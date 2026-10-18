-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

local tlv = require("opentlv")
local format = tlv.formats.nfc_type2
if not format then return end -- availability is checked by components.lua

local wire = string.char(0, 3, 3, 0xD1, 1, 0, 0xFE, 0)
local elements = {}
local writer = tlv.writer(format)
for element in tlv.reader(wire, format) do
    elements[#elements + 1] = element
    writer:write_element(element)
end
assert(#elements == 4)
assert(elements[2].value == string.char(0xD1, 1, 0))
assert(elements[3].tag == string.char(0xFE))
assert(writer:bytes() == wire)
writer = tlv.writer(format)
writer:write(string.char(3), string.rep("x", 255))
assert(writer:bytes() == string.char(3, 255, 0, 255) .. string.rep("x", 255))
local element = tlv.reader(writer:bytes(), format):next()
assert(#element.value == 255)
local ok, err = pcall(function()
    return tlv.reader(string.char(3, 255, 255, 255), format):next()
end)
assert(not ok and err.code == tlv.errors.INVALID_LENGTH)
