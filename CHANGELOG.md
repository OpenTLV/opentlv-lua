# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Changed

- Report the independent Lua binding version through `version()` and `_VERSION`, using the same generated value as package/release metadata, with Lua checks for build and installed package consistency. Core identity remains in build/release provenance only. (#18)
- Require all stable Core features supported by Lua in every official build, remove Core feature selection, reject incomplete Core packages, and align component checks and documentation with the single full-featured configuration. (#16)

### Fixed

- Correct the `LICENSE` copyright holder from the OpenTLV organization, inherited from the GitHub template, to Marek Cingel, matching the source headers and the Core license; packages built from this revision carry the corrected `LICENSE-Lua`. (#23)
- Distinguish migration-era Core-aligned documentation from current independently versioned Lua manuals, while preserving source provenance and historical validation. (#21)
- Make shared Core DLLs discoverable by Windows Lua tests and run the LuaRocks package manager under PUC Lua for LuaJIT to avoid oversized-manifest failures. (#13)
- Use `v`-prefixed Lua release tags while accepting an unprefixed semantic version, preserving release quality gates and reporting the semantic binding version separately from the full Git tag. (#11)
- Accept both CTest success-summary formats in Lua CI so passing Windows suites continue to the Query and packaging checks. (#4)

### Added

- Compose custom Formats without C: `opentlv.formats.compose(config)` composes a fixed or variable identifier encoding (with an optional identifier policy) and a fixed, variable (with an optional length policy) or escaped length encoding, with TLV or LTV order, Value or Tag-and-Value length scope, tag-only identifiers and constructed-bit nesting, through Core's Fixed, tagged binary, Variable, escaped and generic field compositions. `opentlv.formats.packed(config)` composes a packed Tag/Length header with an owned canonical identifier table, and `opentlv.fields.read_packed()`/`write_packed()` extract and insert one bit field of a header integer. A composed Format snapshots its configuration into its own userdata and works with every Format entry point: Reader, Writer, Tree Writer, traversal, Query, compiled Query programs, Document and Schema validation. Every Format, including builtin and fixed ones, exposes `can_read()`, `can_write()`, `decode()`, `measure()` and `encode()`, failing with an error that carries the Format's operation, region, offset, required size and field ranges. The new `compose.lua` example uses one composed Format with Tree Writer, traversal, Query and Document. Callback implementations installed by bound descriptor initializers are in the API coverage ignore list with their reason; core coverage of Core `d87dcf96b270` is 147/235 (62.6 %). (#27)
- Report pinned Core API coverage in PR and Full CI as a non-blocking job: `TLV_API` functions and data referenced from the native module sources in `src`, in total and per header, for core and builtins separately; added and removed symbols when a pull request changes the Core pin; and a documented ignore list for primitives and diagnostic producers in `cmake/api-coverage.json`. The script is the Python and Rust repositories' standard-library report with `cmake/api-coverage.json` as its default configuration and a statement that symbol references do not prove functional parity; core coverage of Core `d87dcf96b270` is 118/254 (46.5 %). (#26)
- Notify Docs after successful Main CI when Lua documentation or examples change, using attempt-bound evidence and a separate dispatch workflow. (OpenTLV/opentlv-docs#42)
- Notify Docs after verified GitHub releases using exact release, commit and workflow-attempt evidence. (OpenTLV/opentlv-docs#42)
- Align Lua PR, Main, Full and Release gates with runtime and sanitizer coverage, verified Core dependencies, validated module packages and CI timing evidence. (#13)
- Own the migrated Lua API guide and implementation notes, link standalone build/test instructions and preserve the initial README as a historical provenance artifact. (OpenTLV/opentlv-docs#27)
- Adapt standalone LuaRocks installation, add Lua version metadata independent of Core, and gate manual release tags on the full Lua CI matrix with verified commit/Core provenance and test-count summaries. (#4)
- Add standalone Lua CI with the existing platform and Lua version matrix, LuaRocks test dependencies, and Query facade conformance checks using the pinned Core corpus. (#3)
- Add a standalone CMake build using local, installed, or pinned remote Core through its public C target, preserving Lua module loading, optional components, and existing tests. (#2)
- Import the existing Lua binding sources, tests, examples, and original build metadata from OpenTLV Core without changing the public API, with a pinned source inventory and migration provenance. (#1)
