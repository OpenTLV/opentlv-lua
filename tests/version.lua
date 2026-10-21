-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

-- Exercise the compiled API against the metadata shipped with this build.
local metadata = assert(io.open(assert(arg[1], "version metadata path required"), "r"))
local contents = metadata:read("*a")
metadata:close()
-- The generated version alphabet contains no JSON escapes.
local expected = assert(contents:match('"binding_version"%s*:%s*"([^"]+)"'))
local opentlv = require("opentlv")
assert(opentlv == require("opentlv._core"))
assert(opentlv.version() == expected, "version() differs from binding package metadata")
assert(opentlv._VERSION == expected, "_VERSION differs from binding package metadata")
for _, name in ipairs({ "core_version", "_CORE_VERSION", "core_revision", "core_sha" }) do
    assert(opentlv[name] == nil, "Core identity must not be exposed through the Lua API")
end
print("Lua binding version matches package metadata: " .. expected)
