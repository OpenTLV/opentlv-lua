-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

-- Generates reproducible wire cases for a flat record Format and maps each
-- one to a Lua table through a structure codec: C validates the sequence
-- against a Schema before the decode callback sees it, and a Schema-number
-- codec converts the amount within its permitted lengths.
local tlv = require("opentlv")
local bytes = string.char
local format = tlv.formats.fixed(1, 1, "big")

-- A record holds one two-byte identifier (01) and an optional amount (02).
local amount_rule = {tag = bytes(2), name = "amount", min_length = 1, max_length = 4}
local record = tlv.schema {
    rules = {{tag = bytes(1), name = "identifier", min_occurs = 1, max_occurs = 1,
              min_length = 2, max_length = 2},
             {tag = amount_rule.tag, name = amount_rule.name, max_occurs = 1,
              min_length = amount_rule.min_length, max_length = amount_rule.max_length}},
}:check()
local amount = tlv.codecs.schema_number {rule = amount_rule}

local codec = tlv.structure_codec {
    format = format,
    schema = record,
    decode = function(sequence)
        local result = {}
        for element in tlv.reader(sequence, format) do
            if element.tag == bytes(1) then
                result.identifier = element.value
            else
                result.amount = amount:decode(element.value)
            end
        end
        return result
    end,
    encode = function(value)
        local writer = tlv.writer(format, {capacity = 16})
        writer:write(bytes(1), value.identifier)
        if value.amount then writer:write(bytes(2), amount:encode(value.amount)) end
        return writer:bytes()
    end,
}

-- Cases mix both identifiers with arbitrary Values. The seed and case index
-- reproduce each one; record them with GENERATOR_VERSION.
local generator = tlv.generator {
    format = format,
    seed = 2026,
    candidates = {{tag = bytes(1), min_value_size = 2, max_value_size = 2},
                  {tag = bytes(2), min_value_size = 0, max_value_size = 5}},
    max_elements = 3,
    max_depth = 0,
    max_value_size = 5,
    max_case_size = 32,
}
local valid, findings = 0, {}
for index = 0, 63 do
    local ok, result = pcall(codec.decode, codec, generator:generate(index))
    if ok then
        valid = valid + 1
        -- Encoding writes the shortest amount the rule permits, then C validates it.
        local again = codec:decode(codec:encode(result))
        assert(again.identifier == result.identifier and again.amount == result.amount)
    else
        local kind = result.codec_detail.schema.kind_name
        findings[kind] = (findings[kind] or 0) + 1
    end
end
print("generator version", tlv.GENERATOR_VERSION, "valid records", valid)
local kinds = {}
for kind in pairs(findings) do kinds[#kinds + 1] = kind end
table.sort(kinds)
for _, kind in ipairs(kinds) do print("rejected", kind, findings[kind]) end
print("Hello, generated world!")
