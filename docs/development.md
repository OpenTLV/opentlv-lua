# Development

## Requirements

Use CMake 3.24 or newer, a C99 compiler, and the development headers for the
Lua interpreter that will load the module. Supported runtimes remain Lua
5.1–5.4 and LuaJIT. Clang must be version 18 or newer, as in Core. No C++
compiler or `tlv++` target is required.

The CMake minimum supports the
[FetchContent package discovery integration](https://cmake.org/cmake/help/v3.24/module/FetchContent.html#integrating-with-find-package).
Core is resolved in this order:

1. Explicit `FETCHCONTENT_SOURCE_DIR_OPENTLV_CORE` checkout.
2. Installed `OpenTLVConfig.cmake` package.
3. Git fetch of `d87dcf96b2707e817dd51163a9f9efa36651e929` from
   `https://github.com/OpenTLV/opentlv.git`.

Choose a separate build directory for each Core installation, Lua ABI, and
toolchain. An existing sibling checkout is used only when explicitly selected.

## Local workspace Core

From this repository, supply an absolute path to the sibling Core checkout:

```sh
cmake -S . -B build/local -DCMAKE_BUILD_TYPE=Release \
    -DFETCHCONTENT_SOURCE_DIR_OPENTLV_CORE=/absolute/path/to/opentlv
cmake --build build/local --target opentlv_lua --config Release
```

The binding builds Core as a static library with position-independent code by
default. The source dependency's C++, embedded Lua and other bindings, tests,
examples, documentation, benchmarks, and fuzzers are disabled. Core itself is
not modified. Its public generated headers and include directories propagate
through `OpenTLV::tlv`.

## Installed Core

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
builds copy the Core DLL beside `_core.dll`; on Linux/macOS, keep the selected
Core shared library available to the system loader when deploying the module.

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

The existing Lua API requires Reader, Writer, Query, Schema, and Codec. The
configure step rejects a Core build missing any of those capabilities.
Document and built-in format families remain optional. For a source dependency,
set Core options such as `-DOPENTLV_DOCUMENT=OFF` or `-DOPENTLV_EMV=OFF` when
configuring Lua. For an installed Core, configure these when building Core.

Lua source selection and test expectations read the selected Core's generated
public `tlv/config.h`, including normalized format options. Consumer cache
variables cannot change the capabilities of an installed Core package.

Existing tests are registered by default (`OPENTLV_BUILD_LUA_TESTS=ON`). Supply
`OPENTLV_LUA_EXECUTABLE` and `OPENTLV_BUSTED_EXECUTABLE` if discovery does not
find the desired interpreter and Busted launcher. Both must use the same Lua
ABI as the development headers. Busted and its dependencies must already be
installed for that runtime.

```sh
ctest --test-dir build/local -C Release --output-on-failure
```

CTest sets module search paths for the existing Lua tests, examples, and native
allocator helper. The Busted suite requires BER, CER, DER, and Bluetooth;
without them it is skipped and the existing component-aware tests remain.
Without an interpreter, runtime tests are skipped. A successful configure or
build alone therefore does not demonstrate a passing runtime suite. Set
`OPENTLV_BUILD_LUA_TESTS=OFF` for a module-only build.

Standalone CI and the shared Core Query corpus migration belong to #3.
LuaRocks adaptation belongs to #4; the original rockspec is not yet a
standalone installation command.
