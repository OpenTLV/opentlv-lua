-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

local tlv = require("opentlv")
local bytes = string.char
local format = tlv.formats.fixed(1, 1, "big")
local schema = tlv.schema {
    rules = {
        {tag = bytes(1), name = "identifier", min_occurs = 1, max_occurs = 1,
         min_length = 2, max_length = 2, kind = "primitive"},
    },
}

assert(schema:validate(bytes(1, 2, 0, 255), format).ok)
local result = schema:validate(bytes(1, 1, 0, 2, 0), format)
assert(not result.ok and result.total_count == 2)
for _, diagnostic in ipairs(result.diagnostics) do
    print(diagnostic.kind, diagnostic.code, diagnostic.offset, diagnostic.field)
    if diagnostic.length then
        print("length", diagnostic.length.minimum, diagnostic.length.maximum,
              diagnostic.length.actual)
    end
end
