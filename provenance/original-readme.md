# opentlv (Lua, experimental)

For public API setup and a first task, start with [the Lua user guide](../../docs/guides/lua.md).
The component details below cover packaging, implementation or development.

Lua bindings to the [OpenTLV](https://github.com/marekcingel/OpenTLV) C API:
two pieces, `opentlv._core` (`src/`, built directly against the Lua C API,
binding `Reader`, `Writer`, `TreeWriter`, `Schema`, `Codec`, `Element`, `Tag` and preorder tree traversal) and `opentlv`
(`lua/opentlv/init.lua`, a one-line pure-Lua entry point on top of it) — the
same native/pure split as the Python `opentlv-core`/`opentlv` and Rust
`opentlv-sys`/`opentlv` packages. Targets Lua 5.1 through 5.4 and LuaJIT.
See [Lua bindings](https://marekcingel.github.io/OpenTLV/development/lua/)
and [using OpenTLV from Lua](https://marekcingel.github.io/OpenTLV/guides/lua/).

`tlv.query(path)` compiles native tag paths and evaluates them over buffers.
With `OPENTLV_DOCUMENT=ON`, `tlv.document(data, format)` owns a native mutable
Document with `find`, `query`, `set`, `insert`, `erase` and `serialize` methods.
Node userdata retain their Document and reject access after removal.

```lua
local opentlv = require("opentlv")

local data = string.char(0x01, 0x02, 0xAA, 0xBB)
for element in opentlv.reader(data) do
    print(element.tag, element.length, element.value)
end
```

## Build

Requirements: CMake 3.16 or newer, a C99 compiler, and Lua 5.1, 5.2, 5.3, 5.4
or LuaJIT headers and library (`liblua`, `liblua5.x` or `libluajit`,
whichever CMake's `FindLua` module locates; set `LUA_INCLUDE_DIR` and
`LUA_LIBRARIES` directly if it does not find yours).

```sh
cmake -S . -B build-lua -DOPENTLV_BUILD_LUA=ON -DCMAKE_BUILD_TYPE=Release \
    -DOPENTLV_BUILD_CXX=OFF -DOPENTLV_BUILD_TESTS=OFF \
    -DOPENTLV_BUILD_EXAMPLES=OFF
cmake --build build-lua --target opentlv_lua
```

This produces `opentlv/_core.so` (`opentlv/_core.dll` on Windows) under
`build-lua/bindings/lua/`; put it on `LUA_CPATH`, and put this directory's
`lua/` on `LUA_PATH` (or copy both next to your script), so
`require("opentlv")` finds `lua/opentlv/init.lua`, which in turn finds the
compiled module.

Each built-in format registration is compiled only when its CMake component
is enabled: `OPENTLV_FORMAT_BER`, `OPENTLV_FORMAT_CER`, `OPENTLV_FORMAT_DER`,
`OPENTLV_BLUETOOTH` and `OPENTLV_LLDP`. Disabled presets are absent from
`opentlv.formats`; Fixed is always available. When BER is disabled, pass a
format explicitly to `opentlv.reader(data, format)`.

CTest registers `Integration_lua_components` when a Lua interpreter is found.
It checks preset availability and parsing against the selected build options,
including builds with every optional format disabled. The full busted suite
requires BER, CER, DER and Bluetooth; the examples require BER.

**On Windows**, link against the same Lua you run this module with, and make
sure it is a *shared* `lua5x.dll`, not a static `liblua*.a`/`.lib`: a Lua
interpreter statically linked against Lua and a module (this one, or any
other) separately statically linked against Lua end up with two independent
copies of the Lua runtime sharing one `lua_State`, which corrupts memory
under the module's calls back into Lua (the ones in `visitor.c`) and crashes.
Point `LUA_INCLUDE_DIR`/`LUA_LIBRARY` at your interpreter's own shared
`lua5x.dll`'s headers and import library to avoid this; it is generally not
an issue on Linux and macOS, where system Lua packages already ship a shared
library for this reason.

Alternatively, from this directory:

```sh
luarocks make
```

which installs both `opentlv._core` and `lua/opentlv/init.lua` in one step.

## Test

The test suite uses [busted](https://lunarmodules.github.io/busted/):

```sh
luarocks install busted
LUA_PATH="lua/?.lua;lua/?/init.lua;;" LUA_CPATH="build-lua/bindings/lua/?.so;;" busted tests
```

## Writing

```lua
local tlv = require("opentlv")
local writer = tlv.writer(tlv.formats.ber, { capacity = 1024 })
writer:write(string.char(0x5F, 0x2A), string.char(0x09, 0x78))
local encoded = writer:bytes()
```

Tags and values are binary strings, not hexadecimal text. Writer owns its
bounded buffer and delegates encoding to the C core. See the
[writing guide](../../docs/guides/lua.md#writing) for nesting, capacity options
and diagnostics. The standalone `tests/writer_spec.lua` runs without busted
and is registered as `Integration_lua_writer`, including reduced builds.

## Schema validation

`opentlv.schema { rules = {...} }` creates an immutable structural schema.
`schema:validate(data, format, options)` returns `ok`, `code`, a bounded
`diagnostics` array, `total_count` and `truncated`. All rules and diagnostic
details originate from the C schema validator. Diagnostic `path` retains the
outermost scopes; `path_omitted` counts innermost scopes beyond its capacity. See the
[Schema guide](../../docs/guides/lua.md#structural-schemas) and runnable
[`examples/schema.lua`](examples/schema.lua).
The standalone `tests/schema_spec.lua` is registered as `Integration_lua_schema`
and runs with Fixed Format even when optional formats are disabled.

## Value codecs

`opentlv.codecs.uint16_be:decode(bytes)` converts Value bytes through the C
codec; `codec:encode(value)` returns a binary string. `opentlv.codecs` exposes
generic codecs, configured number/text/digits constructors and the enabled
ASN.1, Bluetooth, LLDP and EMV codecs. `opentlv.codec {decode = ..., encode = ...}`
bridges custom Lua functions through `tlv_codec_t`. Codec errors have
shared codes in `opentlv.errors` and optional `codec_detail`.

See the [Codec guide](../../docs/guides/lua.md#value-codecs) for exact integer
handling, builtin representations and EMV selection, and
[`examples/codec.lua`](examples/codec.lua) for a runnable example.
The standalone `tests/codec_spec.lua` runs with optional components disabled;
CTest also runs allocation-failure checks through `Integration_lua_codec_allocator`.
