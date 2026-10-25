-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

-- A custom wire format composed without C: one-byte tags whose high bit marks
-- a constructed Value, and lengths below 0xFF inline, otherwise 0xFF and two
-- big-endian bytes. Core implements the framing; Lua only configures it.
local tlv = require("opentlv")
local bytes = string.char

local format = tlv.formats.compose {
    tag = {kind = "fixed", size = 1},
    length = {kind = "escaped", escape = 0xFF, extended_size = 2},
    constructed = {byte_index = 0, mask = 0x80, value = 0x80},
}

local writer = tlv.tree_writer(format, {capacity = 1024})
writer:begin(bytes(0x81))
writer:write(bytes(0x01), "Hello")
writer:write(bytes(0x02), ("x"):rep(300))
writer:end_element()
local wire = writer:finish()
print(#wire, string.format("%02X %02X %02X %02X", wire:byte(1, 4)))

tlv.visit_tree(wire, format, function(element, depth)
    print(string.rep("  ", depth) .. string.format("%02X", element.tag:byte()), element.length)
end)

local greeting = tlv.query("81/01"):evaluate(wire, format)[1]
print(greeting.value)

local document = tlv.document(wire, format)
document:set("81/01", "Hello, composed world!")
local encoded = document:serialize()
local element, size = format:decode(encoded)
assert(element.tag == bytes(0x81) and size == #encoded)
print(document:find("81/01"):value(), size)
