# Lua source migration

## Provenance

| Field | Value |
| --- | --- |
| Source repository | [OpenTLV/opentlv](https://github.com/OpenTLV/opentlv) |
| Source commit | `d87dcf96b2707e817dd51163a9f9efa36651e929` |
| Source directory | [`bindings/lua/`](https://github.com/OpenTLV/opentlv/tree/d87dcf96b2707e817dd51163a9f9efa36651e929/bindings/lua) |
| Destination | [OpenTLV/opentlv-lua](https://github.com/OpenTLV/opentlv-lua) |
| Migration issue | [opentlv-lua#1](https://github.com/OpenTLV/opentlv-lua/issues/1) |
| Parent epic | [opentlv#632](https://github.com/OpenTLV/opentlv/issues/632) |

The import copies committed Git blobs from the recorded revision without
transferring Git history. It preserves source bytes, including original SPDX
and copyright notices. No binding implementation, test, example, CMake, or
rockspec content is modified in this step.

Workspace integration is complete in
[opentlv-workspace#28](https://github.com/OpenTLV/opentlv-workspace/issues/28).
The source revision above is the provenance baseline, not a Lua release version
or a decision about the remote dependency pin for the future standalone build.

## Source inventory

All 54 tracked files under the original `bindings/lua/` directory are retained:

| Source relative to `bindings/lua/` | Destination | Files |
| --- | --- | ---: |
| `src/` | `src/` | 33 |
| `lua/` | `lua/` | 1 |
| `examples/` | `examples/` | 4 |
| `tests/` | `tests/` | 13 |
| `CMakeLists.txt` | `CMakeLists.txt` | 1 |
| `opentlv-scm-1.rockspec` | `opentlv-scm-1.rockspec` | 1 |
| `README.md` | `docs/migration/original-readme.md` | 1 |

[source-inventory.json](migration/source-inventory.json) records every source
path, destination path, and SHA-256 digest of the original Git blob. The root
README documents the migration state; the original README is retained separately
without editing its historical links or commands.

The inventory records the initial import. Later build, packaging, or source
changes should be reviewed against this baseline rather than silently updating
its recorded hashes.

## Preserved contracts

- `require("opentlv")`, `require("opentlv._core")`, and
  `luaopen_opentlv__core` retain their existing names and implementation.
- Module registration, public Lua APIs, userdata ownership, garbage collection,
  error handling, and diagnostic mappings are unchanged.
- Lua 5.1–5.4 and LuaJIT compatibility helpers are unchanged.
- Optional Core component integration and platform-specific linking logic are
  retained in their original sources and build metadata.
- `version()` and `_VERSION` continue to report the linked Core version.

## Verification and remaining validation

Source-import verification compares the manifest against the complete Git tree
at the recorded revision, checks each destination byte-for-byte against its
original Git blob, and verifies its SHA-256 digest. This establishes source
identity, including native initialization and the public Lua entry point.

At import, all 54 files passed both byte comparison against the pinned Git
blobs and SHA-256 verification. The `src/`, `lua/`, `tests/`, and `examples/`
inventories matched exactly, with no missing or extra files. Local links in
the new documentation and whitespace checks also passed.

This import does not establish standalone runtime compatibility. No new runtime
test baseline or hosted CI pass is claimed here. Runtime baseline capture and
comparison must identify the Core revision, Lua runtime, platform, component
configuration, and exact test commands and results when the standalone build
and test migration are implemented.

The existing Core workflow is available at
[the recorded revision](https://github.com/OpenTLV/opentlv/blob/d87dcf96b2707e817dd51163a9f9efa36651e929/.github/workflows/lua.yml).
Its shared Query corpus integration is outside `bindings/lua/` and remains in
Core during this source import. Issue #3 must resolve that dependency while
preserving the existing Lua facade coverage and Core test ownership.

## Follow-up ownership

| Work | Issue |
| --- | --- |
| Standalone CMake and external Core dependency | [opentlv-lua#2](https://github.com/OpenTLV/opentlv-lua/issues/2) |
| Test execution, CI, and shared Query corpus integration | [opentlv-lua#3](https://github.com/OpenTLV/opentlv-lua/issues/3) |
| LuaRocks adaptation, independent versioning, and release baseline | [opentlv-lua#4](https://github.com/OpenTLV/opentlv-lua/issues/4) |
| Core cleanup after standalone validation | [opentlv#633](https://github.com/OpenTLV/opentlv/issues/633) |
| Portal documentation migration | [opentlv-docs#27](https://github.com/OpenTLV/opentlv-docs/issues/27) |

The standalone changelog starts with the source import. Independent release
versioning and the first standalone release baseline remain part of #4. Core
sources and CI remain in place until the standalone migration is validated.
