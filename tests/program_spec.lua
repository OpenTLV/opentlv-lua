-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel
local tlv = require("opentlv")
local b = string.char
local f = tlv.formats.ber or tlv.formats.fixed(1, 1, "big")
local function fails(run, code)
    local ok, err = pcall(run)
    assert(not ok, "expected failure")
    if code then assert(type(err) == "table" and err.code == code, tostring(err)) end
    if code == tlv.errors.INVALID_STATE then
        assert(err.query and err.query.kind == 12, "missing Query STATE diagnostic")
        assert(err.query.kind_name == "state")
    end
    return err
end
local syntax = fails(function() tlv.query_program("/", f) end)
assert(syntax.query.kind_name == "syntax")
assert(syntax.location.domain == "expression")
local reader_error = fails(function()
    local execution = tlv.query_program("//5A", tlv.formats.fixed(1, 1, "big")):execution()
    execution:set_input(b(0x5a, 3, 0))
    execution:finish()
end, tlv.errors.TRUNCATED)
assert(reader_error.query.kind_name == "reader")
assert(reader_error.operation == "value")
assert(reader_error.declared_length == 3 and reader_error.available == 1)
assert(reader_error.raw_length == b(3))
local options = {variables = {min = "integer"}, optimize = true}
local fixed = tlv.formats.fixed(2, 2, "little")
local tag_options = {tags={id=301, class_of=function() return 7 end,
    number_of=function(raw) return string.byte(raw) end}, resolve=function(namespace, name)
    if namespace == "demo" and name == "leaf" then return b(0x5a,0) end
end}
local semantic = tlv.query_program("//demo:leaf[class()=7 and number()=90]", fixed, tag_options)
local semantic_loaded = tlv.query_program_load(semantic:image(), fixed, tag_options)
fails(function() tlv.query_program_load(semantic:image(), fixed,
    {resolve=tag_options.resolve, tags={id=302, class_of=tag_options.tags.class_of,
                                       number_of=tag_options.tags.number_of}}) end, tlv.errors.INVALID_VALUE)
fails(function() tlv.query_program("number(//5A)", f,
    {tags={id=303, class_of=function() return 0 end}}) end, tlv.errors.UNSUPPORTED)
if tlv.formats.emv then
    assert(tlv.query_emv_resolve("emv", "PAN") == b(0x5a))
    local emv = tlv.query_program("count(//emv:PAN)", tlv.formats.emv,
                                {resolve=tlv.query_emv_resolve}):execution()
    emv:set_input(b(0x5a,0)); emv:finish(); assert(emv:result() == 1); emv:close()
end
if tlv.document then
    local doc = tlv.document(b(0x5a,0,1,0,7), fixed)
    local de = semantic:execution()
    de:evaluate_document(doc)
    local match, ordinal = de:next_ordinal()
    assert(match.value == b(7) and ordinal == 0)
    de:close(); doc:close()
    assert(match.tag == b(0x5a,0))
end
local semantic_execution = semantic_loaded:execution()
semantic, semantic_loaded = nil, nil
collectgarbage("collect")
semantic_execution:feed({kind="element", source=b(0x5a,0,1,0,7), offset=9})
semantic_execution:finish()
local semantic_match, semantic_ordinal = semantic_execution:next_ordinal()
assert(semantic_match.tag == b(0x5a,0) and semantic_match.value == b(7) and semantic_ordinal == 0)
semantic_execution:close()
local changed = 0
fails(function() tlv.query_program("//named", f, {resolve=function()
    changed = changed + 1; return changed == 1 and b(0x5a) or b(0x5b)
end}) end, tlv.errors.INVALID_VALUE)
local registry = {a={{tag=b(0x5a), name="leaf"}}, b={{tag=b(0x5a), name="leaf"}}}
local definition_resolve = tlv.query_definition_resolver(registry)
registry.a[1].name = "changed"
assert(definition_resolve("a", "leaf") == b(0x5a))
fails(function() definition_resolve("", "leaf") end, tlv.errors.INVALID_ARG)
local named = tlv.query_program("count(//a:leaf)", f, {resolve=definition_resolve}):execution()
named:set_input(b(0x5a,0)); named:finish(); assert(named:result() == 1); named:close()
local marker = {}
assert(fails(function() tlv.query_program("//named", f, {resolve=function() error(marker) end}) end) == marker)
local failing_tags = tlv.query_program("class(//5A)", f, {tags={id=302, class_of=function() error(marker) end}}):execution()
failing_tags:set_input(b(0x5a,0))
assert(fails(function() failing_tags:finish() end) == marker)
failing_tags:close()
local source_query = tlv.query_program("//5A[@hlen=2]", f):execution()
source_query:feed({kind="element", source=b(0x5a,0), offset=17})
source_query:finish()
local sourced, ordinal = source_query:next_ordinal()
assert(sourced.offset == 17 and ordinal == 0)
source_query:close()
local calls = {}
local providers = {num = {id = 101, decode = function(value, metadata)
    calls[#calls + 1] = metadata
    return string.byte(value) * 10
end}}
local custom = tlv.query_program("num(//5A)", f, {providers = providers})
providers.num = nil
collectgarbage("collect")
local ce = custom:execution()
ce:set_input(b(0x5a, 1, 3))
ce:visit(function() end)
assert(ce:result() == 30 and calls[#calls].offset == 0)
local loaded_custom = tlv.query_program_load(custom:image(), f, {providers = {
    num = {id = 101, decode = function(value) return string.byte(value) * 10 end}
}})
local le = loaded_custom:execution()
le:set_input(b(0x5a, 1, 4))
le:visit(function() end)
assert(le:result() == 40)
fails(function() tlv.query_program_load(custom:image(), f) end, tlv.errors.INVALID_VALUE)
local tp = tlv.query_program("text(//5A)", f, {providers = {
    text = {id = 102, max_result_bytes = 3, decode = function() return "a\0b" end}
}})
local te = tp:execution()
te:set_input(b(0x5a, 0)); te:visit(function() end)
assert(te:result() == "a\0b")
local failing = tlv.query_program("num(//5A)", f, {providers = {
    num = {id = 103, decode = function() error("provider failed") end}
}}):execution()
failing:set_input(b(0x5a, 0))
assert(tostring(fails(function() failing:visit(function() end) end)):find("provider failed", 1, true))
failing:reset()
local reentry, reentry_program
reentry_program = tlv.query_program("num(//5A)", f, {providers = {
    num = {id = 104, decode = function()
        fails(function() reentry_program:info() end, tlv.errors.INVALID_STATE)
        reentry:reset()
        return 1
    end}
}})
reentry = reentry_program:execution()
reentry:set_input(b(0x5a, 0))
fails(function() reentry:visit(function() end) end, tlv.errors.INVALID_STATE)
reentry:reset()
local feed_program = tlv.query_program("//5A", f)
local protected_feed = feed_program:execution({retained=false})
local metadata_reads = 0
local guarded_event = setmetatable({kind="element", source=b(0x5a,0)}, {__index=function()
    metadata_reads = metadata_reads + 1
    fails(function() protected_feed:close() end, tlv.errors.INVALID_STATE)
end})
assert(protected_feed:feed(guarded_event).tag == b(0x5a) and metadata_reads > 0)
protected_feed:finish()
if debug and debug.sethook then
    collectgarbage("stop")
    local finalized, blocked, entered = false, false, false
    local function finalize()
        finalized = true
        blocked = not pcall(protected_feed.close, protected_feed)
    end
    local pending
    if newproxy then
        pending = newproxy(true); getmetatable(pending).__gc = finalize
    else pending = setmetatable({}, {__gc=finalize}) end
    pending = nil
    local method = protected_feed.reset
    debug.sethook(function()
        if entered then debug.sethook(); collectgarbage("collect")
        else entered = true end
    end, "c")
    method(protected_feed)
    debug.sethook(); collectgarbage("restart")
    assert(finalized and blocked, "reset released execution storage during an allocation")
end
protected_feed:reset(); protected_feed:close()
local feeding = feed_program:execution({retained = false})
local selected = feeding:feed({kind = "element", tag = b(0x5a), value = b(1), offset = 7})
assert(selected.offset == 7)
fails(function() feeding:set_input(b(0x5a, 0)) end, tlv.errors.INVALID_STATE)
feeding:finish()
assert(feeding:info().full_validation == 1)
feeding:reset()
fails(function() feeding:feed({kind = "end"}) end, tlv.errors.INVALID_VALUE)
assert(feeding:info().invalid == 1)
fails(function() feeding:finish() end, tlv.errors.INVALID_STATE)
feeding:reset(); feeding:close(); feeding:close()
local retained_feed = feed_program:execution()
assert(retained_feed:feed({kind = "element", tag = b(0x5a), offset = 7}) == nil)
retained_feed:finish()
assert(retained_feed:next().offset == 7 and retained_feed:next() == nil)
retained_feed:close()
local p = tlv.query_program("count(//5A[@len >= $min])", f, options)
assert(p:info().variable_slots == 1 and p:variables().min == 2)
assert(#p:format() > 0 and #p:explain() > 0)
local loaded = tlv.query_program_load(p:image(), f, options)
fails(function() tlv.query_program_load(p:image():sub(1,-2), f, options) end)
local q = loaded:execution()
q:bind("min", "integer", 2)
q:set_input(b(0x5a,1,9,0x5a,2,8,7))
q:visit(function() error("scalar emitted node") end)
assert(q:result() == 1)
q:close()
fails(function() q:result() end, tlv.errors.INVALID_STATE)

local streaming = tlv.query_program("//5A", f):execution({retained = false})
streaming:set_input(b(0x5a,1,9),0,false)
local first = streaming:next()
assert(first.value == b(9) and first.offset == 0)
fails(function() streaming:next() end, tlv.errors.NEED_MORE_DATA)
collectgarbage("collect")
streaming:set_input(b(0x5a,1,9,0x5a,1,8),0,true)
streaming:visit(function(match)
    assert(match.offset == 3)
    fails(function() streaming:reset() end, tlv.errors.INVALID_STATE)
    fails(function() streaming:close() end, tlv.errors.INVALID_STATE)
    return false
end)
assert(streaming:next() == nil)
streaming:reset()
assert(first.value == b(9))
streaming:set_input(b(0x5a,0))
assert(fails(function() streaming:visit(function() error("callback failure") end) end):match("callback failure"))
fails(function() streaming:next() end, tlv.errors.INVALID_STATE)
streaming:reset()
streaming:set_input(b(0x5a,0,0x5a))
assert(streaming:exists(true))
assert(streaming:info().full_validation == 0)
streaming:reset()
streaming:set_input(b(0x5a,0,0x5a))
fails(function() streaming:exists(false) end, tlv.errors.TRUNCATED)

local names = tlv.query_program("count(//fixture:leaf)",f,{names={["fixture:leaf"]=b(0x5a)}}):execution()
names:set_input(b(0x5a,0));names:visit(function() end);assert(names:result()==1)
local diagnostic = fails(function() tlv.query_program("//5A[", f) end)
assert(type(diagnostic.query) == "table" and diagnostic.location.domain == "expression")
-- The primary expression span is not repeated as related evidence.
assert(diagnostic.query.expression.kind == "unknown" and diagnostic.query.begin == nil)
-- A rejected path is a Query syntax error without a Reader operation.
local path = fails(function() tlv.query("01/") end, tlv.errors.SYNTAX)
assert(path.query.kind_name == "syntax" and path.operation == nil)
assert(path.location.domain == "expression" and path.location.begin == 3)
local steps = {}
for i = 1, 66 do steps[i] = "5a" end
local long = fails(function() tlv.query(table.concat(steps, "/")) end, tlv.errors.LIMIT)
assert(long.query.limit == "path-steps" and long.query.configured == 65)
local wide = fails(function() tlv.query(string.rep("5a", 513)) end, tlv.errors.LIMIT)
assert(wide.query.limit == "path-bytes" and wide.query.configured == 512)
if tlv.document and tlv.formats.ber then
    local edit_doc = tlv.document(b(0x70,6,0x5a,1,1,0x5a,1,2,0x5a,1,3),f)
    local old_node = edit_doc:first()
    local editing = tlv.query_program("//5A",f):execution()
    editing:evaluate_document(edit_doc)
    local short = fails(function() editing:edit_document("replace",nil,b(9),2) end,
                        tlv.errors.BUFFER_TOO_SHORT)
    assert(short.applied == 0)
    assert(short.query and short.query.kind_name == "storage")
    assert(editing:edit_document("replace",nil,b(9),3) == 3)
    fails(function() old_node:tag() end)
    fails(function() editing:next() end,tlv.errors.INVALID_STATE)
    editing:reset(); editing:evaluate_document(edit_doc)
    assert(editing:edit_document("insert_after",b(0x5b),b(4),3) == 3)
    local ancestors = tlv.query_program("//70 | //5A",f):execution()
    ancestors:evaluate_document(edit_doc)
    assert(ancestors:edit_document("remove") == 2)
    assert(edit_doc:serialize() == b(0x5b,1,4))
    -- A rejected constructed replacement keeps the Reader cause of its children.
    local reader_doc = tlv.document(b(0x70,3,0x5a,1,1),f)
    local constructed = tlv.query_program("//70",f):execution()
    constructed:evaluate_document(reader_doc)
    local truncated = fails(function() constructed:edit_document("replace",nil,b(0x5a,3),1) end,
                            tlv.errors.TRUNCATED)
    assert(truncated.applied == 0 and truncated.query.kind_name == "reader")
    local located = tlv.document(b(0x50,0,0x57,1,0xaa),f,{retain_source_locations=true})
    local origins = tlv.query_program("//50/following::57[@offset=2 and @hlen=2]",f):execution()
    origins:evaluate_document(located)
    assert(origins:next():value()==b(0xaa))
    assert(origins:next()==nil)
    origins:close()
    local doc = tlv.document(b(0x70,6,0x5a,1,1,0x5a,1,2),f)
    local selected = tlv.query_program("//5A/preceding-sibling::*",f):execution()
    selected:evaluate_document(doc)
    local node = selected:next()
    assert(node:value()==b(1) and node:identity()>0)
    node:set(b(7))
    fails(function() selected:next() end,tlv.errors.INVALID_STATE)
    selected:reset();selected:evaluate_document(doc)
    assert(selected:next():value()==b(7))
    selected:close()
    local scalar = tlv.query_program("value((//5A)[1])",f):execution()
    scalar:evaluate_document(doc)
    assert(scalar:result()==b(7))
end
if tlv.formats.ber then
    local good, bad = b(0x70,3,0x5a,1,1), b(0x70,3,0x5a,1,2)
    local rules = {{context = tlv.query_program("//5A", f),
                    assertion = tlv.query_program("num(.) = 1", f), name = "one"}}
    tlv.query_schema_validate(rules, good)
    tlv.query_schema_validate({{context = tlv.query_program("//5B", f),
        assertion = tlv.query_program("1 = 0", f)}}, bad)
    local failed = fails(function() tlv.query_schema_validate(rules, bad) end, tlv.errors.SCHEMA)
    assert(failed.rule == 0 and failed.schema.kind == "assertion")
    -- The Schema part adds only Schema detail; common evidence is the error's own.
    assert(failed.schema.tag == b(0x5a) and failed.path[1] == b(0x70))
    assert(failed.path_omitted == 0 and failed.schema.path == nil)
    local deep = b(0x5a, 1, 2)
    for level = 1, 35 do deep = b(level == 35 and 0x70 or 0x30, #deep) .. deep end
    local truncated = fails(function() tlv.query_schema_validate(rules, deep) end, tlv.errors.SCHEMA)
    assert(#truncated.path == 32 and truncated.path[1] == b(0x70))
    assert(truncated.path_omitted == 3)
    assert(failed.location.domain == "input" and failed.location.begin == 2)
    assert(failed.schema.field == "one")
    assert(failed.expected == "contextual Query assertion true")
    local bounded = fails(function() tlv.query_schema_validate(rules, good, {max_contexts=0}) end, tlv.errors.LIMIT)
    assert(bounded.query.limit == "schema-contexts")
    fails(function() tlv.query_schema_validate(rules, good, {max_work=1}) end, tlv.errors.LIMIT)
    fails(function() tlv.query_schema_validate({{context=rules[1].context,
        assertion=tlv.query_program("count(.)", f)}}, good) end, tlv.errors.INVALID_VALUE)
    if tlv.document then
        local empty_root = tlv.query_program("value(//70)", f):execution({max_depth=0})
        empty_root:evaluate_document(tlv.document(b(0x70,0), f))
        assert(empty_root:result() == "")
        empty_root:close()
        local reverse = {{context=tlv.query_program("//5A[2]", f),
                          assertion=tlv.query_program("exists(preceding::5A)", f)}}
        local siblings = b(0x70,6,0x5a,1,1,0x5a,1,2)
        fails(function() tlv.query_schema_validate(reverse, siblings) end, tlv.errors.UNSUPPORTED)
        tlv.query_schema_validate(reverse, tlv.document(siblings, f))
        local doc = tlv.document(good, f)
        tlv.query_schema_validate(rules, doc)
        local broken = tlv.document(bad, f)
        local from_document = fails(function() tlv.query_schema_validate(rules, broken) end, tlv.errors.SCHEMA)
        broken:close()
        assert(from_document.location.kind == "unknown" and from_document.path[1] == b(0x70))
        local root = doc:first()
        local provider_rules
        local calls = 0
        local predicate = tlv.query_program("num(.) = 1", f, {providers={num={id=201,
            decode=function(value, metadata)
                calls = calls + 1
                assert(metadata.tag == b(0x5a))
                fails(function() doc:close() end)
                fails(function() root:erase() end)
                fails(function() tlv.query_schema_validate(provider_rules, doc) end, tlv.errors.INVALID_STATE)
                provider_rules[1] = nil
                collectgarbage("collect")
                return string.byte(value)
            end}}})
        provider_rules = {{context=tlv.query_program("//5A", f), assertion=predicate}}
        predicate = nil
        tlv.query_schema_validate(provider_rules, doc)
        assert(calls == 1 and doc:serialize() == good)
        local marker = {}
        local throwing = {{context=rules[1].context, assertion=tlv.query_program("num(.) = 1", f,
            {providers={num={id=202, decode=function() error(marker) end}}})}}
        assert(fails(function() tlv.query_schema_validate(throwing, doc) end) == marker)
        doc:close()
        if debug and debug.sethook then
            local guarded = tlv.document(good, f)
            local finalized, blocked, returned = false, false, false
            local gc_predicate = tlv.query_program("num(.) = 1", f, {providers={num={id=203,
                decode=function()
                    collectgarbage("stop")
                    local function finalize()
                        finalized = true
                        local ok, err = pcall(guarded.close, guarded)
                        blocked = not ok and type(err) == "table" and err.code == tlv.errors.INVALID_STATE
                    end
                    local pending
                    if newproxy then
                        pending = newproxy(true)
                        getmetatable(pending).__gc = finalize
                    else
                        pending = setmetatable({}, {__gc=finalize})
                    end
                    pending = nil
                    -- The next C call is protected diagnostic projection. Collect
                    -- before its first allocation to exercise a closing finalizer.
                    debug.sethook(function()
                        if returned then
                            debug.sethook()
                            collectgarbage("collect")
                        end
                    end, "c")
                    returned = true
                    return 0
                end}}})
            local ok, diagnostic = pcall(tlv.query_schema_validate,
                {{context=rules[1].context, assertion=gc_predicate}}, guarded)
            debug.sethook()
            collectgarbage("restart")
            assert(not ok and diagnostic.code == tlv.errors.SCHEMA)
            assert(finalized and blocked, "Document guard ended before diagnostic projection")
            assert(diagnostic.path[1] == b(0x70))
            guarded:close()
        end
    end
    rules, bad = nil, nil
    collectgarbage("collect")
    assert(failed.schema.tag == b(0x5a) and failed.schema.field == "one")
end
local allocator_available, inject = pcall(require, "opentlv_test_allocator")
if allocator_available then
    for budget = 0, 32 do
        local owned = feed_program:execution()
        inject(function() owned:reset() end, budget)
        owned:reset()
        inject(function() owned:feed({kind="element", source=b(0x5a,0)}) end, budget)
        owned:reset(); owned:set_input(b(0x5a,0)); owned:finish()
        inject(function() owned:next_ordinal() end, budget)
        owned:reset(); owned:close()
        local matcher = tlv.query("5A"):matcher(f)
        matcher:set_input(b(0x5a,0),0,true)
        inject(function() matcher:next() end, budget)
        matcher:reset()
    end
end
print("Lua compiled Query lifetime, continuation, callbacks, Document and Schema tests passed")
