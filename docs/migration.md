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
The source revision above records provenance, not a Lua release version. The
initial import did not select a remote dependency pin; the standalone build
follow-up below records that decision.

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
- Lua 5.1â€“5.4 and LuaJIT compatibility helpers are unchanged.
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

## Standalone build follow-up (#2)

The standalone build pins the same Core commit as the source import. The
original #2 integration adapted `CMakeLists.txt` and read optional components
from the selected Core's public generated configuration. Issue #16 supersedes
that selection policy: official builds now require every supported feature.
The reduced-build results below remain historical migration evidence.
See the current [development instructions](development.md#components-and-tests).

Two headers used by the original Lua sources lived outside `bindings/lua/`.
They are now retained locally, byte-for-byte from the same source commit:

| Original path | Destination | SHA-256 |
| --- | --- | --- |
| `bindings/common/path_query.h` | `src/path_query.h` | `f6a328b062e6e22d61c644b7de0b6ce51e4e3b6bb23ecdbf0ba5b85d89d9ee2a` |
| `bindings/common/query_schema.h` | `src/query_schema.h` | `7abe853c2eb5269b8bbaf7569c644e8d370fe790a1e056aefac62c0d543aa2e8` |

Only the include paths in `query.c` and `program.c` change. These helpers adapt
binding diagnostics and owning workspace storage through public C APIs; they
contain no private Core engine implementation. The initial #1 inventory remains
unchanged as the historical import record.

Local validation for #2 used the recorded Core commit and existing tests:

| Environment | Core selection | Result |
| --- | --- | --- |
| Windows x64, MSVC 19.44, Lua 5.4.8 | Local static / installed static | 15/15 tests each |
| Linux (WSL), GCC 13.3, Lua 5.4.6 | Local static / relocated installed static / remote pinned static | 15/15 tests each |
| Linux (WSL), GCC 13.3, Lua 5.4.6 | Local shared | 15/15 tests |
| Linux (WSL), GCC 13.3, Lua 5.4.6 | Local / installed static, Document and optional formats disabled | 13/13 tests each |

The checks also verified both module names load the same module, no C++ compiler
is configured, an explicit source override takes precedence over an installed
package, installed component selection ignores contradictory consumer cache
options, and missing required Reader capability fails during configuration.
The reduced configurations intentionally skip the Busted suite and BER parsing
example under the original registration rules. CMake versions were 3.31.7 on
Windows and 3.28.3 on Linux.

macOS, other Lua ABIs, installed shared Core on Windows, and hosted CI were not
executed in this validation. macOS linking and Lua compatibility code remain
unchanged. This is local build validation; #3 still owns hosted CI and the
shared Query corpus integration.

## Migration work ownership

The #3 workflow, Query runner migration, original CI baseline, and local
comparison results are recorded in [tests and CI](testing.md).

| Work | Issue |
| --- | --- |
| Standalone CMake and external Core dependency | [opentlv-lua#2](https://github.com/OpenTLV/opentlv-lua/issues/2) |
| Test execution, CI, and shared Query corpus integration | [opentlv-lua#3](https://github.com/OpenTLV/opentlv-lua/issues/3) |
| LuaRocks adaptation, independent versioning, and manual release quality gate | [opentlv-lua#4](https://github.com/OpenTLV/opentlv-lua/issues/4) |
| Core cleanup after standalone validation | [opentlv#633](https://github.com/OpenTLV/opentlv/issues/633) |
| Portal documentation migration | [opentlv-docs#27](https://github.com/OpenTLV/opentlv-docs/issues/27) |

The standalone changelog starts with the source import. Core sources and CI
were removed in [Core #633](https://github.com/OpenTLV/opentlv/issues/633).

## LuaRocks and versioning follow-up (#4)

The rockspec now builds from the destination repository root and installs the
same native `opentlv._core` and pure-Lua `opentlv` entry points. Its source URL
is `https://github.com/OpenTLV/opentlv-lua.git`. The original inventory remains
the record of the initial import, including the original rockspec's checksum.

Lua versions derive only from this repository's `v`-prefixed release tags. Versioning
starts when the first tag `v0.1.0` is added during quality-gate validation; until
then the binding remains `scm`.
The first standalone release baseline uses the source and dependency
revision `d87dcf96b2707e817dd51163a9f9efa36651e929` recorded above. The original tag-only gate used nine configurations; its implementation
created no tag by itself. The current [release quality gate](releases.md)
supersedes that workflow with PR/Main/Full/Release separation and verified
module artifacts. See [versioning](versioning.md) for the distinction between
Lua versions, LuaRocks revisions and the Core version reported by the API.

## Documentation ownership follow-up (opentlv-docs#27)

The [API guide](api.md) and [implementation notes](implementation.md) were
moved from Core commit `56b95a6754b4bd7210cc63252885db2c919f7178`
(`docs/guides/lua.md` and `docs/development/lua.md`). API descriptions and
examples are preserved; links and setup now use the standalone repository.
The portal assembles these sources with the build, testing and versioning guides.
Its documentation versions continue to follow Core, independently of Lua tags.

The original README now resides at
[provenance/original-readme.md](../provenance/original-readme.md), outside the
published guides. Its bytes are unchanged. The initial source inventory above
retains its original destination paths and hashes as an import record.
