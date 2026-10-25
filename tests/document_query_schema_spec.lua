-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

-- Document, Query and Schema operations completed against the pinned Core.
-- Core owns the tree, matching and validation semantics; these cases check the
-- binding's conversion, ownership, lifetime and diagnostic contract.
local tlv = require("opentlv")
local describe = describe or function(_, run) run() end
local it = it or function(_, run) run() end
local b = string.char
local fixed = tlv.formats.fixed(1, 1, "big")
local ber = tlv.formats.ber

-- An FCI Template (6F) holding a DF Name (84) and an FCI Proprietary
-- Template (A5) holding an Application Label (50).
local FCI = b(0x6F, 0x0A, 0x84, 0x03, 0x41, 0x42, 0x43, 0xA5, 0x03, 0x50, 0x01, 0x01)
local AMOUNT = b(0x9F, 0x02, 0x01, 0x05)
local FLAT = b(1, 1, 65, 2, 0, 1, 1, 66)
local RULES = {rules = {{tag = b(1), name = "identifier", min_occurs = 1, max_occurs = 1,
                         min_length = 2, max_length = 2}}}

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

if tlv.document then
    describe("Document", function()
        it("counts successful edits and keeps the revision on failure", function()
            local doc = tlv.document(b(1, 1, 65, 2, 0), fixed)
            assert(doc:revision() == 0)
            assert(doc:set("01", "B") and doc:revision() == 1)
            doc:insert(b(3), "C")
            assert(doc:revision() == 2)
            failure(function() doc:insert(b(4, 4), "x") end)
            failure(function() doc:insert(b(4), "x", "09") end)
            assert(doc:revision() == 2)
            assert(doc:erase("02") and doc:revision() == 3)
        end)

        it("checks Node handles with Core identities across retirement", function()
            local doc = tlv.document(b(1, 1, 65, 2, 1, 66, 3, 1, 67), fixed)
            local first, second, third = doc:find("01"), doc:find("02"), doc:find("03")
            local identity, retired = first:identity(), second:identity()
            assert(first:set("Z") and first:value() == "Z" and first:identity() == identity)
            second:erase()
            failure(function() second:value() end)
            assert(first:value() == "Z" and third:value() == "C")
            for _ = 1, 8 do assert(doc:insert(b(2), "x"):identity() ~= retired) end
            failure(function() second:tag() end)
            assert(doc:find("02"):identity() ~= retired)
            if ber then
                local nested = tlv.document(FCI, ber)
                local template, label = nested:find("6F"), nested:find("6F/A5/50")
                failure(function() template:set(b(0x84, 4, 0x41)) end, tlv.errors.TRUNCATED)
                assert(label:value() == b(1))
                assert(template:set(b(0x84, 1, 0x41)))
                failure(function() label:value() end)
                assert(template:first_child():value() == "A")
            end
        end)

        if ber then
            it("reports retained source locations of unchanged Nodes", function()
                local doc = tlv.document(FCI, ber, {retain_source_locations = true})
                local label, name = doc:find("6F/A5/50"), doc:find("6F/84")
                local location = label:source_location()
                assert(location.offset == 9 and location.header_size == 2)
                assert(tlv.document(FCI, ber):find("6F/A5/50"):source_location() == nil)
                assert(doc:insert(b(0x9F, 0x02), b(1)):source_location() == nil)
                assert(label:set(b(2)))
                assert(label:source_location() == nil and doc:find("6F"):source_location() == nil)
                assert(name:source_location().offset == 2)
            end)

            it("finds direct children by tag", function()
                local doc = tlv.document(FCI .. AMOUNT, ber)
                local template = doc:find_child(b(0x6F))
                assert(template and template:tag() == b(0x6F))
                assert(doc:find_child(b(0x9F, 0x02)):value() == b(5))
                assert(doc:find_child(b(0x84), template):value() == "ABC")
                assert(doc:find_child(b(0xA5), "6F"):is_constructed())
                assert(doc:find_child(b(0x50)) == nil and doc:find_child(b(0x99), template) == nil)
                local other = tlv.document(FCI, ber)
                failure(function() doc:find_child(b(0x84), other:first()) end)
                failure(function() doc:find_child(b(0x84), "99") end)
            end)

            it("visits path matches as Nodes and rejects mutation during callbacks", function()
                local doc = tlv.document(b(0xE1, 0, 0xE1, 6, 1, 1, 65, 1, 1, 66, 2, 0), ber)
                local seen = {}
                local visited, stopped = doc:visit("E1/01", function(node)
                    seen[#seen + 1] = node:value()
                end)
                assert(visited == 2 and not stopped and seen[1] == "A" and seen[2] == "B")
                visited, stopped = doc:visit(tlv.query("E1/01"), function() return false end)
                assert(visited == 1 and stopped)
                local token = {}
                local ok, err = pcall(doc.visit, doc, "E1/01", function() error(token) end)
                assert(not ok and err == token)
                doc:visit("E1/01", function(node)
                    failure(function() node:set("Z") end, tlv.errors.INVALID_STATE)
                    failure(function() doc:erase("02") end, tlv.errors.INVALID_STATE)
                    failure(function() doc:close() end, tlv.errors.INVALID_STATE)
                    assert(#doc:query("E1/01") == 2)
                end)
                assert(doc:revision() == 0)
                local syntax = failure(function() doc:visit("E1/", function() end) end,
                                       tlv.errors.SYNTAX)
                assert(syntax.query.kind_name == "syntax" and syntax.query.cause_name == "none")
                failure(function() doc:visit("E1") end)
                assert(doc:set("02", "Q") and #doc:query("E1/01") == 2)
            end)

            it("builds Documents from a whole stream or one subtree of a Tree Reader", function()
                local reader = tlv.tree_reader(FCI .. AMOUNT, ber)
                local doc = reader:document_builder():consume()
                assert(doc:count() == 5 and doc:serialize() == FCI .. AMOUNT)
                assert(reader:next() == nil)

                reader = tlv.tree_reader(FCI .. AMOUNT, ber)
                assert(reader:document_builder({subtree = "next"}):consume():serialize() == FCI)
                assert(reader:next().tag == b(0x9F, 0x02) and reader:next() == nil)
                assert(reader:document_builder({subtree = "next"}) == nil)

                reader = tlv.tree_reader(FCI, ber)
                reader:next()
                reader:next()
                failure(function() reader:document_builder({subtree = "root"}) end)
                assert(reader:next().tag == b(0xA5))
                local proprietary = reader:document_builder({subtree = "current",
                                                             retain_source_locations = true})
                doc = proprietary:consume()
                assert(doc:serialize() == b(0xA5, 3, 0x50, 1, 1))
                assert(doc:find("A5/50"):source_location().offset == 9)
                assert(reader:next() == nil)
                failure(function() reader:document_builder({subtree = "current"}) end,
                        tlv.errors.INVALID_STATE)
                failure(function() tlv.tree_reader(FCI, ber):document_builder({max_elements = 2})
                                                            :consume() end, tlv.errors.LIMIT)
                reader = tlv.tree_reader(FCI, ber)
                failure(function()
                    reader:document_builder({subtree = "next", max_elements = 0})
                end, tlv.errors.LIMIT)
                assert(reader:document_builder({subtree = "current"}):consume():count() == 4)
            end)

            it("resumes builders on new input and owns the cursor until completion", function()
                local reader = tlv.tree_reader(FCI:sub(1, 5), ber, {final_input = false})
                local builder = reader:document_builder()
                failure(function() reader:document_builder() end, tlv.errors.INVALID_STATE)
                failure(function() builder:consume() end, tlv.errors.NEED_MORE_DATA)
                failure(function() reader:next() end, tlv.errors.INVALID_STATE)
                failure(function() reader:next_event() end, tlv.errors.INVALID_STATE)
                failure(function() reader:skip_subtree() end, tlv.errors.INVALID_STATE)
                failure(function() reader:visit(function() end) end, tlv.errors.INVALID_STATE)
                assert(not reader:at_end())
                reader:set_input(FCI, 0, true)
                assert(builder:consume():serialize() == FCI)
                failure(function() builder:consume() end, tlv.errors.INVALID_STATE)
                assert(reader:at_end())

                reader = tlv.tree_reader(b(0x6F, 4, 0x84, 9, 0x41, 0x42), ber)
                builder = reader:document_builder()
                failure(function() builder:consume() end, tlv.errors.TRUNCATED)
                failure(function() builder:consume() end, tlv.errors.INVALID_STATE)

                reader = tlv.tree_reader(FCI, ber)
                reader:document_builder():close()
                assert(reader:next().tag == b(0x6F))
            end)

            it("roots the Tree Reader only while building", function()
                local weak = setmetatable({}, {__mode = "v"})
                local builder
                do
                    local reader = tlv.tree_reader(FCI, ber)
                    weak[1] = reader
                    builder = reader:document_builder()
                end
                collectgarbage("collect")
                assert(weak[1] ~= nil)
                local doc = builder:consume()
                for _ = 1, 3 do collectgarbage("collect") end
                assert(weak[1] == nil and doc:serialize() == FCI)
            end)
        end
    end)
end

describe("Query", function()
    it("formats canonical paths", function()
        local query = tlv.query("6f/a5/50")
        assert(query:format() == "6F/A5/50" and tostring(query) == "6F/A5/50")
        assert(tlv.query("9f02"):format() == "9F02")
    end)

    it("visits complete buffers through the native matcher", function()
        local query = tlv.query("01")
        local seen = {}
        local visited, stopped = query:visit(FLAT, fixed, function(match)
            seen[#seen + 1] = match
        end)
        assert(visited == 2 and not stopped and seen[2].value == "B" and seen[2].offset == 5)
        assert(seen[2].depth == 0 and seen[2].constructed == false)
        visited, stopped = query:visit(FLAT, fixed, function() return false end)
        assert(visited == 1 and stopped)
        local token = {}
        local ok, err = pcall(query.visit, query, FLAT, fixed, function() error(token) end)
        assert(not ok and err == token)
        failure(function() query:visit(b(1, 5), fixed, function() end) end, tlv.errors.TRUNCATED)
        failure(function() query:visit(FLAT, fixed, function() end, {max_elements = 1}) end,
                tlv.errors.LIMIT)
        failure(function() query:visit(FLAT, fixed) end)
        if ber then
            local nested = tlv.query("6F/A5/50")
            visited = nested:visit(FCI, nil, function(match)
                assert(match.depth == 2 and match.value == b(1))
            end)
            assert(visited == 1)
            failure(function() nested:visit(FCI, ber, function() end, {max_depth = 1}) end,
                    tlv.errors.LIMIT)
        end
    end)

    it("resumes matcher visits and reports visited, stopped", function()
        local matcher = tlv.query("01"):matcher(fixed)
        matcher:set_input(FLAT)
        local visited, stopped = matcher:visit(function() return false end)
        assert(visited == 1 and stopped)
        local last
        visited, stopped = matcher:visit(function(match) last = match end)
        assert(visited == 1 and not stopped and last.value == "B" and last.depth == 0)
        assert(matcher:next() == nil)
    end)
end)

describe("compiled Query", function()
    it("compiles static names directly and checked resolvers to the same image", function()
        local static = tlv.query_program("count(//a:leaf)", fixed,
                                         {names = {["a:leaf"] = b(0x5a)}})
        local checked = tlv.query_program("count(//a:leaf)", fixed, {resolve = function(space, name)
            if space == "a" and name == "leaf" then return b(0x5a) end
        end})
        assert(static:image() == checked:image())
        assert(static:info().program_size == #static:image())
        local execution = static:execution()
        execution:set_input(b(0x5a, 0, 0x5b, 0, 0x5a, 0))
        execution:finish()
        assert(execution:result() == 2)
        local syntax = failure(function() tlv.query_program("/", fixed) end, tlv.errors.SYNTAX)
        assert(syntax.query.cause == 0 and syntax.query.cause_name == "none")
    end)

    it("opens native plans without the frontend", function()
        local program = tlv.query_program("count(//5A)", fixed)
        local plan = tlv.query_plan_open(program:image(), fixed)
        local info, plan_info = program:info(), plan:plan_info()
        assert(plan_info.program_size == info.program_size)
        assert(plan_info.result_kind == info.result_kind and plan_info.scratch_size == 0)
        assert(program:plan_info().program_size == info.program_size)
        local execution = plan:execution()
        execution:set_input(b(0x5a, 0, 0x5a, 0))
        execution:finish()
        assert(execution:result() == 2)
        for _, image in ipairs({program:image():sub(1, -2), string.rep("\0", #program:image())}) do
            local err = failure(function() tlv.query_plan_open(image, fixed) end)
            assert(err.query and err.query.kind_name ~= "none", tostring(err))
        end
    end)

    it("resets executions in place and keeps their budgets", function()
        local program = tlv.query_program("count(//5A[@len >= $min])", fixed,
                                          {variables = {min = "integer"}})
        local execution = program:execution({max_nodes = 2})
        execution:bind("min", "integer", 1)
        execution:set_input(b(0x5a, 1, 1, 0x5a, 0))
        execution:finish()
        assert(execution:result() == 1)
        execution:reset()
        execution:bind("min", "integer", 0)
        execution:set_input(b(0x5a, 1, 1, 0x5a, 0))
        execution:finish()
        assert(execution:result() == 2)
        execution:reset()
        execution:bind("min", "integer", 0)
        execution:set_input(b(0x5a, 0, 0x5a, 0, 0x5a, 0))
        failure(function() execution:finish() end, tlv.errors.LIMIT)
    end)

    it("decodes provider Values through Value codecs in C", function()
        local codec = tlv.codecs.int64_minimal_be
        local signed = tlv.query_program("num(//5A)", fixed,
                                         {providers = {num = {id = 401, codec = codec}}})
        collectgarbage("collect")
        local execution = signed:execution()
        execution:set_input(b(0x5a, 1, 0xff))
        execution:finish()
        assert(execution:result() == -1)
        local loaded = tlv.query_program_load(signed:image(), fixed,
                                              {providers = {num = {id = 401, codec = codec}}})
        execution = loaded:execution()
        execution:set_input(b(0x5a, 2, 0x01, 0x00))
        execution:finish()
        assert(execution:result() == 256)
        local text = tlv.query_program("text(//5A)", fixed,
                                       {providers = {text = {id = 403, codec = tlv.codecs.bytes}}})
        execution = text:execution()
        execution:set_input(b(0x5a, 2, 0x68, 0x69))
        execution:finish()
        assert(execution:result() == "hi")
        for _, rejected in ipairs({tlv.codecs.uint8, tlv.codecs.ipv4,
                                   tlv.codec {decode = function() end}}) do
            failure(function() tlv.query_program("num(//5A)", fixed,
                {providers = {num = {id = 404, codec = rejected}}}) end, tlv.errors.INVALID_ARG)
        end
    end)

    if tlv.document and ber then
        it("visits Document results as Nodes and sizes constructed Value snapshots", function()
            local doc = tlv.document(b(0x70, 6, 0x5a, 1, 1, 0x5a, 1, 2), ber)
            local execution = tlv.query_program("//5A", ber):execution()
            execution:evaluate_document(doc)
            local values = {}
            execution:visit(function(node)
                values[#values + 1] = node:value()
                return false
            end)
            assert(#values == 1 and values[1] == b(1))
            execution:visit(function(node)
                values[#values + 1] = node:value()
                failure(function() node:set(b(9)) end, tlv.errors.INVALID_STATE)
            end)
            assert(#values == 2 and values[2] == b(2) and execution:next() == nil)
            local token = {}
            execution:reset()
            execution:evaluate_document(doc)
            local ok, err = pcall(execution.visit, execution, function() error(token) end)
            assert(not ok and err == token)

            local value = tlv.query_program("value(//70)", ber)
            assert(value:info().constructed_values_required == 1)
            local scalar = value:execution()
            scalar:evaluate_document(doc)
            assert(scalar:result() == b(0x5a, 1, 1, 0x5a, 1, 2))
            assert(doc:set(doc:find("70/5A"), b(3, 4)))
            scalar:reset()
            scalar:evaluate_document(doc)
            assert(scalar:result() == b(0x5a, 2, 3, 4, 0x5a, 1, 2))
            local short = value:execution()
            failure(function() short:evaluate_document(doc, nil, 2) end)
        end)
    end
end)

describe("Schema", function()
    it("checks a definition once and validates through the checked handle", function()
        local schema = tlv.schema(RULES)
        local checked = schema:check()
        assert(checked:schema() == schema)
        assert(checked:validate(b(1, 2, 0, 0), fixed).ok)
        local direct = schema:validate(b(1, 1, 0), fixed)
        local prepared = checked:validate(b(1, 1, 0), fixed)
        assert(not prepared.ok and prepared.code == direct.code)
        assert(prepared.total_count == direct.total_count and prepared.total_count == 1)
        local violation = prepared.diagnostics[1]
        assert(violation.kind == "length" and violation.cause == "schema")
        local invalid = tlv.schema {rules = {{tag = b(1)}, {tag = b(1)}}}
        local err = failure(function() invalid:check() end, tlv.errors.INVALID_SCHEMA)
        assert(err.cause == "schema")
        local weak = setmetatable({}, {__mode = "v"})
        do
            local source = tlv.schema(RULES)
            weak[1] = source
            checked = source:check()
        end
        collectgarbage("collect")
        assert(weak[1] ~= nil and checked:validate(b(1, 2, 0, 0), fixed).ok)
        checked = nil
        for _ = 1, 3 do collectgarbage("collect") end
        assert(weak[1] == nil)
    end)

    it("stops at the first failure on request", function()
        local schema = tlv.schema(RULES)
        local input = b(1, 1, 0, 2, 0)
        assert(schema:validate(input, fixed).total_count == 2)
        local result = schema:validate(input, fixed, {first = true})
        assert(not result.ok and result.code == tlv.errors.SCHEMA and result.total_count == 1)
        assert(#result.diagnostics == 1 and not result.truncated)
        assert(result.diagnostics[1].cause == "schema")
        assert(schema:validate(b(1, 2, 0, 0), fixed, {first = true}).ok)
        assert(schema:check():validate(b(1, 1, 0), fixed, {first = true}).code == tlv.errors.SCHEMA)
        local truncated = schema:check():validate(b(1, 5), fixed, {first = true})
        assert(truncated.code == tlv.errors.TRUNCATED)
        assert(truncated.diagnostics[1].cause == "reader")
        failure(function() schema:validate(input, fixed, {first = true, unknown = "allow"}) end)
        failure(function() schema:validate(input, fixed, {first = 1}) end)
    end)

    it("looks up per-tag length rules", function()
        local lengths = tlv.length_schema {
            {tag = b(1), name = "short", min_length = 1, max_length = 2},
            {tag = b(2), min_length = 2, max_length = 8, flags = tlv.SCHEMA_LENGTH_ENDPOINTS},
            {tag = b(3), length_multiple = 4},
            {tag = b(1), max_length = 9},
        }
        local rule = lengths:find(b(1))
        assert(rule.tag == b(1) and rule.name == "short")
        assert(rule.min_length == 1 and rule.max_length == 2 and rule.flags == 0)
        assert(lengths:find(b(3)).max_length == math.huge and lengths:find(b(9)) == nil)
        assert(lengths:validate_length(b(1), 2) and not lengths:validate_length(b(1), 3))
        assert(lengths:validate_length(b(2), 8) and not lengths:validate_length(b(2), 5))
        assert(lengths:validate_length(b(3), 8) and not lengths:validate_length(b(3), 6))
        assert(not lengths:validate_length(b(9), 0))
        local reversed = tlv.length_schema {{tag = b(1), min_length = 3, max_length = 2}}
        failure(function() reversed:validate_length(b(1), 2) end, tlv.errors.INVALID_SCHEMA)
        failure(function() lengths:validate_length(b(1), -1) end)
        failure(function() tlv.length_schema {{tag = b(1), name = "a\0b"}} end)
    end)

    it("validates decoded values against value constraints", function()
        local range = tlv.value_constraint {kind = "range", min_value = -5, max_value = 5}
        assert(range:validate(-5) and range:validate(5) and not range:validate(6))
        assert(range:name(0) == nil)
        local colors = tlv.value_constraint {kind = "allowed_values", allowed_values = {0, 1, 2},
                                             allowed_value_names = {"red", nil, "blue"}}
        assert(colors:validate(1) and not colors:validate(3))
        assert(colors:name(0) == "red" and colors:name(1) == nil and colors:name(2) == "blue")
        assert(tlv.value_constraint {}:validate("-9223372036854775808"))
        local wide = tlv.value_constraint {kind = "range", min_value = "9223372036854775806",
                                           max_value = "9223372036854775807"}
        assert(wide:validate("9223372036854775807") and not wide:validate(0))
        failure(function() tlv.value_constraint {kind = "range", min_value = 2, max_value = 1} end,
                tlv.errors.INVALID_SCHEMA)
        failure(function() tlv.value_constraint {kind = "allowed_values", allowed_values = {1},
                                                 allowed_value_names = {"a\0b"}} end)
        failure(function() tlv.value_constraint {kind = "set"} end)
        local values = {7}
        local seven = tlv.value_constraint {kind = "allowed_values", allowed_values = values}
        values[1] = 8
        assert(seven:validate(7) and not seven:validate(8))
    end)
end)

if inject_failure then
    describe("allocation failures", function()
        it("release guards, builders and snapshots", function()
            local shared = tlv.document and tlv.document(FLAT, fixed)
            local failures, successes = 0, 0
            for budget = 0, 200 do
                local ok = inject_failure(function()
                    if shared then
                        local doc = tlv.document(b(1, 1, 65, 2, 0), fixed,
                                                 {retain_source_locations = true})
                        doc:visit("01", function(node) node:source_location() end)
                        doc:find_child(b(2))
                        shared:visit("01", function(node) return node:value() == "A" end)
                        shared:query("01")
                        if ber then tlv.tree_reader(FCI, ber):document_builder():consume() end
                    end
                    tlv.query("01"):visit(FLAT, fixed, function() end)
                    assert(tlv.query("01"):format() == "01")
                    tlv.schema(RULES):check():validate(b(1, 2, 0, 0), fixed, {first = true})
                    tlv.length_schema({{tag = b(1)}}):find(b(1))
                    tlv.value_constraint({kind = "allowed_values", allowed_values = {1},
                                          allowed_value_names = {"one"}}):name(1)
                    local program = tlv.query_program("num(//5A)", fixed, {providers = {
                        num = {id = 405, codec = tlv.codecs.int64_minimal_be}}})
                    tlv.query_plan_open(program:image(), fixed, {providers = {
                        num = {id = 405, codec = tlv.codecs.int64_minimal_be}}}):execution()
                end, budget)
                if ok then successes = successes + 1 else failures = failures + 1 end
                for _ = 1, 3 do collectgarbage("collect") end
            end
            assert(failures > 0 and successes > 0)
            if shared then
                assert(shared:set("01", "Z") and shared:serialize() == b(1, 1, 90, 2, 0, 1, 1, 66))
            end
        end)
    end)
end
