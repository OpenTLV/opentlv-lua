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

See the [Lua API guide](docs/api.md) for usage and
[implementation notes](docs/implementation.md) for ownership and error handling.
The [Core API coverage record](docs/api-coverage.md) maps every generic Core
header to its public Lua API and binding tests, and lists the reviewed
exclusions and limitations for the pinned Core.

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
Lua discovery, module loading, Core requirements, and test prerequisites.

Official Lua packages include all stable Core functionality supported by the
binding. Individual Core modules cannot be disabled through Lua CMake options.
Custom source modifications are possible but are not official release configurations.

## LuaRocks

With LuaRocks configured for the target Lua runtime, build and install from
the repository root:

```sh
luarocks make opentlv-scm-1.rockspec
lua -e 'assert(require("opentlv") == require("opentlv._core"))'
```

The LuaRocks tree must be on Lua's search paths. See the
[installation instructions](docs/development.md#luarocks-installation) for an
isolated tree, Windows requirements, and explicit Core selection.
The rock installs both modules and Lua binding version metadata. Lua release
tags are independent of Core; `opentlv.version()` and `opentlv._VERSION`
report the Lua binding version from that same metadata. See [versioning](docs/versioning.md).

The [quality gates](docs/quality-gates.md) separate PR, Main, Full and Release
verification. Full covers Lua 5.1–5.4 on Linux/Windows/macOS, Linux LuaJIT,
shared Core and sanitizers. The manual [release gate](docs/releases.md) requires
an authenticated Core release and same-attempt Full evidence before publishing
validated Lua 5.4 module archives and a `v`-prefixed tag.

## Migration status

The initial sources were imported from OpenTLV Core commit
[`d87dcf96b2707e817dd51163a9f9efa36651e929`](https://github.com/OpenTLV/opentlv/tree/d87dcf96b2707e817dd51163a9f9efa36651e929/bindings/lua).
The standalone CMake build uses that same revision as its remote dependency pin.
See [migration provenance](docs/migration.md) for the original inventory and the
additional shared binding helpers required by the standalone build.

The rockspec builds directly from this standalone repository and retains the
existing package name `opentlv` and development rock version `scm-1`.
The standalone [Lua test workflow](.github/workflows/lua.yml) runs the existing
test suites and shared Query corpus through the public Lua facade. See
[tests, CI matrix, and baseline comparison](docs/testing.md) for prerequisites,
ownership, and local execution.
The preserved [original README](provenance/original-readme.md) describes the
historical Core layout, not the standalone build.

## Repository layout

Successful Main CI proposes a Docs source-pin update when Lua manuals or examples
change. See [documentation pin notifications](docs/quality-gates.md#documentation-pin-notifications)
for credentials, retries and the separate versioned release publication flow.

| Path | Contents |
| --- | --- |
| `src/` | Native C binding, module registration, and Lua compatibility helpers |
| `lua/opentlv/` | Pure-Lua public entry point |
| `examples/` | Existing Lua examples |
| `tests/` | Existing Lua tests and native failing allocator helper |
| `CMakeLists.txt` | Standalone binding build and existing test registration |
| `opentlv-scm-1.rockspec` | Standalone LuaRocks build and installation metadata |
| `.github/workflows/release.yml` | Manual release verification and artifact publication |
| `docs/migration/` | Initial source inventory |
| `provenance/` | Byte-preserved historical README |
| `LICENSE`, `LICENSES/`, `REUSE.toml` | MIT license, the license text for REUSE and the metadata of files without a header |
| `.pre-commit-config.yaml`, `requirements-dev.txt` | Formatting, whitespace and REUSE hooks and the Python tools of the quality checks |

The binding consumes the OpenTLV C API. Core remains responsible for shared
architecture and native functionality. The standalone build supports both an
external Core dependency and an explicit local sibling Core checkout.

## License

MIT; see [LICENSE](LICENSE). Marek Cingel holds copyright in his original
contributions, including the binding migrated from Core; other contributors
retain copyright in theirs. OpenTLV is a GitHub organization, not a copyright
holder. Imported sources retain their original copyright and SPDX notices.
Sources carry SPDX headers, and [`REUSE.toml`](REUSE.toml) covers the files
that cannot carry one, so every tracked file has REUSE metadata. The module
archives include the Lua and Core license texts, and the rock installs
`LICENSE`. See [licensing](docs/development.md#contributing-and-licensing).

## Community

Contribution, conduct, security and support policies are shared across OpenTLV
in [OpenTLV/.github](https://github.com/OpenTLV/.github); this repository has
no local copies, so GitHub shows those defaults. The
[development guide](docs/development.md#contributing-and-licensing) adds the
Lua build, test and licensing instructions. Report suspected vulnerabilities
privately as described in the
[security policy](https://github.com/OpenTLV/.github/blob/main/SECURITY.md),
not in public issues.
