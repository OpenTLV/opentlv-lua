-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

local tlv = require("opentlv")
local codec = tlv.codecs.uint16_be
local format = tlv.formats.fixed(1, 1, "big")
local writer = tlv.writer(format, {capacity = 32})
writer:write(string.char(1), codec:encode(4660))
for entry in tlv.reader(writer:bytes(), format) do
    print(codec:decode(entry.value)) -- 4660
end

local number = tlv.codecs.number {encoding = "binary_be", width = 8}
local exact = "18446744073709551615"
assert(number:decode(number:encode(exact)) == exact)

local custom = tlv.codec {
    decode = function(bytes) return {payload = bytes} end,
    encode = function(value) return value.payload end,
}
assert(custom:decode(custom:encode {payload = "hello"}).payload == "hello")
