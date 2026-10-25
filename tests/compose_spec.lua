-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

local tlv = require("opentlv")
local b = string.char

local function failure(run, code)
    local ok, err = pcall(run)
    assert(not ok, "expected failure")
    if code then assert(type(err) == "table" and err.code == code, tostring(err)) end
    return err
end

local function escaped()
    return tlv.formats.compose {
        tag = {kind = "fixed", size = 1},
        length = {kind = "escaped", escape = 0xFF, extended_size = 2},
        constructed = {mask = 0x80, value = 0x80},
    }
end

-- BER-like framing: low-tag-number identifiers with escapes, short or long counts.
local function variable(options)
    options = options or {}
    return tlv.formats.compose {
        tag = {kind = "variable", inline_mask = 0x1F, escape = 0x1F, continuation_bit = 0x80,
               payload_mask = 0x7F, max_size = 3, policy = options.tag_policy},
        length = {kind = "variable", long_form_bit = 0x80, payload_mask = 0x7F,
                  policy = options.length_policy},
        tag_only = options.tag_only,
        constructed = {byte_index = 0, mask = 0x20, value = 0x20},
    }
end

local function lldp()
    return tlv.formats.packed {
        header_size = 2,
        tag = {bit_offset = 9, bit_width = 7},
        length = {bit_offset = 0, bit_width = 9},
    }
end

describe("Format operations", function()
    it("measure, encode and decode one element of any Format", function()
        local ber = tlv.formats.ber
        assert(ber:can_read() and ber:can_write())
        local encoding = ber:measure(b(4), "abc")
        assert(encoding.header == 2 and encoding.value == 3 and encoding.trailer == 0)
        assert(encoding.total == 5)
        assert(ber:encode(b(4), "abc") == b(4, 3) .. "abc")
        local element, size = ber:decode(b(4, 3) .. "abc" .. b(5, 0))
        assert(element.tag == b(4) and element.value == "abc" and element.length == 3)
        assert(element.offset == 0 and size == 5)
        local fixed = tlv.formats.fixed(1, 2, "little")
        assert(fixed:encode(b(9), "xy") == b(9, 2, 0) .. "xy")
    end)

    it("report the Format's own failure detail", function()
        local err = failure(function() tlv.formats.ber:decode(b(4, 5) .. "ab") end,
            tlv.errors.TRUNCATED)
        assert(err.operation == "decode" and err.region == "value" and err.required == 5)
        assert(err.message == tlv.strerror(err.code) and err.offset == 2)
        assert(err.tag_range.offset == 0 and err.tag_range.size == 1)
        assert(err.length_range.offset == 1 and err.length_range.size == 1)
        local end_err = failure(function() tlv.formats.ber:decode("") end, tlv.errors.END)
        assert(end_err.operation == "decode" and end_err.region == nil)
        err = failure(function() tlv.formats.fixed(1, 1, "big"):encode(b(1, 2), "x") end,
            tlv.errors.INVALID_TAG_SIZE)
        assert(err.operation == "measure")
        err = failure(function() tlv.formats.fixed(1, 1, "big"):measure(b(1), ("x"):rep(256)) end,
            tlv.errors.INVALID_LENGTH)
        assert(err.region == "length")
    end)

    it("check their arguments", function()
        failure(function() tlv.formats.ber.decode("x") end)
        failure(function() tlv.formats.ber:decode(5) end)
        failure(function() tlv.formats.ber:encode(b(4), nil) end)
        failure(function() tlv.formats.ber:measure(4, "x") end)
    end)
end)

describe("opentlv.formats.compose", function()
    it("composes a fixed identifier with an escaped length", function()
        local format = escaped()
        assert(tostring(format) == "opentlv.Format<composed>")
        assert(format:can_read() and format:can_write())
        assert(format:encode(b(1), "Hello") == b(1, 5) .. "Hello")
        local long = ("a"):rep(300)
        local wire = format:encode(b(1), long)
        assert(wire == b(1, 0xFF, 0x01, 0x2C) .. long)
        local encoding = format:measure(b(1), long)
        assert(encoding.header == 4 and encoding.total == 304)
        local element, size = format:decode(wire .. "tail")
        assert(element.tag == b(1) and element.value == long and size == 304)
        local err = failure(function() format:measure(b(1), ("a"):rep(65536)) end,
            tlv.errors.INVALID_LENGTH)
        assert(err.operation == "measure")
    end)

    it("selects element order and length scope", function()
        local format = tlv.formats.compose {
            tag = {kind = "fixed", size = 1},
            length = {kind = "fixed", size = 1},
            element_order = "ltv",
            length_scope = "tag_and_value",
        }
        assert(format:encode(b(9), "abc") == b(4, 9) .. "abc")
        if tlv.formats.bluetooth_ltv then
            assert(tlv.formats.bluetooth_ltv:encode(b(9), "abc") == b(4, 9) .. "abc")
        end
        local escaped_ltv = tlv.formats.compose {
            tag = {kind = "fixed", size = 1},
            length = {kind = "escaped", escape = 0xFF, extended_size = 2, byte_order = "little"},
            element_order = "ltv",
            length_scope = "tag_and_value",
        }
        assert(escaped_ltv:encode(b(7), "abc") == b(4, 7) .. "abc")
        local long = ("z"):rep(299)
        assert(escaped_ltv:encode(b(7), long) == b(0xFF, 0x2C, 0x01, 7) .. long)
    end)

    it("matches BER framing with variable identifiers and counts", function()
        local format = variable()
        local amount = b(0, 0, 0, 0, 1, 0)
        assert(format:encode(b(0x9F, 0x02), amount) == tlv.formats.ber:encode(b(0x9F, 0x02), amount))
        local long = ("v"):rep(200)
        assert(format:encode(b(4), long) == b(4, 0x81, 200) .. long)
        local element, size = format:decode(b(0x9F, 0x02, 1, 7))
        assert(element.tag == b(0x9F, 0x02) and element.value == b(7) and size == 4)
    end)

    it("applies identifier and length policies", function()
        local format = variable {
            tag_policy = {forbidden_leading = b(0), reject_zero_first_payload = true},
            length_policy = {max_long_octets = 2, max_value = 255, require_minimal = true},
        }
        failure(function() format:decode(b(0, 1) .. "x") end, tlv.errors.INVALID_TAG)
        failure(function() format:decode(b(0x1F, 0x80, 0x01, 0)) end, tlv.errors.INVALID_TAG)
        failure(function() format:measure(b(4), ("x"):rep(256)) end, tlv.errors.INVALID_LENGTH)
        failure(function() format:decode(b(4, 0x81, 1, 0)) end, tlv.errors.INVALID_LENGTH)
        assert(variable():decode(b(4, 0x81, 1, 0)).value == b(0))
        assert(format:encode(b(4), ("x"):rep(255)):sub(1, 3) == b(4, 0x81, 255))
        local unlimited = variable {length_policy = {max_value = math.huge}}
        assert(unlimited:measure(b(4), "x").total == 3)
    end)

    it("composes mixed encoding pairs through the generic field composition", function()
        local fixed_variable = tlv.formats.compose {
            tag = {kind = "fixed", size = 2},
            length = {kind = "variable", long_form_bit = 0x80, payload_mask = 0x7F},
        }
        local long = ("m"):rep(200)
        local wire = fixed_variable:encode(b(1, 2), long)
        assert(wire == b(1, 2, 0x81, 200) .. long)
        local element, size = fixed_variable:decode(wire)
        assert(element.tag == b(1, 2) and element.value == long and size == 204)
        local variable_fixed = tlv.formats.compose {
            tag = {kind = "variable", inline_mask = 0x1F, escape = 0x1F, continuation_bit = 0x80,
                   payload_mask = 0x7F, max_size = 2},
            length = {kind = "fixed", size = 2, byte_order = "little"},
        }
        assert(variable_fixed:encode(b(0x9F, 0x02), "ab") == b(0x9F, 0x02, 2, 0) .. "ab")
        local variable_escaped = tlv.formats.compose {
            tag = {kind = "variable", inline_mask = 0x1F, escape = 0x1F, continuation_bit = 0x80,
                   payload_mask = 0x7F, max_size = 2},
            length = {kind = "escaped", escape = 0xF0, extended_size = 1, min_extended = 0xF0},
        }
        assert(variable_escaped:encode(b(4), ("e"):rep(0xF1)):sub(1, 3) == b(4, 0xF0, 0xF1))
        assert(variable_escaped:decode(b(4, 2) .. "ee").value == "ee")
        failure(function() variable_escaped:decode(b(4, 0xF0, 2) .. "ee") end,
            tlv.errors.INVALID_LENGTH)
    end)

    it("frames tag-only identifiers without Length and Value", function()
        local configs = {
            {tag = {kind = "fixed", size = 1}, length = {kind = "fixed", size = 1}},
            {tag = {kind = "fixed", size = 1}, length = {kind = "escaped", escape = 0xFF,
                                                         extended_size = 2}},
            {tag = {kind = "fixed", size = 1}, length = {kind = "variable", long_form_bit = 0x80,
                                                         payload_mask = 0x7F}},
        }
        for _, config in ipairs(configs) do
            config.tag_only = {b(0)}
            local format = tlv.formats.compose(config)
            assert(format:encode(b(0), "") == b(0))
            local element, size = format:decode(b(0, 1, 1) .. "x")
            assert(element.tag == b(0) and element.value == "" and size == 1)
            local tags = {}
            for item in tlv.reader(b(0, 1, 1) .. "x", format) do tags[#tags + 1] = item.tag end
            assert(#tags == 2 and tags[2] == b(1))
            failure(function() format:encode(b(0), "x") end, tlv.errors.INVALID_LENGTH)
        end
        local tagged = variable {tag_only = {b(5)}}
        assert(tagged:encode(b(5), "") == b(5))
        local depths = {}
        tlv.visit_tree(b(5, 0xE1, 3, 4, 1) .. "x", tagged, function(element, depth)
            depths[#depths + 1] = {element.tag, depth, element.constructed}
        end)
        assert(#depths == 3 and depths[2][3] == true and depths[3][1] == b(4))
        assert(depths[3][2] == 1)
    end)

    it("snapshots its configuration tables", function()
        local tags = {b(0)}
        local policy = {forbidden_leading = b(0)}
        local config = {
            tag = {kind = "fixed", size = 1},
            length = {kind = "fixed", size = 1},
            tag_only = tags,
        }
        local format = tlv.formats.compose(config)
        local guarded = variable {tag_policy = policy}
        tags[1] = b(7)
        config.length.size = 4
        policy.forbidden_leading = b(4)
        collectgarbage("collect")
        assert(format:encode(b(0), "") == b(0))
        assert(format:encode(b(7), "x") == b(7, 1) .. "x")
        assert(guarded:encode(b(4), "x") == b(4, 1) .. "x")
        failure(function() guarded:decode(b(0, 0)) end, tlv.errors.INVALID_TAG)
    end)

    it("rejects invalid configuration through Core", function()
        local function compose(tag, length, extra)
            local config = {tag = tag, length = length}
            for key, value in pairs(extra or {}) do config[key] = value end
            return function() return tlv.formats.compose(config) end
        end
        local fixed1 = {kind = "fixed", size = 1}
        local fixed_length = {kind = "fixed", size = 1}
        local cases = {
            compose({kind = "fixed", size = 0}, fixed_length),
            compose(fixed1, {kind = "fixed", size = 9}),
            compose(fixed1, {kind = "escaped", escape = 0, extended_size = 2}),
            compose({kind = "fixed", size = 0}, {kind = "variable", long_form_bit = 0x80,
                                                 payload_mask = 0x7F}),
            compose({kind = "variable", inline_mask = 0, escape = 0, continuation_bit = 0x80,
                     payload_mask = 0x7F, max_size = 2}, fixed_length),
            compose(fixed1, fixed_length, {constructed = {mask = 0, value = 0}}),
            compose(fixed1, fixed_length, {constructed = {mask = 0x20, value = 0x40}}),
            compose(fixed1, fixed_length, {tag_only = {b(1, 2)}}),
            compose(fixed1, fixed_length, {tag_only = {b(1)}, element_order = "ltv"}),
        }
        for _, case in ipairs(cases) do failure(case, tlv.errors.INVALID_ARG) end
        for _, case in ipairs({
            compose(nil, fixed_length),
            compose(fixed1, nil),
            compose({size = 1}, fixed_length),
            compose({kind = "escaped", size = 1}, fixed_length),
            compose(fixed1, {kind = "fixed", size = 1, byte_order = "middle"}),
            compose(fixed1, fixed_length, {element_order = "vlt"}),
            compose(fixed1, fixed_length, {tag_only = {1}}),
            compose(fixed1, fixed_length, {constructed = {mask = 256, value = 0}}),
            compose({kind = "fixed", size = -1}, fixed_length),
            compose({kind = "variable", inline_mask = 0x1F, escape = 0x1F, continuation_bit = 0x80,
                     payload_mask = 0x7F, max_size = 2, policy = {require_minimal = 1}},
                    fixed_length),
        }) do
            local err = failure(case)
            assert(type(err) == "string", tostring(err))
        end
        failure(function() tlv.formats.compose() end)
    end)

    it("works with every Format entry point", function()
        local format = escaped()
        local writer = tlv.tree_writer(format, {capacity = 64})
        writer:begin(b(0xE1))
        writer:write(b(4), "x")
        writer:end_element()
        local data = writer:finish()
        assert(data == b(0xE1, 3, 4, 1) .. "x")
        local flat = tlv.writer(format)
        flat:write(b(1), "abc")
        assert(flat:bytes() == b(1, 3) .. "abc" and flat:format() == format)
        local reader = tlv.reader(data, format)
        assert(reader:next().value == b(4, 1) .. "x" and reader:format() == format)
        local seen = {}
        tlv.visit_tree(data, format, function(element, depth) seen[#seen + 1] = depth end)
        assert(#seen == 2 and seen[2] == 1)
        local matches = tlv.query("E1/04"):evaluate(data, format)
        assert(#matches == 1 and matches[1].value == "x" and matches[1].depth == 1)
        local schema = tlv.schema {rules = {{tag = b(0xE1), kind = "constructed"}}}
        assert(schema:validate(data, format).ok)
        if tlv.document then
            local document = tlv.document(data, format)
            assert(document:find("E1/04"):value() == "x")
            assert(document:set("E1/04", "yz"))
            assert(document:serialize() == b(0xE1, 4, 4, 2) .. "yz")
        end
    end)

    it("stays alive while a Reader, Document or Query program uses it", function()
        local reader = tlv.reader(b(0xE1, 3, 4, 1) .. "x", escaped())
        local document = tlv.document and tlv.document(b(0xE1, 3, 4, 1) .. "x", escaped())
        local program = tlv.query_program("count(//04)", escaped())
        collectgarbage("collect")
        collectgarbage("collect")
        assert(reader:next().tag == b(0xE1))
        local execution = program:execution()
        execution:set_input(b(0xE1, 3, 4, 1) .. "x")
        execution:finish()
        assert(execution:result() == 1)
        if document then
            assert(document:find("E1/04"):value() == "x")
            local format = escaped()
            local nodes = tlv.query_program("//04", format):execution()
            nodes:evaluate_document(tlv.document(b(0xE1, 3, 4, 1) .. "y", format))
            assert(nodes:next():value() == "y")
        end
    end)
end)

describe("opentlv.formats.packed", function()
    it("composes a packed Tag/Length header", function()
        local format = lldp()
        assert(tostring(format) == "opentlv.Format<packed>")
        local element, size = format:decode(b(6, 2, 0, 120))
        assert(element.tag == b(3) and element.value == b(0, 120) and size == 4)
        assert(format:encode(b(3), b(0, 120)) == b(6, 2, 0, 120))
        if tlv.formats.lldp then
            assert(tlv.formats.lldp:encode(b(3), b(0, 120)) == b(6, 2, 0, 120))
        end
        failure(function() format:encode(b(0x80), "") end, tlv.errors.INVALID_TAG)
        failure(function() format:measure(b(1), ("x"):rep(512)) end, tlv.errors.INVALID_LENGTH)
        local wide = tlv.formats.packed {
            header_size = 2,
            byte_order = "little",
            tag = {bit_offset = 12, bit_width = 4},
            length = {bit_offset = 0, bit_width = 12},
            tag_size = 2,
        }
        assert(wide:encode(b(0, 5), "ab") == b(2, 0x50) .. "ab")
        assert(wide:decode(b(2, 0x50) .. "ab").tag == b(0, 5))
        local counted = tlv.formats.packed {
            header_size = 1,
            tag = {bit_offset = 4, bit_width = 4},
            length = {bit_offset = 0, bit_width = 4},
            length_scope = "tag_and_value",
            constructed = {mask = 0x08, value = 0x08},
        }
        assert(counted:encode(b(2), "ab") == b(0x23) .. "ab")
        local depths = {}
        tlv.visit_tree(b(0x83, 0x22) .. "a", counted, function(element, depth)
            depths[#depths + 1] = depth
        end)
        assert(#depths == 2 and depths[2] == 1)
    end)

    it("rejects invalid layouts", function()
        local function packed(config)
            return function() return tlv.formats.packed(config) end
        end
        for _, case in ipairs({
            packed {header_size = 2, tag = {bit_offset = 8, bit_width = 8},
                    length = {bit_offset = 0, bit_width = 9}},
            packed {header_size = 2, tag = {bit_offset = 9, bit_width = 0},
                    length = {bit_offset = 0, bit_width = 9}},
            packed {header_size = 9, tag = {bit_offset = 9, bit_width = 7},
                    length = {bit_offset = 0, bit_width = 9}},
            packed {header_size = 2, tag = {bit_offset = 9, bit_width = 7},
                    length = {bit_offset = 0, bit_width = 9}, tag_size = 9},
        }) do
            failure(case, tlv.errors.INVALID_ARG)
        end
        local err = failure(packed {header_size = 4, tag = {bit_offset = 0, bit_width = 17},
                                    length = {bit_offset = 17, bit_width = 8}})
        assert(type(err) == "string" and err:find("16 bits", 1, true))
        assert(type(failure(packed {header_size = 2, tag = {bit_width = 7},
                                    length = {bit_offset = 0, bit_width = 9}})) == "string")
    end)
end)

describe("opentlv.fields", function()
    it("reads and writes one packed bit field", function()
        local flags = {header_size = 1, bit_offset = 5, bit_width = 3}
        assert(tlv.fields.read_packed(flags, b(0xA4)) == 5)
        local header = b(0xA4)
        assert(tlv.fields.write_packed(flags, header, 2) == b(0x44))
        assert(header == b(0xA4))
        local length = {header_size = 2, bit_offset = 0, bit_width = 9, byte_order = "little"}
        assert(tlv.fields.read_packed(length, b(1, 1, 0xFF)) == 257)
        assert(tlv.fields.write_packed(length, b(0, 0xFE, 7), 3) == b(3, 0xFE, 7))
        local all = {header_size = 8, bit_offset = 0, bit_width = 64}
        local maximum = "18446744073709551615"
        local encoded = tlv.fields.write_packed(all, ("\0"):rep(8), maximum)
        assert(encoded == ("\255"):rep(8))
        assert(tostring(tlv.fields.read_packed(all, encoded)) == maximum or
               tlv.fields.read_packed(all, encoded) == 2 ^ 64)
    end)

    it("reports Core failures and leaves input unchanged", function()
        local flags = {header_size = 1, bit_offset = 5, bit_width = 3}
        failure(function() tlv.fields.write_packed(flags, b(0), 8) end, tlv.errors.OVERFLOW)
        failure(function() tlv.fields.read_packed(flags, "") end, tlv.errors.TRUNCATED)
        failure(function() tlv.fields.write_packed(flags, "", 1) end, tlv.errors.BUFFER_TOO_SHORT)
        failure(function()
            tlv.fields.read_packed({header_size = 1, bit_offset = 6, bit_width = 3}, b(0))
        end, tlv.errors.INVALID_ARG)
        failure(function() tlv.fields.read_packed(flags, 1) end)
        failure(function() tlv.fields.write_packed(flags, b(0), -1) end)
        failure(function() tlv.fields.read_packed({bit_offset = 0, bit_width = 1}, b(0)) end)
    end)
end)
