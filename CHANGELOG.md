# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added

- Add standalone Lua CI with the existing platform and Lua version matrix, LuaRocks test dependencies, and Query facade conformance checks using the pinned Core corpus. (#3)
- Add a standalone CMake build using local, installed, or pinned remote Core through its public C target, preserving Lua module loading, optional components, and existing tests. (#2)
- Import the existing Lua binding sources, tests, examples, and original build metadata from OpenTLV Core without changing the public API, with a pinned source inventory and migration provenance. (#1)
