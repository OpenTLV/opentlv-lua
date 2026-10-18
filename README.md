# opentlv-lua

Lua bindings for OpenTLV, providing idiomatic access to the OpenTLV C API for
parsing, writing, querying, and processing TLV and TLV-like data.

The binding targets Lua 5.1–5.4 and LuaJIT. The public `opentlv` module loads
the native `opentlv._core` module through `lua/opentlv/init.lua`. The native
entry point remains `luaopen_opentlv__core`.

```lua
local opentlv = require("opentlv")

local data = string.char(0x01, 0x02, 0xAA, 0xBB)
for element in opentlv.reader(data) do
    print(element.tag, element.length, element.value)
end
```

## Migration status

This repository contains the unchanged Lua binding sources, tests, examples,
and build metadata imported from OpenTLV Core commit
[`d87dcf96b2707e817dd51163a9f9efa36651e929`](https://github.com/OpenTLV/opentlv/tree/d87dcf96b2707e817dd51163a9f9efa36651e929/bindings/lua).
See [migration provenance and verification](docs/migration.md) for the complete
source inventory and scope of this import.

The imported `CMakeLists.txt` still expects the Core parent build, and the
rockspec still contains the original monorepo paths. **Standalone builds and
`luarocks make` from this repository are not ready yet.** They are tracked in
[standalone CMake #2](https://github.com/OpenTLV/opentlv-lua/issues/2) and
[LuaRocks and versioning #4](https://github.com/OpenTLV/opentlv-lua/issues/4).
[Test and CI migration #3](https://github.com/OpenTLV/opentlv-lua/issues/3)
will establish standalone test execution and CI.

Until those steps are complete, use the
[original Core checkout and instructions](https://github.com/OpenTLV/opentlv/tree/d87dcf96b2707e817dd51163a9f9efa36651e929/bindings/lua)
to build and run the binding. The preserved
[original README](docs/migration/original-readme.md) describes that Core layout;
its relative links and commands are historical, not standalone instructions.

## Repository layout

| Path | Contents |
| --- | --- |
| `src/` | Native C binding, module registration, and Lua compatibility helpers |
| `lua/opentlv/` | Pure-Lua public entry point |
| `examples/` | Existing Lua examples |
| `tests/` | Existing Lua tests and native failing allocator helper |
| `CMakeLists.txt` | Original Core-dependent binding build and test registration |
| `opentlv-scm-1.rockspec` | Original LuaRocks build metadata awaiting adaptation |
| `docs/migration/` | Original README and source inventory |

The binding consumes the OpenTLV C API. Core remains responsible for shared
architecture and native functionality. The workspace is optional; the planned
standalone build will support both an external Core dependency and a local
sibling Core checkout.

## License

MIT; see [LICENSE](LICENSE). Imported sources retain their original copyright
and SPDX notices.
