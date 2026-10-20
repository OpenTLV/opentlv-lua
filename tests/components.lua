-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

-- Every official Lua build must expose the full supported feature set.
local opentlv = require("opentlv")
assert(opentlv.document, "Document is required")
assert(opentlv.codecs.dhcpv4.message_type, "DHCP codec is required")
local names = { "ber", "cer", "der", "bluetooth_ltv", "lldp", "emv", "nfc_type2" }
local wires = {
    string.char(4, 1, 42), string.char(4, 1, 42), string.char(4, 1, 42),
    string.char(2, 4, 42), string.char(8, 1, 42), string.char(4, 1, 42), string.char(4, 1, 42),
}
for i, name in ipairs(names) do
    local format = opentlv.formats[name]
    assert(format, name .. " is required")
    assert(tostring(format) == "opentlv.Format<" .. name .. ">")
    local count = 0
    for element in opentlv.reader(wires[i], format) do
        assert(element.tag == string.char(4))
        assert(element.value == string.char(42))
        count = count + 1
    end
    assert(count == 1)
    local visited = opentlv.visit_tree(wires[i], format, function() end)
    assert(visited == 1)
    assert(opentlv.query("04"):evaluate(wires[i], format)[1].value == string.char(42))
    local doc = opentlv.document(wires[i], format)
    assert(doc:find("04"):value() == string.char(42))
    assert(doc:serialize() == wires[i])
end

local fixed = opentlv.formats.fixed(1, 1, "big")
local count = 0
for element in opentlv.reader(wires[1], fixed) do
    assert(element.value == string.char(42))
    count = count + 1
end
assert(count == 1)
assert(pcall(opentlv.reader, wires[1]))
assert(pcall(function() opentlv.query("04"):evaluate(wires[1]) end))
assert(pcall(opentlv.document, wires[1]))
