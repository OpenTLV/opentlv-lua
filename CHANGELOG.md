# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Fixed

- Use `v`-prefixed Lua release tags while accepting an unprefixed semantic version, preserving release quality gates and reporting the semantic binding version separately from the full Git tag. (#11)
- Accept both CTest success-summary formats in Lua CI so passing Windows suites continue to the Query and packaging checks. (#4)

### Added

- Own the migrated Lua API guide and implementation notes, link standalone build/test instructions and preserve the initial README as a historical provenance artifact. (OpenTLV/opentlv-docs#27)
- Adapt standalone LuaRocks installation, add Lua version metadata independent of Core, and gate manual release tags on the full Lua CI matrix with verified commit/Core provenance and test-count summaries. (#4)
- Add standalone Lua CI with the existing platform and Lua version matrix, LuaRocks test dependencies, and Query facade conformance checks using the pinned Core corpus. (#3)
- Add a standalone CMake build using local, installed, or pinned remote Core through its public C target, preserving Lua module loading, optional components, and existing tests. (#2)
- Import the existing Lua binding sources, tests, examples, and original build metadata from OpenTLV Core without changing the public API, with a pinned source inventory and migration provenance. (#1)
