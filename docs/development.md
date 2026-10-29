# Development

## Requirements

Use CMake 3.24 or newer, a C99 compiler, and the development headers for the
Lua interpreter that will load the module. Supported runtimes remain Lua
5.1–5.4 and LuaJIT. Clang must be version 18 or newer, as in Core. No C++
compiler or `tlv++` target is required.

The CMake minimum supports the
[FetchContent package discovery integration](https://cmake.org/cmake/help/v3.24/module/FetchContent.html#integrating-with-find-package).
Without an explicit `OPENTLV_LUA_CORE_PACKAGE`, Core is resolved in this order:

1. Explicit `FETCHCONTENT_SOURCE_DIR_OPENTLV_CORE` checkout.
2. Installed `OpenTLVConfig.cmake` package.
3. Git fetch of `345c77acb29266036df5f27d04ce315bb61853cc` from
   `https://github.com/OpenTLV/opentlv.git`.

Choose a separate build directory for each Core installation, Lua ABI, and
toolchain. An existing sibling checkout is used only when explicitly selected.

## Local workspace Core

The [optional workspace](https://github.com/OpenTLV/opentlv-workspace) opens
`opentlv-lua` and `opentlv` as sibling repositories. Opening it does not configure
a dependency; the explicit source override below selects the Core checkout.
The same commands work without VS Code or the workspace repository.

From this repository, supply an absolute path to the sibling Core checkout:

```sh
cmake -S . -B build/local -DCMAKE_BUILD_TYPE=Release \
    -DFETCHCONTENT_SOURCE_DIR_OPENTLV_CORE=/absolute/path/to/opentlv
cmake --build build/local --target opentlv_lua --config Release
```

The binding builds Core as a static library with position-independent code by
default. The source dependency's C++ and in-tree bindings, tests,
examples, documentation, benchmarks, and fuzzers are disabled. Core itself is
not modified. Its public generated headers and include directories propagate
through `OpenTLV::tlv`.

## Installed Core

For an exact package selection, set
`-DOPENTLV_LUA_CORE_PACKAGE=/absolute/path/to/prefix/lib/cmake/OpenTLV` to the
directory containing `OpenTLVConfig.cmake`. A missing package or simultaneous
source override is an error; this mode never falls back to Git or another
discoverable package. CMake selection alone does not authenticate the producer.
The [CI resolver](quality-gates.md#core-verification) additionally verifies
release checksums, attestations and compatibility before selecting this mode.

Build and install Core with its public CMake target support (Core #620 or newer).
For example, from this repository:

```sh
cmake -S ../opentlv -B build/core-install -DCMAKE_BUILD_TYPE=Release \
    -DOPENTLV_BUILD_CXX=OFF -DOPENTLV_BUILD_TESTS=OFF \
    -DOPENTLV_BUILD_EXAMPLES=OFF -DOPENTLV_BUILD_SHARED_LIBS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build build/core-install --target tlv --config Release
cmake --install build/core-install --config Release --prefix /absolute/path/to/core-prefix
cmake -S . -B build/installed -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/absolute/path/to/core-prefix
cmake --build build/installed --target opentlv_lua --config Release
```

Alternatively, set `opentlv_core_DIR` directly to the directory containing
`OpenTLVConfig.cmake`, normally `<prefix>/lib/cmake/OpenTLV`. This variable uses
the FetchContent dependency name. A selected package without `OpenTLV::tlv` or
its public `tlv/config.h` is rejected.

An installed static Core must already have been compiled with PIC on platforms
that require it for shared modules. Setting PIC on the Lua build cannot repair
an existing static archive. Shared Core packages are also supported: Windows
builds copy the Core DLL beside `_core.dll`; add that directory to `PATH` when
loading the module (CTest and the CI runner do this automatically). On Linux/macOS,
keep the selected Core shared library available to the system loader when deploying
the module.

## Pinned remote Core

The default build tries package discovery before fetching Core. To explicitly
exercise the remote fallback, use a fresh build directory:

```sh
cmake -S . -B build/remote -DCMAKE_BUILD_TYPE=Release \
    -DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER
cmake --build build/remote --target opentlv_lua --config Release
```

Git and network access are needed for the first fetch. Do not set a source
override for this check. The immutable pin is recorded in `cmake/Core.cmake`;
update it deliberately with dependency validation.

## Lua discovery and loading

`find_package(Lua)` discovers development headers and libraries. If needed,
pass `-DLUA_INCLUDE_DIR=/path/to/lua/include`. On Windows, pass
`-DLUA_LIBRARY=/path/to/lua/lib/lua54.lib` as well, or use the existing automatic
search in the header prefix's sibling `lib` and `bin` directories. Use the
import library for the same shared Lua runtime as the interpreter; do not link
a second static Lua runtime into the module.

Linux resolves Lua symbols from the interpreter at module load time; macOS
retains `-undefined dynamic_lookup` and the `.so` module suffix. Windows retains
the exported `luaopen_opentlv__core` entry point. All configurations emit the
module under `<build>/opentlv/_core.so` or `_core.dll`.

From the repository root on Linux/macOS:

```sh
LUA_PATH='lua/?.lua;lua/?/init.lua;;' LUA_CPATH='build/local/?.so;;' \
    lua -e 'assert(require("opentlv") == require("opentlv._core"))'
```

For PowerShell, select the matching Lua executable and use:

```powershell
$env:LUA_PATH = 'lua/?.lua;lua/?/init.lua;;'
$env:LUA_CPATH = 'build/local/?.dll;;'
lua examples/quick_start.lua
```

Ensure the interpreter's Lua DLL is available on Windows. These environment
variables apply only to the current shell and its child processes.

## Components and tests

Official Lua builds and packages include all stable Core functionality supported
by the binding: Reader, Writer, Document, Query (including its text frontend and
set operations), Schema, Codec, ASN.1/BER/CER/DER, Bluetooth, EMV, NFC, DHCP and
LLDP. This does not add Lua APIs for Core functionality the binding does not expose.

The requirements are recorded in [`cmake/core-features.json`](../cmake/core-features.json).
When building Core from source, Lua explicitly enables every requirement as an
internal CMake setting, replacing old cache or command-line `OFF` values.
Individual Core modules cannot be disabled through Lua binding CMake options.
Newly supported modules must be added to these standard requirements.

The selected Core's public generated `tlv/config.h` must confirm every required
feature. An incomplete installed Core is rejected during configuration; consumer
cache values cannot enable code missing from that library. Build/install a full
Core package or select Core source instead.

Custom source modifications are possible but are not official release
configurations. Core retains its own modular build policy for C/C++ consumers.
Lua build controls such as shared/static Core, warnings, dependency selection
and test registration remain available.

Existing tests are registered by default (`OPENTLV_BUILD_LUA_TESTS=ON`). Supply
`OPENTLV_LUA_EXECUTABLE` and `OPENTLV_BUSTED_EXECUTABLE` if discovery does not
find the desired interpreter and Busted launcher. Both must use the same Lua
ABI as the development headers. Busted and its dependencies must already be
installed for that runtime.

```sh
ctest --test-dir build/local -C Release --output-on-failure
```

CTest sets module search paths for the existing Lua tests, examples, and native
allocator helper. All supported sources and component tests are registered
without feature-selection conditions. Without Busted, its suite is skipped.
Without an interpreter, runtime tests are skipped. A successful configure or
build alone therefore does not demonstrate a passing runtime suite. Set
`OPENTLV_BUILD_LUA_TESTS=OFF` for a module-only build.

See the [API guide](api.md) and [implementation notes](implementation.md) for
usage and ownership. See [tests and CI](testing.md) for the preserved CI matrix, the full test runner,
and shared Core Query corpus execution.

## LuaRocks installation

Use LuaRocks configured for the same interpreter and development headers as the
module. From this repository root:

```sh
luarocks make opentlv-scm-1.rockspec
```

This uses `build/luarocks`, disables the binding test targets, and retains the
normal Core selection order documented above. The default fetched Core is
static, so the installed module needs no separately deployed Core library.
No sibling checkout is assumed. Git/network access is required if Core has not
already been supplied. CMake and a compiler remain build prerequisites; the
rock's runtime dependency remains `lua >= 5.1`.

For a local Core override, preconfigure the same build directory before
`luarocks make` (use `--fresh` when changing Lua ABI, compiler, or Core):

```sh
cmake --fresh -S . -B build/luarocks -DCMAKE_BUILD_TYPE=Release \
    -DOPENTLV_BUILD_LUA_TESTS=OFF \
    -DLUA_INCLUDE_DIR=/absolute/path/to/lua/include \
    -DFETCHCONTENT_SOURCE_DIR_OPENTLV_CORE=/absolute/path/to/opentlv
luarocks make opentlv-scm-1.rockspec
```

An installed Core can instead be selected with `opentlv_core_DIR` or
`CMAKE_PREFIX_PATH` during that preconfiguration. When using a shared Core,
its runtime library must also be available to the system loader, including
on Windows; the rock installs the Lua module, not external shared libraries.

To install into an isolated tree on Linux/macOS:

```sh
luarocks --tree="$PWD/build/rocks" make opentlv-scm-1.rockspec
eval "$(luarocks --tree="$PWD/build/rocks" path)"
lua -e 'assert(require("opentlv") == require("opentlv._core"))'
```

On Windows, run in an MSVC developer PowerShell with LuaRocks configured for
MSVC and the interpreter's shared Lua DLL. The existing library discovery uses
the `lib`/`bin` sibling of `LUA_INCDIR`; nonstandard layouts can preconfigure
`LUA_LIBRARY` explicitly. Avoid Git Bash's Unix `link.exe` shadowing MSVC.

```powershell
$tree = Join-Path $PWD 'build/rocks'
luarocks "--tree=$tree" make opentlv-scm-1.rockspec
$env:LUA_PATH = (luarocks "--tree=$tree" path --lr-path)
$env:LUA_CPATH = (luarocks "--tree=$tree" path --lr-cpath)
lua -e 'assert(require("opentlv") == require("opentlv._core"))'
```

The rock installs `opentlv/_core.so` (or `.dll`) in LuaRocks' native module
directory and `opentlv/init.lua` in its Lua source directory. It also stores
`lua-version.json` in the versioned rock directory. See
[independent versioning](versioning.md) for its meaning and release baseline.

`scripts/rockspec.py vMAJOR.MINOR.PATCH` generates the
[release rockspec](versioning.md#luarocks-release-rockspec) of a release tag
in `build/rockspecs`. Its build passes `OPENTLV_LUA_RELEASE_TAG`, because
LuaRocks builds a release from a tag clone without `.git`; `luarocks make`
with it from a checkout succeeds only at that tag. Use `opentlv-scm-1.rockspec`
for development builds.

## Contributing and licensing

The shared
[OpenTLV contribution guidelines](https://github.com/OpenTLV/.github/blob/main/CONTRIBUTING.md),
[Code of Conduct](https://github.com/OpenTLV/.github/blob/main/CODE_OF_CONDUCT.md)
and [support directory](https://github.com/OpenTLV/.github/blob/main/SUPPORT.md)
apply; this guide adds the Lua build and test rules. Report suspected
vulnerabilities privately through the repository's Security tab as described
in the [security policy](https://github.com/OpenTLV/.github/blob/main/SECURITY.md),
not in public issues. Changes to shared TLV semantics, the C API or Core
components belong to [OpenTLV Core](https://github.com/OpenTLV/opentlv/issues).

The binding is MIT licensed. Marek Cingel holds copyright in his original
contributions; other contributors retain copyright in theirs, since an MIT
contribution does not assign copyright, and OpenTLV is a GitHub organization,
not a copyright holder. Start new Lua sources by Marek Cingel with:

<!-- REUSE-IgnoreStart -->
```lua
-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel
```
<!-- REUSE-IgnoreEnd -->

Use `//` comments in C files, `#` comments in Python, CMake and YAML files,
and your own name and year for your own work; never rewrite an existing or
third-party notice. Documentation and JSON data files that cannot carry a
header are listed by exact path in the repository's `REUSE.toml`; add new ones
there. The initial source inventory in `docs/migration/` and the historical
README in `provenance/` keep their original bytes and never receive headers.
`pre-commit run --all-files`, which the source quality job runs, includes
`reuse lint` over every file Git does not ignore. The hook only lints: it does
not add headers, which would put one author's notice on other contributors'
files.

The module archives carry `LICENSE-Lua` and `LICENSE-Core`, and the rock
installs `LICENSE` into its documentation directory, because LuaRocks copies
the root license and Markdown files when a rockspec names no documentation
directory. The rockspec declares `license = "MIT"`. The native module
statically links Core by default, so a binary distribution needs Core's MIT
notice as well; at the pinned commit, Core's `LICENSE` is identical to this
repository's and names the same holder. The archives bundle no Lua interpreter
or library; the consumer provides it. Recheck Core's notices when changing
the pin, and add the license of every new bundled or linked dependency to the
archives and this section.
