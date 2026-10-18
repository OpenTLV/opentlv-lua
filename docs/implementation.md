# Lua bindings (experimental)

The Lua bindings live in the standalone
[opentlv-lua repository](https://github.com/OpenTLV/opentlv-lua), split the same way the [Python
bindings](https://github.com/OpenTLV/opentlv/blob/main/docs/development/python.md) and [Rust bindings](https://github.com/OpenTLV/opentlv/blob/main/docs/development/rust.md) are: `opentlv._core`
(`src/`), a native extension module written directly against
the Lua C API, and `opentlv` (`lua/opentlv/init.lua`), a
one-line pure-Lua entry point that `require("opentlv")` resolves to and that
returns `opentlv._core` unchanged. Unlike Python's and Rust's pure layers,
`opentlv` adds no ergonomics of its own beyond the name callers request:
Lua's C API is close enough to the concepts this binding exposes (Reader,
Writer, Tree Writer, Element, Tag) that there is nothing a Lua-side wrapper would usefully add
today, so the split exists for naming/packaging consistency across bindings
rather than for a richer pure-Lua layer. `src/common.h`
documents the ownership, registration and error-handling conventions every
native-side component (including Reader, Writer and Schema)
follows, and states the rule for what belongs in this binding versus in
`tlv/`: functionality useful outside Lua belongs in the OpenTLV C API, this
binding only adapts what already exists there to Lua's conventions. For
usage, see [Using OpenTLV from Lua](api.md). For the naming and
shape this binding follows and adapts, see the [language bindings conceptual
model](https://github.com/OpenTLV/opentlv/blob/main/docs/concepts/bindings.md).

It targets Lua 5.1 through 5.4 and LuaJIT (which implements the Lua 5.1 C
API), using only the portable subset of the Lua C API common to all of them;
see `src/compat.h`. It covers Reader, Writer, Tree Writer, Element and Tag, and
preorder tree traversal (`opentlv.visit_tree`, built on `tlv_tree_reader_visit()`/
`tlv_der_visit()`), structural Schema validation with detailed diagnostic reports,
Value codecs with builtin and custom callback support, compiled path Query,
and mutable Document (when `OPENTLV_DOCUMENT=ON`).

All Lua source paths on this page are relative to `opentlv-lua`.

## Build

Core no longer configures or builds the Lua module. Use the standalone
[development guide](development.md)
for CMake, external Core selection, module paths and the shared Lua runtime
requirement on Windows. For installation with LuaRocks, follow the
[package instructions](development.md#luarocks-installation).

## Versioning

Lua binding releases are versioned independently of Core. The existing
`opentlv.version()` and `opentlv._VERSION` report the linked Core version.
See [Lua versioning](versioning.md).

## Continuous integration

Lua builds, tests, examples, allocator checks, Query facade conformance and
LuaRocks installation run in the standalone
[Lua workflow](https://github.com/OpenTLV/opentlv-lua/actions/workflows/lua.yml).
Core retains the shared Query corpus, reference implementation and native tests.
See [Lua test ownership and commands](testing.md).

## Codec implementation

`src/codec.c` owns codec userdata, configured descriptors, exact Lua integer
conversion and protected custom callback trampolines. `src/codec_builtin.c`
adapts documented C representations to Lua values and tables. All wire
conversion and semantic validation uses `tlv_codec_decode()` and
`tlv_codec_encode()`. EMV selection uses its existing dictionary and builtin
presentation adapter. There is no Lua-specific codec registry.

Native temporary objects and output buffers are Lua userdata, so argument
errors and allocation failures cannot leak C heap storage. Borrowed strings
remain rooted until the native operation finishes; decoded views become
owned Lua strings. Custom functions are held in a userdata environment
(Lua 5.1) or uservalue table (5.2+), allowing callback cycles to be collected.
Each invocation prepares its own protected closure before entering C, so
allocation errors cannot unwind across the core and recursive calls do not
overwrite callback state.

`Integration_lua_codec`, `Integration_lua_codec_allocator` and
`Integration_example_lua_codec` run standalone without busted, including
minimal builds. The codec suite also runs under `busted tests` and checks
every exported static descriptor. Structure codecs remain outside this
Value-codec binding.

## Writer implementation

`src/writer.c` owns bounded output and Tree Writer workspace storage, retains
Format userdata, and delegates writes to `tlv_writer_write_element_diag()`
and `tlv_tree_writer_*`. It contains no format-specific encoding logic.
`src/error.c` copies native writer diagnostics into owned Lua error fields.
The C core remains allocation-free; Lua userdata finalizers release storage
allocated by the binding, including after partial constructor failures.

`tests/writer_spec.lua` runs both under busted and directly with Lua. CTest
registers the standalone suite whenever the interpreter is available, even
when optional formats are disabled. It covers binary strings, Reader/Writer
roundtrips, exact preset bytes, ownership, nested output, resource exhaustion
and structured diagnostics.

## Schema implementation

`src/schema.c` snapshots Lua descriptions into `tlv_structure_schema_t`,
`tlv_structure_rule_t`, `tlv_schema_entry_t` and `tlv_structure_group_t` arrays.
A registry reference retains a private table holding native-array userdata,
immutable tag/name strings and child Schema objects. The Schema finalizer is
installed before acquiring references; temporary diagnostic arrays are rooted
on the Lua stack, so constructor or result-conversion allocation failures do
not leak native storage. No Lua callback runs during C validation.

`Schema:validate()` calls `tlv_schema_validate_all_diag()`. It returns bounded
schema reports or a basic native-code/offset diagnostic on fatal failures.
`src/error.c` provides common `tlv_diagnostic_t` conversion shared with Reader
and Writer; Schema adds only the typed detail exposed by the C report.

`tests/schema_spec.lua` runs under busted and directly through CTest, including
builds without BER. It covers rules, groups, ordering, paths, native offsets,
capacity/count-only results, malformed input, numeric checks, immutable
configuration and garbage collection. When the test allocator is available,
it also injects failures during construction and diagnostic conversion and
checks that retained child schemas are released.

## Query and Document ownership tests

`tests/query_document_spec.lua` runs directly through CTest and under busted.
It covers query diagnostics and matching, mutation rollback, foreign and stale
handles, configured Format lifetimes, and optional-component builds. Its
`--allocator` mode injects Lua allocation failures during construction, result
conversion, insertion and serialization. Document userdata acquire their
finalizer before allocating native state; Node userdata root the owning
Document. A non-owning list of live handles tracks native invalidation without
copying the document tree. Query evaluation pulls native Tree Reader items
between Lua allocations, avoiding Lua long jumps through native callbacks.
