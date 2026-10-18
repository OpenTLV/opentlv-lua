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

The existing Lua tests and examples retain their original contents. Their
inventory is recorded in [migration provenance](migration.md). The Query
generator changes only how it locates the shared Core inputs; its Lua runner,
fixture conversion, and assertions are preserved.

## Workflow matrix

`.github/workflows/lua.yml` preserves the original event-dependent matrix:

| Events | Operating systems | Lua versions | Jobs |
| --- | --- | --- | ---: |
| Main branch pushes and relevant pull requests | Ubuntu 24.04, Windows latest | 5.1, 5.3, 5.4 | 6 |
| Manual dispatch, `v`-prefixed version tags, and release workflow calls | Ubuntu 24.04, Windows latest, macOS latest | 5.1, 5.3, 5.4 | 9 |

Lua 5.2 and LuaJIT remain supported runtimes but were not in the original CI
matrix; this migration does not add matrix entries. The existing Lua action,
LuaRocks setup, Windows shared Lua/MSVC configuration, dependency-install retry
behavior, and read-only test permissions are preserved. Release workflow calls
use the candidate SHA and full matrix; they keep their concurrency group
separate from ordinary branch and tag CI.

LuaRocks installs Busted and its dependencies for the selected Lua runtime.
The workflow tests the standalone CMake build and separately builds and
installs the rock into a fresh temporary tree with `luarocks make`.

## CI execution

After Lua and Busted setup, both platform paths invoke:

```sh
python -B scripts/ci.py --lua-prefix .lua --build-dir build/ci
```

The runner:

1. Requires the selected Lua interpreter, headers, and a Busted launcher.
2. Configures CMake with package discovery disabled and the remote pin from
   `cmake/Core.cmake`; no sibling checkout is selected implicitly.
3. Records the resolved Core SHA and checks that the full Busted suite is
   registered, so missing prerequisites cannot produce a silently reduced CI run.
4. Builds `opentlv_lua`, including the existing native allocator helper, and runs
   the existing CTest suite with verbose results and failure propagation.
5. Generates and executes the existing Query facade runner using `corpus.json`
   and `reference.py` from that same Core checkout.

CTest includes Busted, standalone Query/Document, codec, schema, writer,
visitor, NFC and component tests, allocator failure checks, and all four
existing examples. The additional Query corpus execution retains the original
optimized/unoptimized, retained/streaming, input-window and Document checks.
The migrated suite's assertions and expected results remain unchanged.

After the existing suites, CI runs both Python regression suites with
`python -B -m unittest discover -s tests -p "test_*.py"`.
`tests/test_version.py` uses disposable Git repositories to check clean release tags, development
commits, tracked/untracked changes, ambiguous tags, and source archives both
inside and outside an enclosing repository.

`tests/test_release.py` uses local GitHub API fixtures
to check release input and tag validation, exact commit identities, full matrix
evidence, and rejection paths before tag creation. These tests do not contact
GitHub or create release tags in this repository. Run the combined command
locally, or select `python -B tests/test_version.py` or
`python -B tests/test_release.py`, without building the Lua module.

CI then invokes:

```sh
python -B scripts/check_luarocks.py --lua .lua/bin/lua \
    --core-source build/ci/_deps/opentlv_core-src
```

Windows uses `.lua/bin/lua.exe`. The packaging check lints the rockspec,
preconfigures `build/luarocks` with the same Core source tested above, and runs
`luarocks make` from the repository root. It verifies both installed module
files and matching version metadata. Lua runs from outside the checkout, with
only that tree's module paths and no inherited Lua initialization settings,
then checks module identity, preserved Core version reporting, and the existing
quick-start example. The temporary tree includes spaces in its path and is
removed when the check exits. No global rock installation is changed.

For release workflow calls, the runner also verifies the requested Lua commit
and canonical Core repository/pinned SHA. It records the actual completed
CTest tests, Busted successes, Query fixtures, and Query checks. Successful
evidence steps make these identities and counts available in the current
Actions run; each matrix job writes a summary. The final release job verifies
all nine jobs and required steps through the GitHub Actions API before creating
a tag. It accepts evidence only from the current run attempt. See
[release quality gates](releases.md) for the dispatch procedure and final report.

Omit `--core-source` to exercise the rockspec's default Core selection with a
fresh `build/luarocks` directory. LuaRocks must be configured for the interpreter
passed to `--lua`; `--luarocks` can select an explicit executable.

On Windows the workflow runs dependency installation and the CI runner from
PowerShell so Git Bash's `link.exe` cannot shadow the MSVC linker. Python
subprocesses propagate CMake, CTest, generator and Lua failures to the job.

## Local runs

For a complete CI-equivalent test run, use a Lua installation with matching
headers and Busted already installed through LuaRocks:

```sh
python -B scripts/ci.py --lua-prefix /absolute/path/to/lua \
    --build-dir build/ci-local --core-source /absolute/path/to/opentlv
```

The explicit Core override supports development and baseline comparison; the
printed SHA identifies the selected checkout. Omit it to exercise the pinned
remote fetch. Use a separate build directory for each Lua ABI and toolchain.
`--lua-include`, `--lua-executable`, and `--busted` can select nonstandard layouts,
for example system Lua headers in `/usr/include/lua5.4`.

For ordinary local CMake runs, including reduced component configurations, use
the commands in [development instructions](development.md). The full CI runner
intentionally rejects a configuration where the Busted suite is skipped; the
existing CTest component-aware tests still support reduced local builds.

To run just the existing Query corpus against an already-built module:

```sh
python -B tests/query/lua_facade.py --core-source /absolute/path/to/opentlv \
    --output build/lua-query-conformance.lua
LUA_PATH='lua/?.lua;lua/?/init.lua;;' LUA_CPATH='build/local/?.so;;' \
    lua build/lua-query-conformance.lua
```

Select the same Core revision used to build the module. An installed Core
package does not contain the shared test corpus; supply the corresponding
source checkout explicitly. On Windows use `.dll` in `LUA_CPATH` and the matching
Lua executable. `-B` prevents Python bytecode files from being written into
either repository.

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
