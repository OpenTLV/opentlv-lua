-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

-- Schema-number and structure codecs and the wire Generator against the
-- pinned Core. Core owns conversion, validation and generation; these cases
-- check the binding's conversion, ownership, callback and diagnostic contract.
local tlv = require("opentlv")
local describe = describe or function(_, run) run() end
local it = it or function(_, run) run() end
local b = string.char
local fixed = tlv.formats.fixed(1, 1, "big")
local ber = tlv.formats.ber

local inject_failure
if arg and arg[1] == "--allocator" then
    inject_failure = require("opentlv_test_allocator")
else
    local ok, module = pcall(require, "opentlv_test_allocator")
    if ok then inject_failure = module end
end

local function failure(run, code)
    local ok, err = pcall(run)
    assert(not ok, "expected failure")
    if code then assert(type(err) == "table" and err.code == code, tostring(err)) end
    return err
end

-- A flat sequence of 01 (identifier, two bytes) and optional 02 elements.
local RECORD = {rules = {{tag = b(1), name = "identifier", min_occurs = 1, max_occurs = 1,
                          min_length = 2, max_length = 2},
                         {tag = b(2), max_occurs = 1}}}

local function record_codec(options)
    options = options or {}
    local calls = {decode = 0, encode = 0}
    options.format = options.format or fixed
    options.decode = options.decode or function(bytes)
        calls.decode = calls.decode + 1
        return {identifier = bytes:sub(3, 2 + bytes:byte(2)), size = #bytes}
    end
    options.encode = options.encode or function(value)
        calls.encode = calls.encode + 1
        return b(1, #value.identifier) .. value.identifier
    end
    return tlv.structure_codec(options), calls
end

-- Reads every root element of a complete stream with the given Format.
local function roots(data, format)
    local elements = {}
    for element in tlv.reader(data, format) do elements[#elements + 1] = element end
    return elements
end

describe("Schema-number codec", function()
    local AMOUNT = {tag = b(0x9F, 0x02), name = "amount", min_length = 2, max_length = 4}

    it("encodes in the shortest width the rule permits", function()
        local codec = tlv.codecs.schema_number {rule = AMOUNT}
        assert(codec:encode(5) == b(0, 5))
        assert(codec:encode(0x123456) == b(0x12, 0x34, 0x56))
        assert(codec:decode(b(0, 5)) == 5 and codec:decode(b(0, 0, 1, 0)) == 256)
        local bcd = tlv.codecs.schema_number {rule = AMOUNT, encoding = "bcd", digits = 6}
        assert(bcd:encode(1234) == b(0x12, 0x34) and bcd:decode(b(0, 0x12, 0x34)) == 1234)
        local le = tlv.codecs.schema_number {rule = AMOUNT, encoding = "binary_le"}
        assert(le:encode(258) == b(2, 1))
    end)

    it("checks Value lengths against the rule before conversion", function()
        local codec = tlv.codecs.schema_number {rule = AMOUNT}
        local err = failure(function() codec:decode(b(5)) end, tlv.errors.SCHEMA)
        assert(err.codec_detail.cause_name == "schema")
        assert(err.codec_detail.schema.tag == AMOUNT.tag)
        failure(function() codec:encode("4294967296") end, tlv.errors.SCHEMA)
        local fixed_width = tlv.codecs.schema_number {rule = AMOUNT, width = 1}
        failure(function() fixed_width:encode(1) end, tlv.errors.SCHEMA)
        local multiple = tlv.codecs.schema_number {rule = {tag = b(1), length_multiple = 3}}
        assert(multiple:encode(1) == b(0, 0, 1))
        failure(function() tlv.codecs.schema_number {rule = {tag = b(1), min_length = 3,
                                                            max_length = 2}}:encode(1) end,
                tlv.errors.INVALID_SCHEMA)
        failure(function() codec:encode(-1) end)
    end)

    it("snapshots and retains its rule", function()
        local rule = {tag = b(0x9F, 0x02) .. "", min_length = 2, max_length = 4, name = "amount"}
        local codec = tlv.codecs.schema_number {rule = rule}
        rule.min_length, rule.tag = 1, b(1)
        rule = nil
        collectgarbage("collect")
        assert(codec:encode(1) == b(0, 1))
        assert(failure(function() codec:decode(b(1)) end).codec_detail.schema.tag ==
               b(0x9F, 0x02))
        failure(function() tlv.codecs.schema_number {} end)
        failure(function() tlv.codecs.schema_number {rule = {min_length = 1}} end)
        failure(function() tlv.codecs.schema_number {rule = {tag = b(1)}, digits = 19} end)
    end)

    it("decodes Query provider Values in C", function()
        local codec = tlv.codecs.schema_number {rule = {tag = b(0x5A), min_length = 1,
                                                        max_length = 2}}
        local program = tlv.query_program("num(//5A)", fixed,
                                          {providers = {num = {id = 401, codec = codec}}})
        codec = nil
        collectgarbage("collect")
        local execution = program:execution()
        execution:set_input(b(0x5A, 2, 1, 0))
        execution:finish()
        assert(execution:result() == 256)
        execution = program:execution()
        execution:set_input(b(0x5A, 3, 1, 0, 0))
        failure(function() execution:finish() end, tlv.errors.SCHEMA)
    end)
end)

describe("structure codec", function()
    it("validates complete sequences before decode and after encode", function()
        local codec, calls = record_codec {schema = tlv.schema(RECORD)}
        local data = codec:encode {identifier = "AB"}
        assert(data == b(1, 2, 65, 66) and calls.encode == 1)
        local value = codec:decode(b(1, 2, 65, 66, 2, 0))
        assert(value.identifier == "AB" and value.size == 6 and calls.decode == 1)

        local err = failure(function() codec:decode(b(2, 0)) end, tlv.errors.SCHEMA)
        assert(err.codec_detail.cause_name == "schema" and
               err.codec_detail.schema.kind_name == "missing")
        assert(err.codec_detail.operation_name == "decode")
        err = failure(function() codec:decode(b(1, 3, 65)) end)
        assert(err.codec_detail.cause_name == "reader")
        assert(calls.decode == 1)

        err = failure(function() codec:encode {identifier = "ABC"} end, tlv.errors.SCHEMA)
        assert(err.location.domain == "output" and err.codec_detail.operation_name == "encode")
        assert(calls.encode == 2)
    end)

    it("checks framing and limits without a Schema", function()
        local codec = record_codec {}
        assert(codec:decode(b(1, 1, 65, 9, 0)).identifier == "A")
        failure(function() codec:decode(b(1, 2, 65)) end)
        local limited = record_codec {max_elements = 1}
        failure(function() limited:decode(b(1, 1, 65, 9, 0)) end)
        assert(limited:decode(b(1, 1, 65)).size == 3)
        if ber then
            local nested = tlv.structure_codec {max_depth = 0, decode = function(bytes) return #bytes end}
            assert(nested:decode(b(0x5A, 0)) == 2)
            failure(function() nested:decode(b(0x70, 2, 0x5A, 0)) end)
            local deep = tlv.structure_codec {decode = function(bytes) return #bytes end}
            assert(deep:decode(b(0x70, 2, 0x5A, 0)) == 4)
        end
    end)

    it("accepts a checked Schema and keeps its inputs alive", function()
        local schema = tlv.schema(RECORD)
        local codec = record_codec {schema = schema:check(),
                                    format = tlv.formats.fixed(1, 1, "big")}
        schema = nil
        collectgarbage("collect")
        collectgarbage("collect")
        assert(codec:decode(codec:encode {identifier = "CD"}).identifier == "CD")
        failure(function() codec:decode(b(1, 1, 65)) end, tlv.errors.SCHEMA)
        failure(function() tlv.structure_codec {schema = {}} end)
        failure(function() tlv.structure_codec {format = {}} end)
    end)

    it("reports UNSUPPORTED for a missing direction", function()
        local decoder = tlv.structure_codec {format = fixed, decode = function() return 1 end}
        local encoder = tlv.structure_codec {format = fixed, encode = function() return "" end}
        assert(decoder:decode("") == 1 and encoder:encode(1) == "")
        failure(function() decoder:encode(1) end, tlv.errors.UNSUPPORTED)
        failure(function() encoder:decode("") end, tlv.errors.UNSUPPORTED)
        failure(function() tlv.structure_codec {decode = 1} end)
    end)

    it("propagates callback errors and checks encode results", function()
        local marker = {}
        local codec = tlv.structure_codec {format = fixed,
                                           decode = function() error(marker) end,
                                           encode = function() return 1 end}
        local ok, err = pcall(codec.decode, codec, "")
        assert(not ok and err == marker)
        ok, err = pcall(codec.encode, codec, {})
        assert(not ok and type(err) == "string")
        failure(function() codec:decode(1) end)
    end)

    it("snapshots callbacks and supports recursion and collection", function()
        local options = {format = fixed}
        local depth = 0
        local codec
        options.decode = function(bytes)
            depth = depth + 1
            if depth < 3 then codec:decode(bytes) end
            return depth
        end
        codec = tlv.structure_codec(options)
        options.decode = function() return "changed" end
        assert(codec:decode(b(1, 0)) == 3)
        local weak = setmetatable({}, {__mode = "v"})
        do
            local cyclic
            cyclic = tlv.structure_codec {decode = function() return cyclic end}
            weak[1] = cyclic
        end
        collectgarbage("collect")
        collectgarbage("collect")
        assert(weak[1] == nil)
    end)
end)

describe("Generator", function()
    local function generator(overrides)
        local config = {format = fixed, seed = 1,
                        candidates = {{tag = b(1), min_value_size = 0, max_value_size = 4},
                                      {tag = b(2), min_value_size = 1, max_value_size = 2}},
                        max_elements = 6, max_depth = 0, max_value_size = 4, max_case_size = 32}
        for key, value in pairs(overrides or {}) do config[key] = value end
        return tlv.generator(config)
    end

    it("reproduces cases from the seed and case index", function()
        assert(tlv.GENERATOR_VERSION == 2)
        local first, second = generator(), generator()
        assert(first:generate() == first:generate(0))
        local distinct = {}
        for index = 0, 15 do
            local case = first:generate(index)
            assert(case == second:generate(index) and #case > 0 and #case <= 32)
            distinct[case] = true
            local elements = roots(case, fixed)
            assert(#elements > 0 and #elements <= 6)
            for _, element in ipairs(elements) do
                assert(element.tag == b(1) and #element.value <= 4 or
                       element.tag == b(2) and #element.value >= 1 and #element.value <= 2)
            end
        end
        assert(next(distinct, next(distinct)) ~= nil)
        local other = generator {seed = 2}
        local changed = false
        for index = 0, 15 do changed = changed or other:generate(index) ~= first:generate(index) end
        assert(changed)
        local large = generator {seed = "18446744073709551615"}
        assert(large:generate("18446744073709551615") ==
               generator {seed = "18446744073709551615"}:generate("18446744073709551615"))
    end)

    if ber then
        it("nests constructed candidates within depth and size limits", function()
            local nested = generator {format = ber, max_depth = 2, max_case_size = 64,
                                      max_value_size = 32, max_elements = 12,
                                      candidates = {{tag = b(0x5A), min_value_size = 0,
                                                     max_value_size = 8},
                                                    {tag = b(0x70), min_value_size = 0,
                                                     max_value_size = 32}}}
            local depth = 0
            for index = 0, 31 do
                local case = nested:generate(index)
                assert(#case <= 64)
                local items = 0
                for item in tlv.tree_reader(case, ber) do
                    items = items + 1
                    if item.depth > depth then depth = item.depth end
                    assert(item.depth <= 2)
                end
                assert(items <= 12)
            end
            assert(depth > 0)
        end)

        it("rejects identifiers the Format cannot write", function()
            for _, tag in ipairs({false, ""}) do
                local candidate = {min_value_size = 0, max_value_size = 1, tag = tag or nil}
                failure(function() generator {format = ber, candidates = {candidate}}:generate() end,
                        tlv.errors.LIMIT)
            end
        end)
    end

    it("reports the workspace and snapshots its configuration", function()
        local candidates = {{tag = b(1), min_value_size = 1, max_value_size = 1}}
        local config = {format = tlv.formats.fixed(1, 1, "big"), candidates = candidates,
                        max_elements = 4, max_depth = 3, max_value_size = 1, max_case_size = 10}
        local generated = tlv.generator(config)
        local expected = generated:generate(5)
        assert(generated:workspace_size() == 40)
        candidates[1].tag, candidates[1].max_value_size = b(2), 2
        candidates[2] = {tag = b(3), min_value_size = 0, max_value_size = 0}
        config.format, config.max_case_size, config = nil, 1, nil
        collectgarbage("collect")
        collectgarbage("collect")
        assert(generated:generate(5) == expected and generated:workspace_size() == 40)
    end)

    it("raises Core configuration results", function()
        failure(function() generator {candidates = {}}:generate() end, tlv.errors.INVALID_ARG)
        failure(function() generator {max_elements = 0}:workspace_size() end,
                tlv.errors.INVALID_ARG)
        failure(function() generator {max_depth = 65}:generate() end, tlv.errors.UNSUPPORTED)
        failure(function() generator {candidates = {{tag = b(1), min_value_size = 3,
                                                     max_value_size = 2}}}:generate() end,
                tlv.errors.INVALID_ARG)
        failure(function() generator {candidates = {{tag = b(1), min_value_size = 8,
                                                     max_value_size = 9}}}:generate() end,
                tlv.errors.INVALID_ARG)
        failure(function() generator {max_case_size = 1}:generate() end, tlv.errors.LIMIT)
        failure(function() generator {format = false} end)
        failure(function() tlv.generator {candidates = {}, max_elements = 1, max_depth = 0,
                                          max_value_size = 1, max_case_size = 1} end)
        failure(function() generator {max_case_size = -1} end)
        failure(function() generator {seed = 1.5} end)
        failure(function() generator {candidates = {{tag = 1, min_value_size = 0,
                                                     max_value_size = 0}}} end)
        failure(function() generator {candidates = {{tag = b(1), max_value_size = 0}}} end)
        failure(function() generator():generate(-1) end)
    end)
end)

if inject_failure then
    describe("allocation failures", function()
        it("leave codecs and generators usable", function()
            local codec = record_codec {schema = tlv.schema(RECORD)}
            local failures, successes = 0, 0
            for budget = 0, 120 do
                local ok = inject_failure(function()
                    local number = tlv.codecs.schema_number {rule = {tag = b(1), name = "n",
                                                                     max_length = 2}}
                    assert(number:decode(number:encode(258)) == 258)
                    local created = record_codec {schema = tlv.schema(RECORD):check()}
                    assert(created:decode(created:encode {identifier = "AB"}).identifier == "AB")
                    assert(codec:decode(b(1, 2, 65, 66)).identifier == "AB")
                    local generated = tlv.generator {format = fixed, seed = 3,
                        candidates = {{tag = b(1), min_value_size = 0, max_value_size = 2}},
                        max_elements = 3, max_depth = 0, max_value_size = 2, max_case_size = 12}
                    assert(#generated:generate(1) > 0 and generated:workspace_size() == 12)
                end, budget)
                if ok then successes = successes + 1 else failures = failures + 1 end
                for _ = 1, 3 do collectgarbage("collect") end
            end
            assert(failures > 0 and successes > 0)
            assert(codec:encode {identifier = "CD"} == b(1, 2, 67, 68))
        end)
    end)
end
