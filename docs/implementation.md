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
see `src/compat.h`. It covers Reader and Tree Reader with incremental input,
Source preservation, Writer, Tree Writer, Element and Tag, and
preorder tree traversal (`opentlv.visit_tree`, built on `tlv_tree_reader_visit()`/
`tlv_der_visit()`), structural Schema validation with detailed diagnostic reports,
Value codecs with builtin and custom callback support, structure codecs,
deterministic wire generation, compiled path Query, mutable Document (when
`OPENTLV_DOCUMENT=ON`) and custom Formats composed from Core's field encodings.

All Lua source paths on this page are relative to `opentlv-lua`.

## Build

Core no longer configures or builds the Lua module. Use the standalone
[development guide](development.md)
for CMake, external Core selection, module paths and the shared Lua runtime
requirement on Windows. For installation with LuaRocks, follow the
[package instructions](development.md#luarocks-installation).

## Versioning

Lua binding releases are versioned independently of Core. The existing
`opentlv.version()` and `opentlv._VERSION` report the Lua binding version from
the same generated value as `lua-version.json`. Core identity is build/release
metadata and is not exposed through the Lua API.
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

`codecs.schema_number{}` embeds its `tlv_schema_entry_t` and
`tlv_schema_number_t` in the codec userdata and keeps the rule's Tag and name
strings in its environment. Structure codecs (`opentlv.StructureCodec`) keep
their Format, Schema or checked Schema and callbacks in the same kind of
environment. Each call copies the `tlv_structure_codec_t` and sets its
context to the call's own protected closure, shared with custom Value codecs.
Core validates the sequence around the callbacks, and the encoder's string is
reused for the size query and the validated write into a Lua userdata buffer.

`src/generator.c` snapshots `tlv_generator_options_t` and its candidate table
in a Generator userdata whose environment roots the Format, the table and the
candidate Tag strings. `generate()` checks the Format's capabilities, sizes the
workspace with `tlv_generator_workspace_size()` and calls `tlv_generate()` with
one Lua userdata block that holds the workspace and the output case. No Lua
code runs while Core generates.

`Integration_lua_codec`, `Integration_lua_codec_allocator` and
`Integration_example_lua_codec` run standalone without busted.
The codec suite also runs under `busted tests` and checks
every exported static descriptor. `tests/codec_generator_spec.lua` runs under
busted and through `Integration_lua_codec_generator` and its allocator variant.
It covers Schema-number widths, length checks, rule snapshots and Query
providers; structure codec validation, limits, checked Schemas, missing
directions, error propagation, recursion and collection; and reproducible
generation, nesting, configuration snapshots and Core configuration results.
Its `--allocator` mode injects Lua allocation failures across these
operations. CTest also runs `examples/generate.lua`.

## Format composition implementation

`src/formats/compose.c` reads `opentlv.formats.compose()` and
`opentlv.formats.packed()` configuration tables into the Core field encoding,
composition and packed layout structures, then initializes the descriptor with
the matching Core composition. A composed Format is one larger userdata block
that begins with the common `tlv_lua_format_t`; the configuration, policies,
tag-only identifiers and the packed identifier table follow it in the same
allocation. Core borrows them for the descriptor's lifetime, and every Format
consumer retains the userdata, so no finalizer or C heap allocation is needed.
Borrowed configuration strings stay rooted on the Lua stack until they are
copied, and argument errors cannot leak storage. Encoding pairs without a
dedicated Core composition use small adapters that only forward each field to
its `tlv/field/` read or write primitive.

`src/format.c` binds the canonical Format operations for every Format, and
`src/error.c` copies `tlv_format_error_t` detail into the raised error table.
`tests/compose_spec.lua` runs under busted and checks compositions, policies,
tag-only framing, configuration rejection and snapshots, every Format entry
point and retained lifetimes. CTest also runs `examples/compose.lua`.

## Reader and Source implementation

`src/reader.c` binds the sequential Reader and the Tree Reader. Each cursor
references its current input string and Format userdata; `set_input()`
references the new window before Core borrows it and releases the old one
only after Core accepts it. The binding checks that the new window starts with
the old undiscarded bytes, a precondition Core leaves to the caller. Tree
Reader frames are allocated after the finalizer is installed. Elements, items
and events are copied into Lua tables, so they never borrow a window.

`src/source.c` defines `opentlv.Source`: a copy of Core's `tlv_source_t`
together with references to the input string and Format it borrows, so
preservation stays valid after the Reader moves on or is collected.

`reader:visit()` and `tree_reader:visit()` share the protected trampoline of
`src/visitor.c` with `opentlv.visit()`/`visit_tree()`. While native traversal
runs, the cursor rejects its own methods, including an explicit finalizer
call, because Core forbids changing the cursor from its callback.

## Writer implementation

`src/writer.c` owns bounded output and Tree Writer workspace storage, retains
Format userdata, and delegates writes to `tlv_writer_write_element_diag()`,
`tlv_writer_copy_encoded_diag()`, `tlv_writer_preserve_diag()` and
`tlv_tree_writer_*`. It contains no format-specific encoding logic.
`src/error.c` copies native writer diagnostics into owned Lua error fields.
The C core remains allocation-free; Lua userdata finalizers release storage
allocated by the binding, including after partial constructor failures.

`measure_tree()` and `measure_events()` allocate their workspace as Lua
userdata, so an error never leaks it. Core pulls items through a provider
callback; the callback runs Lua only inside `lua_pcall()`, stores a Lua error
and returns a failure to Core, and the error is raised again after Core
returns. Tag and Value strings of every item stay referenced until the call
ends, because Core keeps open parent Tags and diagnostics may refer to them.

`tests/writer_spec.lua` runs both under busted and directly with Lua. CTest
registers the standalone suite whenever the interpreter is available.
It covers binary strings, Reader/Writer
roundtrips, exact preset bytes, ownership, nested output, resource exhaustion
and structured diagnostics.

`tests/stream_spec.lua` runs under busted and directly through CTest. It
covers incremental windows, resumable visits, Sources and preservation, Tree
Reader items, events, subtree skipping and limits, raw copies, event writing
and measurement. Its `--allocator` mode injects Lua allocation failures during
visits, measurement and Source creation and checks that no callback or item
stays referenced. CTest also runs `examples/stream.lua`.

## Schema implementation

`src/schema.c` snapshots Lua descriptions into `tlv_structure_schema_t`,
`tlv_structure_rule_t`, `tlv_schema_entry_t` and `tlv_structure_group_t` arrays.
A registry reference retains a private table holding native-array userdata,
immutable tag/name strings and child Schema objects. The Schema finalizer is
installed before acquiring references; temporary diagnostic arrays are rooted
on the Lua stack, so constructor or result-conversion allocation failures do
not leak native storage. No Lua callback runs during C validation.

`Schema:validate()` calls `tlv_schema_validate_all_diag()`, or
`tlv_schema_validate()` with `first = true`. It returns bounded
schema reports or a basic native-code/offset diagnostic on fatal failures.
`schema:check()` runs `tlv_schema_prepare()` once; the checked handle roots the
immutable Schema snapshot it borrows and validates through
`tlv_schema_validate_checked()` and `tlv_schema_validate_all_checked()`.
`length_schema()` and `value_constraint()` snapshot `tlv_schema_entry_t` and
`tlv_value_constraint_t` tables with the same reference-table ownership.
`src/error.c` provides common `tlv_diagnostic_t` conversion shared with Reader
and Writer; Schema adds only the typed detail exposed by the C report and the
`tlv_schema_cause_string()` name of its cause.

`tests/schema_spec.lua` runs under busted and directly through CTest.
It covers rules, groups, ordering, paths, native offsets,
capacity/count-only results, malformed input, numeric checks, immutable
configuration and garbage collection. When the test allocator is available,
it also injects failures during construction and diagnostic conversion and
checks that retained child schemas are released.

## Query and Document ownership tests

`tests/query_document_spec.lua` runs directly through CTest and under busted.
It covers query diagnostics and matching, mutation rollback, foreign and stale
handles and configured Format lifetimes. Its
`--allocator` mode injects Lua allocation failures during construction, result
conversion, insertion and serialization. Document userdata acquire their
finalizer before allocating native state; Node userdata root the owning
Document. A Node stores its address, its never-reused Core identity and the
Document retirement epoch it was checked against. When
`tlv_document_retire_epoch()` changes, `tlv_document_node_identity()` decides
whether the address still names the same node, so erased or replaced nodes are
detected without a binding-side handle list or copy of the tree. An applied
Query edit still invalidates every earlier Node. Query evaluation pulls native
Tree Reader items between Lua allocations, avoiding Lua long jumps through
native callbacks.

Path visits (`document:visit()`, `document:query()`, `query:visit()` and matcher
visits) and compiled Document result visits run Node or element projection and
the Lua callback inside `lua_pcall()` from the Core visitor, store a Lua error
and return `TLV_VISIT_ERROR`, then raise it again after Core returns. The
Document rejects mutation and release while a visit runs. A Document builder
roots its Tree Reader and marks the cursor as owned, so pulls, skips and visits
raise `INVALID_STATE` until the builder completes, fails or is closed; the
owning Document userdata exists before `tlv_document_builder_consume()`
transfers the tree into it.

`tests/document_query_schema_spec.lua` runs under busted and directly through
CTest. It covers Document revisions, identity-checked Nodes, source locations,
direct child lookup, path visits, Document builders, canonical Query paths,
buffer and matcher visits, static compilation, native plans, in-place reset,
codec providers, Document result visits and Value snapshot sizing, checked and
first-failure Schema validation, length schemas and value constraints. Its
`--allocator` mode injects Lua allocation failures across these operations and
checks that a Document used during failed visits accepts edits afterwards.
CTest also runs `examples/audit.lua`.
