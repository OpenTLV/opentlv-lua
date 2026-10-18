# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Generate the common corpus runner using only the public Lua facade."""
import argparse
import json
import sys
from pathlib import Path


def literal(value):
    if isinstance(value, dict):
        return "{" + ",".join(f"[{literal(k)}]={literal(v)}" for k, v in value.items()) + "}"
    if isinstance(value, list):
        return "{" + ",".join(map(literal, value)) + "}"
    if isinstance(value, str):
        return json.dumps(value, ensure_ascii=False)
    if value is None:
        return "nil"
    return str(value).lower()


RUNNER = r'''
local tlv = require("opentlv")
local function unhex(s)
    return (s:gsub("..", function(pair) return string.char(tonumber(pair,16)) end))
end
local function hex(s)
    return (s:gsub(".", function(c) return string.format("%02x",string.byte(c)) end))
end
local checks = 0
for _, case in ipairs(cases) do
    local wire = unhex(case.wire)
    for _, optimize in ipairs({true,false}) do
        local declarations = {}
        for name,v in pairs(case.variables or {}) do
            declarations[name] = v.type == "int" and "integer" or v.type
        end
        local ok,p = pcall(tlv.query_program,case.query,tlv.formats.ber,
            {optimize=optimize,variables=declarations,
             names={["fixture:leaf"]=unhex("5a"),["fixture:container"]=unhex("70")}})
        if case.diagnostic then
            assert(not ok and type(p)=="table",case.id)
            assert(p.code==case.diagnostic.code and p.query.kind==case.diagnostic.kind,case.id)
            if case.diagnostic.begin then
                -- The expression span is primary, or related evidence beside another location.
                local span = p.location.domain == "expression" and p.location or p.query.expression
                assert(span.begin==case.diagnostic.begin and span["end"]==case.diagnostic["end"],case.id)
            end
            checks=checks+1
        else
            assert(ok,tostring(p).." "..case.id)
            local function execution(retained)
                local q=p:execution({retained=retained,max_work=100000000})
                for name,v in pairs(case.variables or {}) do
                    q:bind(name,v.type=="int" and "integer" or v.type,
                        v.type=="bytes" and unhex(v.value) or v.value)
                end
                return q
            end
            local function check(q,actual)
                local kind=p:info().result_kind
                if kind~=0 then
                    local v=q:result()
                    actual=kind==1 and (v and "bool:1" or "bool:0")
                        or kind==2 and "int:"..tostring(v)
                        or (kind==4 and "string:" or "bytes:")..hex(v)
                end
                if type(actual)=="table" then
                    assert(#actual==#case.matches,case.id.." count")
                    for i,v in ipairs(actual) do assert(v==case.matches[i],case.id.." identity") end
                else assert(actual==case.matches,case.id.." scalar "..tostring(actual)) end
                checks=checks+1
                q:close()
            end
            if p:info().level~=3 then
                for _,retained in ipairs({true,false}) do
                    if retained or p:info().level<=1 then
                        for split=0,#wire do
                            local q=execution(retained)
                            local actual={}
                            local function collect(match) actual[#actual+1]=match.offset end
                            q:set_input(wire:sub(1,split),0,false)
                            local complete,err=pcall(function() q:visit(collect) end)
                            assert(not complete and err.code==tlv.errors.NEED_MORE_DATA,case.id)
                            q:set_input(wire,0,true)
                            q:visit(collect)
                            check(q,actual)
                        end
                    end
                end
            end
            if tlv.document then
                local doc=tlv.document(wire,tlv.formats.ber,{retain_source_locations=true})
                local offsets,stack,index={}, {},1
                local node=doc:first()
                while node do
                    offsets[node:identity()]=case.offsets[index];index=index+1
                    if node:next() then stack[#stack+1]=node:next() end
                    node=node:first_child()
                    if not node and #stack>0 then node=table.remove(stack) end
                end
                local q=execution(true)
                q:evaluate_document(doc)
                local actual={}
                if p:info().result_kind==0 then
                    local selected=q:next()
                    while selected do
                        actual[#actual+1]=offsets[selected:identity()];selected=q:next()
                    end
                end
                check(q,actual)
            end
        end
    end
end
print("Lua public Query facade: "..#cases.." fixtures, "..checks.." backend/window checks passed")
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--core-source", type=Path, required=True,
                        help="Core checkout supplying tests/query/corpus.json and reference.py")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    shared = args.core_source.resolve() / "tests" / "query"
    if not (shared / "reference.py").is_file():
        parser.error("The selected Core checkout must contain tests/query/reference.py")
    sys.path.insert(0, str(shared))
    from reference import tree

    cases = json.loads((shared / "corpus.json").read_text())["cases"]
    for case in cases:
        if "diagnostic" not in case:
            _, nodes = tree(bytes.fromhex(case["wire"]))
            case["offsets"] = [node["offset"] for node in nodes]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("-- SPDX-License-Identifier: MIT\n-- Copyright (c) 2026 Marek Cingel\n"
                           + "local cases=" + literal(cases) + "\n" + RUNNER, encoding="utf-8")


if __name__ == "__main__":
    main()
