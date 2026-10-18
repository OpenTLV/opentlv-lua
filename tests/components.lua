-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

-- Standalone smoke test: expected availability comes from the CMake build.
local opentlv = require("opentlv")
assert((opentlv.document ~= nil) == (arg[8] == "1"), "Document availability mismatch")
local names = { "ber", "cer", "der", "bluetooth_ltv", "lldp", "emv", "nfc_type2" }
local wires = {
    string.char(4, 1, 42), string.char(4, 1, 42), string.char(4, 1, 42),
    string.char(2, 4, 42), string.char(8, 1, 42), string.char(4, 1, 42), string.char(4, 1, 42),
}
for i, name in ipairs(names) do
    local format = opentlv.formats[name]
    assert((format ~= nil) == (arg[i] == "1"), name .. " availability mismatch")
    if format then
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
        if opentlv.document then
            local doc = opentlv.document(wires[i], format)
            assert(doc:find("04"):value() == string.char(42))
            assert(doc:serialize() == wires[i])
        end
    end
end

local fixed = opentlv.formats.fixed(1, 1, "big")
local count = 0
for element in opentlv.reader(wires[1], fixed) do
    assert(element.value == string.char(42))
    count = count + 1
end
assert(count == 1)
local ok, result = pcall(opentlv.reader, wires[1])
assert(ok == (arg[1] == "1"))
if not ok then
    assert(tostring(result):find("format is required when BER is disabled", 1, true))
end
assert(pcall(function() opentlv.query("04"):evaluate(wires[1]) end) == (arg[1] == "1"))
if opentlv.document then
    assert(pcall(opentlv.document, wires[1]) == (arg[1] == "1"))
end
