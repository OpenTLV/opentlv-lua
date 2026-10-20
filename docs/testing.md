# Lua tests and CI

## Ownership and migration baseline

Lua owns its public API tests, conversions, userdata lifetime checks, diagnostics,
module loading, examples, allocator tests, and Query facade runner. Core owns
the shared Query corpus, its reference implementation, and native engine tests.
The Lua workflow does not run the Core test suite.

The workflow and Lua Query generator were migrated from Core commit
[`d87dcf96b2707e817dd51163a9f9efa36651e929`](https://github.com/OpenTLV/opentlv/tree/d87dcf96b2707e817dd51163a9f9efa36651e929).
That is also the dependency pin in `cmake/Core.cmake`. The original Git blob
SHA-256 digests are recorded below; checkout line-ending conversion can change
working-file hashes.

| Original Core path | SHA-256 |
| --- | --- |
| `.github/workflows/lua.yml` | `98c253c668c27b2415c63bb4017187ffe49288c699ba1134ba3cc0c8461ceaff` |
| `tests/query/lua_facade.py` | `b079e1d08faba82d7a454c98e17512ab8b86cfedc07e582950fc1e2141d302cc` |
| `tests/query/corpus.json` | `5e9d24f37a31562246959bfaeab23029337d539f5b5e1b6b3ad955040ad23612` |
| `tests/query/reference.py` | `70f8c7db87a715b63f8865fa3e48c566fb195ade159d49649bcd53ce9b99cc3d` |

The initial Lua test and example inventory is recorded in
[migration provenance](migration.md). The component smoke test now requires
every supported feature in the official build. The Query
generator changes only how it locates the shared Core inputs; its Lua runner,
fixture conversion, and assertions are preserved.

## CI selection and ownership

See [quality gates](quality-gates.md) for the four entry points and exact Full
matrix. PR runs the complete Lua suite on one Linux Lua 5.4 configuration;
Main runs only `tests/smoke.lua`. Full covers all four Lua 5.x ABIs on three
platforms, Linux LuaJIT, shared Core, installed-package consumption and sanitizers.

| Tests | Lua-owned behavior |
| --- | --- |
| `module_spec.lua`, `reader_spec.lua`, `format_spec.lua`, `error_spec.lua` | Public API, module identity, argument conversion and diagnostic tables |
| `codec_spec.lua`, `schema_spec.lua`, `writer_spec.lua`, `visitor_spec.lua` | Conversion, callbacks, retained inputs, ownership, GC and error propagation |
| `program_spec.lua`, `query_document_spec.lua` | Query/Document userdata, continuations, finalizers, mutation and lifetime |
| `failing_allocator.c` | Native helper for Lua allocation failure injection; no Core engine tests |
| `query/lua_facade.py` | Generates Lua public-API checks from Core-owned corpus/reference |
| `components.lua`, `nfc_type2.lua`, examples | Binding registration and public API integration |
| `test_*.py` | Version, workflow, gate, dependency identity and publication rejection contracts |

The full runner requires all 15 CTest entries, positive Busted/Query counts and
zero failed, pending or skipped tests. It builds Core only as the required
library. It does not duplicate Core parser/writer suites. The shared corpus
remains owned by Core and is always selected from the linked Core revision.

```sh
python -B scripts/ci.py --lua-prefix /absolute/path/to/lua --runtime 5.4 --rocks
python -B -m unittest discover -s tests -p 'test_*.py'
```

Select `--lua-executable`, `--lua-include` and `--busted` explicitly when needed.
Busted must use the same Lua runtime. `--core-source` is a local development
override; CI otherwise resolves the declared Core pin/artifact policy. Full
release runs reject local overrides. `--shared ON` selects shared Core;
`CC=clang-18` with `--configuration Debug --sanitizer` selects Linux ASan/UBSan.
Use `--smoke` for the narrow Main check, which does not need Busted.

`--rocks` independently builds and installs the rock into a fresh tree with
spaces in its path, checks installed version metadata and loads both modules
from outside the checkout. Packaging uses static Core from the same source
identity. The installed runtime search paths exclude inherited Lua initialization
settings. Windows uses PowerShell/MSVC so Git Bash cannot shadow `link.exe`.

The two official Lua 5.4 package configurations additionally run seven Lua scripts
and the full Query corpus after extraction, with only the archive module paths.
Allocator injection is exercised by the build suite, not shipped in the archive.

Every CI row uses the same full Core feature set described in
[development](development.md#components-and-tests). Runtime, platform, linkage
and sanitizer coverage remain independent of feature selection. The complete
CI runner rejects skipped registration. Tooling regressions exercise source
configuration enforcement and rejection of incomplete installed Core packages;
they do not validate Core's modular build combinations.

## Historical migration validation

The comparisons below record migration-time results, not execution of the new
quality gates or a claim of current hosted acceptance.

## Comparing with Core

At the recorded revision, build the original Core Lua target with
`OPENTLV_BUILD_LUA=ON`, `OPENTLV_BUILD_LUA_TESTS=ON`,
`OPENTLV_BUILD_CXX=OFF`, `OPENTLV_BUILD_TESTS=OFF`,
`OPENTLV_BUILD_EXAMPLES=OFF`, and `CMAKE_POSITION_INDEPENDENT_CODE=ON`.
Run its registered Lua tests, then generate the runner with the original
`tests/query/lua_facade.py --output ...` and execute it against the original
module under `<core-build>/bindings/lua/opentlv/`.

Use the same interpreter, Busted dependencies, Core options, and corpus for
both runs. Compare suite outcomes and Query fixture/check counts; the original
and migrated generators should produce identical runner bytes on the same host.
Record local results separately from hosted GitHub Actions results. Core cleanup
remains in Core #633 after standalone validation.

## Local migration validation

All runs below used the recorded Core revision with full component configuration.
CTest includes the Busted suite, so its 78 successes are not additional CTest
registrations. Local runtime and LuaRocks dependency installations were reused.

| Implementation | Environment | CTest | Busted | Query corpus |
| --- | --- | --- | --- | --- |
| Original Core binding | Linux/WSL, GCC 13.3, Lua 5.4.6 | 15/15 | 78 successes | 171 fixtures, 3422 checks |
| Standalone, pinned remote Core | Linux/WSL, GCC 13.3, Lua 5.4.6 | 15/15 | 78 successes | 171 fixtures, 3422 checks |
| Standalone, local Core | Windows x64, MSVC 19.44, Lua 5.1.5 | 15/15 | 78 successes | 171 fixtures, 3422 checks |
| Standalone, local Core | Windows x64, MSVC 19.44, Lua 5.3.6 | 15/15 | 78 successes | 171 fixtures, 3422 checks |
| Standalone, local Core | Windows x64, MSVC 19.44, Lua 5.4.8 | 15/15 | 78 successes | 171 fixtures, 3422 checks |

The original and migrated generators produced identical runner bytes on both
Windows and Linux. The source tests and examples remain unchanged. The workflow
passed actionlint 1.7.7 (without optional ShellCheck/pyflakes integrations), and
its matrix and setup commands were compared with the original workflow.

These results do not establish a hosted CI pass. GitHub-hosted dependency setup,
macOS, and Linux Lua 5.1/5.3 were not executed locally. The complete preserved
matrix still needs a hosted run after publication; no hosted result is claimed.

## Local LuaRocks and versioning validation (#4)

LuaRocks 3.11.1 installed both modules and matching version metadata into fresh
temporary trees with spaces in their paths. Loading took place outside the
checkout using only installed module paths; module identity, existing Core
version reporting (`0.7.1`), and the quick-start example passed in each run:

| Environment | Lua | Core selection | Result |
| --- | --- | --- | --- |
| Windows x64, MSVC 19.44 | 5.1.5 | Explicit local migration revision | Pass |
| Windows x64, MSVC 19.44 | 5.3.6 | Explicit local migration revision | Pass |
| Windows x64, MSVC 19.44 | 5.4.8 | Explicit local migration revision | Pass |
| Linux/WSL, GCC 13.3 | 5.4.6 | Default pinned remote fetch from a fresh source snapshot | Pass |

All used Core `d87dcf96b2707e817dd51163a9f9efa36651e929`. The Linux source snapshot
had no own Git identity and correctly installed `scm+unknown` metadata. The
Windows development checkout installed `scm+g<sha>.dirty` metadata.

The seven version tests passed on both Windows and Linux, including ignored
CI tool directories not marking a tagged release dirty. The existing suite
also passed on Windows Lua 5.4.8: 15/15 CTest tests, 78 Busted successes, and
171 Query fixtures / 3422 backend-window checks. actionlint, Python syntax,
local documentation links, and whitespace checks passed.

These are local results. Hosted CI, macOS, Linux Lua 5.1/5.3, Lua 5.2, and
LuaJIT were not run for the packaging validation above. The version tests
create tags only in disposable fixtures. The added manual release workflow
still requires a hosted quality-gate run; its first successful test will create
the real `v0.1.0` tag. No hosted gate or real tag creation is claimed by these
local results.

## Local release-gate validation (#4)

The instrumented CI runner was rerun on Windows Lua 5.4.8. The existing suite
passed with 15/15 CTest tests, 78 Busted successes, and 171 Query fixtures /
3422 checks; the generated Actions outputs and summary contained those counts.

Local API fixtures exercised successful complete evidence and rejected missing,
duplicate, stale, skipped, cancelled, and mismatched evidence, malformed inputs,
wrong branch/repository, mutable Core pins, existing annotated release tags,
and tag-creation API failures. Rejected evidence produced a failure summary
without requesting tag creation. The successful fixture requested only creation
of the exact tag reference. These fixtures made no real remote mutations.

The final workflows passed actionlint 1.7.7 without optional ShellCheck/pyflakes
integrations. Python syntax, local documentation links, and whitespace checks
also passed. These checks validate local control flow and reporting, not the
hosted reusable-workflow integration or real tag creation.
