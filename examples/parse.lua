-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

--[[
Parses a nested BER-TLV document and prints every element in document
order, descending into constructed elements recursively; then shows how a
truncated buffer surfaces as a structured error instead of a partial
result. See the C, C++, Rust, JavaScript and Python "parse" examples for the
same bytes and fields.

Run with `lua examples/parse.lua` from `bindings/lua`, after building the
native module (see ../README.md or docs/development/lua.md).
]]

local opentlv = require("opentlv")

local function hex(bytes)
    local parts = {}
    for i = 1, #bytes do
        parts[i] = string.format("%02X", string.byte(bytes, i))
    end
    return table.concat(parts, " ")
end

-- Bit 6 (0x20) of the first tag byte marks a constructed (nested) element;
-- written without Lua 5.3's `&` operator so this runs on 5.1 and LuaJIT too.
local function is_constructed(tag)
    return math.floor(string.byte(tag, 1) / 0x20) % 2 == 1
end

-- An FCI Template (6F) holding a DF Name (84) and an FCI Proprietary
-- Template (A5) holding an Application Label (50).
local DOCUMENT = string.char(0x6F, 0x0A, 0x84, 0x03, 0x41, 0x42, 0x43, 0xA5, 0x03, 0x50, 0x01, 0x01)

-- Elements borrow `data`; nesting is descended by re-reading a child element's
-- value with a new reader.
local function print_elements(data, depth)
    depth = depth or 0
    local count = 0
    for element in opentlv.reader(data, opentlv.formats.ber) do
        print(string.format("%stag=%s length=%d value=%s", string.rep("  ", depth),
            hex(element.tag), element.length, hex(element.value)))
        count = count + 1

        if is_constructed(element.tag) then
            count = count + print_elements(element.value, depth + 1)
        end
    end
    return count
end

local function main()
    local count = print_elements(DOCUMENT)
    -- 6F, its two children (84, A5) and A5's child (50).
    assert(count == 4)

    -- Malformed input raises a structured error instead of a partial result.
    local truncated = DOCUMENT:sub(1, #DOCUMENT - 2)
    local ok, err = pcall(function()
        for _ in opentlv.reader(truncated, opentlv.formats.ber) do
        end
    end)
    if not ok then
        print(string.format("truncated input: %s (offset %s)", tostring(err), tostring(err.offset)))
    end
end

main()
