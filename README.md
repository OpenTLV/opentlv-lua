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

## Build

Requires CMake 3.24 or newer, a C99 compiler, and Lua headers matching the
interpreter used to load the module. Windows also requires that interpreter's
shared Lua import library. Clang builds retain the Core requirement of Clang 18
or newer.

```sh
cmake -S . -B build/local -DCMAKE_BUILD_TYPE=Release \
    -DFETCHCONTENT_SOURCE_DIR_OPENTLV_CORE=/absolute/path/to/opentlv
cmake --build build/local --target opentlv_lua --config Release
ctest --test-dir build/local -C Release --output-on-failure
```

Omit the source override to use an installed Core package or fetch the pinned
Core revision automatically. The binding links only `OpenTLV::tlv` and requires
no C++ compiler. The workspace is optional.

See [development instructions](docs/development.md) for package selection,
Lua discovery, module loading, component configuration, and test prerequisites.

## Migration status

The initial sources were imported from OpenTLV Core commit
[`d87dcf96b2707e817dd51163a9f9efa36651e929`](https://github.com/OpenTLV/opentlv/tree/d87dcf96b2707e817dd51163a9f9efa36651e929/bindings/lua).
The standalone CMake build uses that same revision as its remote dependency pin.
See [migration provenance](docs/migration.md) for the original inventory and the
additional shared binding helpers required by the standalone build.

The rockspec still contains the original monorepo paths; `luarocks make` from
this repository awaits [LuaRocks and versioning #4](https://github.com/OpenTLV/opentlv-lua/issues/4).
The standalone [Lua test workflow](.github/workflows/lua.yml) runs the existing
test suites and shared Query corpus through the public Lua facade. See
[tests, CI matrix, and baseline comparison](docs/testing.md) for prerequisites,
ownership, and local execution.
The preserved [original README](docs/migration/original-readme.md) describes the
historical Core layout, not the standalone build.

## Repository layout

| Path | Contents |
| --- | --- |
| `src/` | Native C binding, module registration, and Lua compatibility helpers |
| `lua/opentlv/` | Pure-Lua public entry point |
| `examples/` | Existing Lua examples |
| `tests/` | Existing Lua tests and native failing allocator helper |
| `CMakeLists.txt` | Standalone binding build and existing test registration |
| `opentlv-scm-1.rockspec` | Original LuaRocks build metadata awaiting adaptation |
| `docs/migration/` | Original README and source inventory |

The binding consumes the OpenTLV C API. Core remains responsible for shared
architecture and native functionality. The standalone build supports both an
external Core dependency and an explicit local sibling Core checkout.

## License

MIT; see [LICENSE](LICENSE). Imported sources retain their original copyright
and SPDX notices.
