## Overview

Expands the public Lua binding with streaming, Format composition, Document, Query, Schema, codec and Generator APIs, and adopts the verified Core v0\.8\.0 baseline\.

## What's Changed

### Added

- Add the Workspace `.markdownlint.json` configuration and explicit MIT licensing in `REUSE.toml` to align Markdown lint settings across repositories. ([#40](https://github.com/OpenTLV/opentlv-lua/issues/40))
- Complete and gate the generic Core API: `opentlv.definition_registry(definitions)` snapshots `{tag, name}` entries, and `registry:find(tag)` returns an owned copy of the first entry with an equal Tag and its index through `tlv_definition_find()`, or `nil`; `registry:definitions()` returns the entries in order. Diagnostic tables with a tracked path gain `path_text`, Core's `tlv_diagnostic_path_string()` formatting of the enclosing Tags, such as `"30 > A5"`, ending in `" > ..."` when truncated. The `audit.lua` example names a Schema finding through a registry and prints its path. `tlv_location_translate` joins the API coverage ignore list with its reason: Lua has no caller-owned destinations or sub-buffer origins, so Core's coordinates are reported unchanged. `cmake/api-coverage.json` lists `core` as `complete`, so `Core API coverage` fails when a core symbol is neither referenced nor ignored or an ignore pattern matches nothing, after writing and uploading its report, and is now a `Lua quality gate` prerequisite instead of an informational job. The Core API coverage record (`docs/api-coverage.md`) is the reviewed baseline for Core pin updates: every core header with its public Lua API and binding tests, the exclusions, the limitations and the update procedure. Core coverage of Core `d87dcf96b270` is 204/204 (100.0 %) with 93 ignored. ([#31](https://github.com/OpenTLV/opentlv-lua/issues/31))
- Complete generic codec and wire generation APIs: `opentlv.codecs.schema_number{rule = ..., encoding, width, digits}` limits a numeric codec by a length rule through `tlv_schema_number_codec()`, checking Value lengths before decoding and encoding in the shortest permitted width, and also works as a Query provider codec. `opentlv.structure_codec(config)` maps one complete sequence to one Lua value through `tlv_structure_decode()` and `tlv_structure_encode()`: C validates framing, nesting, limits and an optional Schema or checked Schema before the `decode` callback sees the sequence, and validates the string `encode` returns before handing it back, with Reader or Schema findings in `codec_detail` and output coordinates for encode failures. `opentlv.generator(config)` generates reproducible Writer-reconstructible cases through `tlv_generate()` from candidate identifiers with inclusive Value size intervals, element, depth, Value and case limits and a 64-bit seed; `generate(case_index)` returns one case, `workspace_size()` reports its scratch size through `tlv_generator_workspace_size()` and `opentlv.GENERATOR_VERSION` exposes the algorithm version. The new `generate.lua` example decodes generated records through a structure codec, and the API guide no longer lists the Document builder as unbound. The number, text, digits and Schema-number codec callbacks that Core's inline constructors install are in the API coverage ignore list with their reason; core coverage of Core `d87dcf96b270` is 202/205 (98.5 %). ([#30](https://github.com/OpenTLV/opentlv-lua/issues/30))
- Complete Document, Query and Schema operations: `document:revision()` counts successful edits, `node:source_location()` reports retained original input coordinates, `document:find_child(tag, parent)` finds a direct child by tag and `document:visit(path, callback)` visits path matches as Nodes through `tlv_document_query_visit()`, which `document:query()` now also uses. Nodes are checked with Core's never-reused node identity, `tlv_document_retire_epoch()` and `tlv_document_node_identity()` instead of a binding-side handle list; an applied Query edit still invalidates every earlier Node. `tree_reader:document_builder(options)` copies the whole stream, the next subtree or the subtree of the item `next()` returned into a Document through the C Document builder, resumes after `NEED_MORE_DATA` and owns the cursor until it completes. `query:format()` and `tostring(query)` return the canonical path through `tlv_query_format()`, `query:visit()` visits complete input through `tlv_query_visit_buffer()`, and matcher `visit()` runs `tlv_query_visit()` and returns `visited, stopped`. Compiled Query programs with static names compile directly through `tlv_query_compile_scratch()` and `tlv_query_compile()`; `opentlv.query_plan_open()` and `program:plan_info()` open and inspect native plans without the Query frontend; `execution:reset()` resets in place through `tlv_query_exec_reset()`; `execution:visit()` visits Document results as Nodes through `tlv_document_query_program_visit()`; Document evaluation without a capacity sizes the Value snapshot with `tlv_document_query_value_size()`; providers accept Value codecs decoded by `tlv_query_codec_decode()`; and Query errors carry `cause` and `cause_name`. `schema:check()` checks a definition once through `tlv_schema_prepare()` and returns a checked Schema that validates through `tlv_schema_validate_checked()` and `tlv_schema_validate_all_checked()`, `validate(data, format, {first = true})` stops at the first failure through `tlv_schema_validate()`, Schema diagnostics carry their `cause`, `opentlv.length_schema()` finds and validates per-tag lengths, and `opentlv.value_constraint()` checks, validates and names decoded values. The new `audit.lua` example builds a subtree Document and validates it with a checked Schema and a value constraint. `tlv_query_parse()` and `tlv_schema_check()` are in the API coverage ignore list with their reasons; core coverage of Core `d87dcf96b270` is 198/213 (93.0 %). ([#29](https://github.com/OpenTLV/opentlv-lua/issues/29))
- Stream through native Reader, Tree Reader and Writer sessions: `opentlv.reader(data, format, {final_input = false})` and the new `opentlv.tree_reader(data, format, options)` read incremental input, raising `NEED_MORE_DATA` until `set_input(data, discard, final_input)` supplies a window that keeps the undiscarded bytes, and report `consumed()`, absolute `offset()` and `at_end()`. `opentlv.tree_reader()` pulls preorder items with `depth` and `constructed`, balanced `begin`/`element`/`end` events through `next_event()`, skips subtrees with `skip_subtree()` (also after a failed descent) and applies frame, depth and element limits. `reader:visit()` and `tree_reader:visit()` resume the C Visitor on an existing cursor and reject reentry from the callback. `reader:next_source()` and `opentlv.read_source()` return an `opentlv.Source` with the original bytes and field layout; `source:preserve()` and `writer:preserve()` reproduce them through Core only while the element is unchanged. `opentlv.read()` reads one element, `writer:copy_encoded()` appends raw bytes and `opentlv.encoded_size()` measures an Element or a Value length. `tree_writer:write_event()` consumes the events a Tree Reader produces, and `opentlv.measure_tree()` and `opentlv.measure_events()` stage a whole tree in one call, reporting `required_data` and `required_scratch` when the workspace is too small. The new `stream.lua` example feeds chunked input through a Tree Reader into a Tree Writer. Wrappers that pass a NULL diagnostic to a bound `_diag` function, the Tag/Value and caller-storage write forms and `tlv_writer_copy_element()` are in the API coverage ignore list with their reasons; core coverage of Core `d87dcf96b270` is 166/215 (77.2 %). ([#28](https://github.com/OpenTLV/opentlv-lua/issues/28))
- Compose custom Formats without C: `opentlv.formats.compose(config)` composes a fixed or variable identifier encoding (with an optional identifier policy) and a fixed, variable (with an optional length policy) or escaped length encoding, with TLV or LTV order, Value or Tag-and-Value length scope, tag-only identifiers and constructed-bit nesting, through Core's Fixed, tagged binary, Variable, escaped and generic field compositions. `opentlv.formats.packed(config)` composes a packed Tag/Length header with an owned canonical identifier table, and `opentlv.fields.read_packed()`/`write_packed()` extract and insert one bit field of a header integer. A composed Format snapshots its configuration into its own userdata and works with every Format entry point: Reader, Writer, Tree Writer, traversal, Query, compiled Query programs, Document and Schema validation. Every Format, including builtin and fixed ones, exposes `can_read()`, `can_write()`, `decode()`, `measure()` and `encode()`, failing with an error that carries the Format's operation, region, offset, required size and field ranges. The new `compose.lua` example uses one composed Format with Tree Writer, traversal, Query and Document. Callback implementations installed by bound descriptor initializers are in the API coverage ignore list with their reason; core coverage of Core `d87dcf96b270` is 147/235 (62.6 %). ([#27](https://github.com/OpenTLV/opentlv-lua/issues/27))
- Report pinned Core API coverage in PR and Full CI as a non-blocking job: `TLV_API` functions and data referenced from the native module sources in `src`, in total and per header, for core and builtins separately; added and removed symbols when a pull request changes the Core pin; and a documented ignore list for primitives and diagnostic producers in `cmake/api-coverage.json`. The script is the Python and Rust repositories' standard-library report with `cmake/api-coverage.json` as its default configuration and a statement that symbol references do not prove functional parity; core coverage of Core `d87dcf96b270` is 118/254 (46.5 %). ([#26](https://github.com/OpenTLV/opentlv-lua/issues/26))
- Notify Docs after successful Main CI when Lua documentation or examples change, using attempt-bound evidence and a separate dispatch workflow. ([OpenTLV/opentlv-docs#42](https://github.com/OpenTLV/opentlv-docs/issues/42))
- Notify Docs after verified GitHub releases using exact release, commit and workflow-attempt evidence. ([OpenTLV/opentlv-docs#42](https://github.com/OpenTLV/opentlv-docs/issues/42))
- Align Lua PR, Main, Full and Release gates with runtime and sanitizer coverage, verified Core dependencies, validated module packages and CI timing evidence. ([#13](https://github.com/OpenTLV/opentlv-lua/issues/13))

### Changed

- Prepare the independently versioned Lua v0.2.0 release against Core v0.8.0: pin both dependency selectors to `345c77acb29266036df5f27d04ce315bb61853cc`, select the published Core release for authenticated binary/source consumption, recheck generic API completeness and document the remaining release gates. Preserve the original migration provenance. ([#40](https://github.com/OpenTLV/opentlv-lua/issues/40))
- Normalize Lua licensing and community policies to the Workspace reference: `REUSE.toml` and `LICENSES/MIT.txt` cover every file, listing documentation, JSON data and the byte-preserved migration records (the initial source inventory and the historical README) by exact path, and the rockspec, clang-format, clang-tidy, pre-commit and Git configuration and `requirements-dev.txt` gain SPDX headers. The pre-commit hooks of the source quality job run `reuse lint` with REUSE 6.2.0, and licensing regression tests reject glob or stale annotations, a changed historical README, a license text differing from `LICENSE`, a rockspec without `MIT` and OpenTLV as a copyright holder. The README and the development guide state Marek Cingel's copyright, contributor ownership, the license notices of module archives, the rock and binaries that statically link Core, and the shared `OpenTLV/.github` contribution, conduct, security and support policies. ([#38](https://github.com/OpenTLV/opentlv-lua/issues/38))
- Report the independent Lua binding version through `version()` and `_VERSION`, using the same generated value as package/release metadata, with Lua checks for build and installed package consistency. Core identity remains in build/release provenance only. ([#18](https://github.com/OpenTLV/opentlv-lua/issues/18))
- Require all stable Core features supported by Lua in every official build, remove Core feature selection, reject incomplete Core packages, and align component checks and documentation with the single full-featured configuration. ([#16](https://github.com/OpenTLV/opentlv-lua/issues/16))

### Fixed

- Recover timing telemetry from the final `Complete job` step when GitHub leaves a job marked `in_progress` despite all steps being complete; verify the run/attempt and step timestamps, label the fallback source and preserve the API's unknown conclusion without changing quality or release gates. ([#40](https://github.com/OpenTLV/opentlv-lua/issues/40))
- Allow delayed GitHub Actions job timing metadata to arrive over 18 attempts at 10-second intervals, retaining exact-attempt collection and rejection of incomplete reports; include pending job details in retry and failure diagnostics. ([#40](https://github.com/OpenTLV/opentlv-lua/issues/40))
- Correct the `LICENSE` copyright holder from the OpenTLV organization, inherited from the GitHub template, to Marek Cingel, matching the source headers and the Core license; packages built from this revision carry the corrected `LICENSE-Lua`. ([#23](https://github.com/OpenTLV/opentlv-lua/issues/23))
- Distinguish migration-era Core-aligned documentation from current independently versioned Lua manuals, while preserving source provenance and historical validation. ([#21](https://github.com/OpenTLV/opentlv-lua/issues/21))
- Make shared Core DLLs discoverable by Windows Lua tests and run the LuaRocks package manager under PUC Lua for LuaJIT to avoid oversized-manifest failures. ([#13](https://github.com/OpenTLV/opentlv-lua/issues/13))

## Compatibility

Lua binding **0.2.0**, Git tag `v0.2.0`.

Verified baseline: [Core v0.8.0](https://github.com/OpenTLV/opentlv/releases/tag/v0.8.0) at `345c77acb29266036df5f27d04ce315bb61853cc`.

Tested runtime/platform combinations:

- Darwin arm64 / Lua 5\.1
- Darwin arm64 / Lua 5\.2
- Darwin arm64 / Lua 5\.3
- Darwin arm64 / Lua 5\.4
- Linux x86\_64 / Lua 5\.1
- Linux x86\_64 / Lua 5\.1 LuaJIT 2\.1\.1792588164
- Linux x86\_64 / Lua 5\.2
- Linux x86\_64 / Lua 5\.3
- Linux x86\_64 / Lua 5\.4
- Windows AMD64 / Lua 5\.1
- Windows AMD64 / Lua 5\.2
- Windows AMD64 / Lua 5\.3
- Windows AMD64 / Lua 5\.4

These are tested configurations, not a claim of binaries for every platform.

## Installation / Distribution

GitHub module archives contain the public Lua facade, native module and license notices. They target the matching Lua 5.4 runtime on Linux/Windows x64.

- [OpenTLV-Lua-0.2.0-linux-x64-lua54.zip](https://github.com/OpenTLV/opentlv-lua/releases/download/v0.2.0/OpenTLV-Lua-0.2.0-linux-x64-lua54.zip)
- [OpenTLV-Lua-0.2.0-windows-x64-lua54.zip](https://github.com/OpenTLV/opentlv-lua/releases/download/v0.2.0/OpenTLV-Lua-0.2.0-windows-x64-lua54.zip)

After downloading and verifying the Linux archive below, extract it in a fresh directory and load it with Lua 5.4:

```sh
unzip OpenTLV-Lua-0.2.0-linux-x64-lua54.zip -d opentlv-lua
LUA_PATH="$PWD/opentlv-lua/?/init.lua" LUA_CPATH="$PWD/opentlv-lua/?.so" \
    lua -e 'assert(require("opentlv") == require("opentlv._core"))'
```

LuaRocks registry publication is a separate approved operation. These release notes do not establish registry availability.

## Verification and artifacts

[Release Full CI, attempt 1](https://github.com/OpenTLV/opentlv-lua/actions/runs/40522912843/attempts/1) records 17 passed configurations for Lua commit `77abc807bdd59956790497483a9d1fbad4b79019`. Required build, source quality, CodeQL and API coverage jobs passed.

| Configuration | CTest | Busted | Query fixtures / checks |
| --- | ---: | ---: | ---: |
| macos\-latest\-5\.1\-OFF | 28/28 | 165 | 171 / 3422 |
| macos\-latest\-5\.2\-OFF | 28/28 | 165 | 171 / 3422 |
| macos\-latest\-5\.3\-OFF | 28/28 | 165 | 171 / 3422 |
| macos\-latest\-5\.4\-OFF | 28/28 | 165 | 171 / 3422 |
| macos\-latest\-5\.4\-ON | 28/28 | 165 | 171 / 3422 |
| ubuntu\-24\.04\-5\.1\-OFF | 28/28 | 165 | 171 / 3422 |
| ubuntu\-24\.04\-5\.2\-OFF | 28/28 | 165 | 171 / 3422 |
| ubuntu\-24\.04\-5\.3\-OFF | 28/28 | 165 | 171 / 3422 |
| ubuntu\-24\.04\-5\.4\-OFF | 28/28 | 165 | 171 / 3422 |
| ubuntu\-24\.04\-5\.4\-OFF\-asan | 28/28 | 165 | 171 / 3422 |
| ubuntu\-24\.04\-5\.4\-ON | 28/28 | 165 | 171 / 3422 |
| ubuntu\-24\.04\-luajit\-OFF | 28/28 | 165 | 171 / 3422 |
| windows\-latest\-5\.1\-OFF | 28/28 | 165 | 171 / 3422 |
| windows\-latest\-5\.2\-OFF | 28/28 | 165 | 171 / 3422 |
| windows\-latest\-5\.3\-OFF | 28/28 | 165 | 171 / 3422 |
| windows\-latest\-5\.4\-OFF | 28/28 | 165 | 171 / 3422 |
| windows\-latest\-5\.4\-ON | 28/28 | 165 | 171 / 3422 |

The release gate verifies checksums, producer attestations and same-attempt jobs before publication. Artifact SHA-256 values from the verified build records:

| Artifact | SHA-256 |
| --- | --- |
| [OpenTLV-Lua-0.2.0-linux-x64-lua54.zip](https://github.com/OpenTLV/opentlv-lua/releases/download/v0.2.0/OpenTLV-Lua-0.2.0-linux-x64-lua54.zip) | `a19804aee0a85e28a732c473f5ab44b79377f064c06f192843abc8d30abbe111` |
| [OpenTLV-Lua-0.2.0-windows-x64-lua54.zip](https://github.com/OpenTLV/opentlv-lua/releases/download/v0.2.0/OpenTLV-Lua-0.2.0-windows-x64-lua54.zip) | `69e4133b9df7530a9e8d1eeb0c65006fc37a4311080ad584a1226001981cb5df` |

Core release ID: `425487758`; authenticated Core manifest SHA-256: `48e4bfdb6c83d339c6cf18dc8138f55b6a32288dcd73f687c26d9043b8bae821`.

Download `SHA256SUMS` and the assets into one directory and check their bytes with `sha256sum --check SHA256SUMS`. Verify each asset using, for example:

```sh
gh attestation verify OpenTLV-Lua-0.2.0-linux-x64-lua54.zip --repo OpenTLV/opentlv-lua \
    --signer-workflow OpenTLV/opentlv-lua/.github/workflows/release.yml \
    --source-digest 77abc807bdd59956790497483a9d1fbad4b79019
```

## Links

- [Full changelog](https://github.com/OpenTLV/opentlv-lua/blob/77abc807bdd59956790497483a9d1fbad4b79019/CHANGELOG.md)
- [Lua API documentation](https://github.com/OpenTLV/opentlv-lua/blob/77abc807bdd59956790497483a9d1fbad4b79019/docs/api.md)
- [Installation guide](https://github.com/OpenTLV/opentlv-lua/blob/77abc807bdd59956790497483a9d1fbad4b79019/docs/development.md)
- [Release workflow evidence](https://github.com/OpenTLV/opentlv-lua/actions/runs/40522912843/attempts/1)
- [Release manifest](https://github.com/OpenTLV/opentlv-lua/releases/download/v0.2.0/release-manifest.json)
- [SHA256SUMS](https://github.com/OpenTLV/opentlv-lua/releases/download/v0.2.0/SHA256SUMS)
