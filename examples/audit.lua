-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

-- Audits one BER template inside a larger stream: a Document builder copies
-- only the selected subtree from a Tree Reader, a Schema checked once validates
-- it, a value constraint names a decoded number, and an edit shows which nodes
-- keep their original input coordinates.
local tlv = require("opentlv")
local bytes = string.char

-- An FCI Template (6F) with a DF Name (84) and a Proprietary Template (A5)
-- holding an Application Priority Indicator (87), then an Amount (9F02).
local wire = bytes(0x6F, 0x08, 0x84, 0x01, 0x41, 0xA5, 0x03, 0x87, 0x01, 0x02,
                   0x9F, 0x02, 0x01, 0x05)

-- Pull items until the Proprietary Template, then build only that subtree.
local reader = tlv.tree_reader(wire, tlv.formats.ber)
repeat
    local item = reader:next()
until item.tag == bytes(0xA5)
local doc = reader:document_builder({subtree = "current", retain_source_locations = true})
                  :consume()
print("next after subtree", reader:next().tag == bytes(0x9F, 0x02))

-- Check the definition once; the checked Schema is reused for validation.
local priority = tlv.schema {
    rules = {{tag = bytes(0x87), name = "priority", min_occurs = 1, max_occurs = 1,
              min_length = 1, max_length = 1}},
}
local template = tlv.schema {
    rules = {{tag = bytes(0xA5), kind = "constructed", min_occurs = 1, children = priority}},
}:check()
print("valid", template:validate(doc:serialize()).ok)

-- A named-number constraint on the decoded Value, not on the wire bytes.
local levels = tlv.value_constraint {
    kind = "allowed_values", allowed_values = {1, 2, 3},
    allowed_value_names = {"highest", "high", "normal"},
}
doc:visit("A5/87", function(node)
    local value = tlv.codecs.uint8:decode(node:value())
    local location = node:source_location()
    print(tlv.query("a5/87"):format(), value, levels:name(value), location.offset,
          location.header_size)
end)

-- Replacing the Value drops its original coordinates and those of its parent.
local node = doc:find("A5/87")
node:set(bytes(1))
print("revision", doc:revision(), node:source_location(), levels:name(1))
print("Hello, audited world!")
