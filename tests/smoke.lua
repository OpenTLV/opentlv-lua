-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel
local m = require("opentlv")
assert(m == require("opentlv._core"))
assert(type(m.version()) == "string" and m.version() == m._VERSION)
local count = 0
for element in m.reader(string.char(0x01, 0x02, 0xAA, 0xBB)) do
    assert(element.tag == string.char(1) and element.length == 2)
    assert(element.value == string.char(0xAA, 0xBB))
    count = count + 1
end
assert(count == 1)
collectgarbage("collect")
print("Lua module, version and public Reader smoke passed")
