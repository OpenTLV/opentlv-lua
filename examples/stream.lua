-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

-- Streams BER input that arrives in chunks: an incremental Tree Reader pulls
-- complete elements only, a Tree Writer rebuilds the tree from its events,
-- and a sequential Writer keeps one element's original framing.
local tlv = require("opentlv")
local bytes = string.char

local function hex(text)
    return (text:gsub(".", function(c) return string.format("%02X ", c:byte()) end))
end

-- An FCI Template (6F) holding a DF Name (84) and a Proprietary Template (A5),
-- then an OCTET STRING with a non-minimal long-form length (81 05).
local wire = bytes(0x6F, 0x0A, 0x84, 0x03, 0x41, 0x42, 0x43, 0xA5, 0x03, 0x50, 0x01, 0x01,
                   0x04, 0x81, 0x05) .. "Hello"
local chunks = {wire:sub(1, 5), wire:sub(6, 14), wire:sub(15)}

-- Feed chunks until the reader reports end of final input. Each window keeps
-- the bytes the reader has not consumed yet, followed by the next chunk.
local reader = tlv.tree_reader(chunks[1], tlv.formats.ber, {final_input = false})
local window, next_chunk = chunks[1], 2
local rebuilt = tlv.tree_writer(tlv.formats.ber)
while true do
    local ok, event = pcall(reader.next_event, reader)
    if ok and event == nil then break end
    if ok then
        rebuilt:write_event(event)
        if event.kind ~= "end" then
            print(string.rep("  ", event.depth) .. event.kind, hex(event.tag), event.offset)
        end
    elseif event.code == tlv.errors.NEED_MORE_DATA then
        local discard = reader:consumed()
        window = window:sub(discard + 1) .. chunks[next_chunk]
        reader:set_input(window, discard, next_chunk == #chunks)
        next_chunk = next_chunk + 1
    else
        error(event)
    end
end
local canonical = rebuilt:finish()
print(#wire, #canonical, hex(canonical:sub(-7)))

-- Copy the OCTET STRING with its original framing; Core checks that the
-- element still matches what was decoded before copying the bytes.
local first, size = tlv.read(wire)
local element, source = tlv.read_source(wire:sub(size + 1))
local writer = tlv.writer(tlv.formats.ber, {capacity = 64})
writer:write_element(first)
writer:preserve(source)
assert(writer:bytes() == wire)
print(hex(source:bytes()), element.value .. ", streamed world!")
