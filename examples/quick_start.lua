-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

-- Write one Fixed-format element, then read it back through the public API.
-- Run with `lua examples/quick_start.lua` from bindings/lua.
local opentlv = require("opentlv")

local format = opentlv.formats.fixed(1, 1, "big")
local writer = opentlv.writer(format)
writer:write(string.char(0x01), "Hello, world!")

for element in opentlv.reader(writer:bytes(), format) do
    print(element.value)
end
