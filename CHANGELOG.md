# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Changed

- Report the independent Lua binding version through `version()` and `_VERSION`, using the same generated value as package/release metadata, with Lua checks for build and installed package consistency. Core identity remains in build/release provenance only. (#18)
- Require all stable Core features supported by Lua in every official build, remove Core feature selection, reject incomplete Core packages, and align component checks and documentation with the single full-featured configuration. (#16)

### Fixed

- Distinguish migration-era Core-aligned documentation from current independently versioned Lua manuals, while preserving source provenance and historical validation. (#21)
- Make shared Core DLLs discoverable by Windows Lua tests and run the LuaRocks package manager under PUC Lua for LuaJIT to avoid oversized-manifest failures. (#13)
- Use `v`-prefixed Lua release tags while accepting an unprefixed semantic version, preserving release quality gates and reporting the semantic binding version separately from the full Git tag. (#11)
- Accept both CTest success-summary formats in Lua CI so passing Windows suites continue to the Query and packaging checks. (#4)

### Added

- Notify Docs after successful Main CI when Lua documentation or examples change, using attempt-bound evidence and a separate dispatch workflow. (OpenTLV/opentlv-docs#42)
- Notify Docs after verified GitHub releases using exact release, commit and workflow-attempt evidence. (OpenTLV/opentlv-docs#42)
- Align Lua PR, Main, Full and Release gates with runtime and sanitizer coverage, verified Core dependencies, validated module packages and CI timing evidence. (#13)
- Own the migrated Lua API guide and implementation notes, link standalone build/test instructions and preserve the initial README as a historical provenance artifact. (OpenTLV/opentlv-docs#27)
- Adapt standalone LuaRocks installation, add Lua version metadata independent of Core, and gate manual release tags on the full Lua CI matrix with verified commit/Core provenance and test-count summaries. (#4)
- Add standalone Lua CI with the existing platform and Lua version matrix, LuaRocks test dependencies, and Query facade conformance checks using the pinned Core corpus. (#3)
- Add a standalone CMake build using local, installed, or pinned remote Core through its public C target, preserving Lua module loading, optional components, and existing tests. (#2)
- Import the existing Lua binding sources, tests, examples, and original build metadata from OpenTLV Core without changing the public API, with a pinned source inventory and migration provenance. (#1)
