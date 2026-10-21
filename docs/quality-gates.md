# Lua quality gates

Lua follows Core's PR, Main, Full and Release architecture. Lua owns the public
Lua API, conversion, userdata lifetime, callbacks, errors, garbage collection,
module loading and packaging. Core owns parser/writer behavior, native suites,
properties, fuzzing, ABI, benchmarks and Callgrind. No Lua gate starts Core CI
or enables its native tests.

## Entry points

| Entry point | Trigger | Required checks |
| --- | --- | --- |
| `lua-pr.yml` | PR to `main` | Linux Lua 5.4 Debug, all Lua tests and Query facade corpus, formatting, clang-tidy, cppcheck, Python tooling regressions, Lua syntax and CodeQL |
| `lua-main.yml` | Push to `main` | One Linux Lua 5.4 build; module identity, version and public Reader smoke |
| `lua-full.yml` | Manual dispatch or Release call | Complete runtime/platform matrix, shared Core, LuaRocks, installed Core consumption, module packages, ASan/UBSan and all source checks |
| `release.yml` | Manual dispatch from `main`, only `tag` | Closed changelog, dispatch SHA, locked authenticated Core release, fresh Full CI, checksummed and attested packages, Git tag and GitHub Release |

PR has no path filters or branch-push trigger. Full has no push or schedule
trigger. Main does not repeat the runtime matrix or source analysis. Standalone
Full never publishes. Historical PR results cannot authorize a release.

The Full matrix has 17 configurations:

- Ubuntu 24.04, Windows latest and macOS latest, each with Lua 5.1, 5.2, 5.3
  and 5.4, static Core, Release (12).
- Linux LuaJIT 2.1, static Core, Release (1). LuaJIT on Windows/macOS is not
  verified by this matrix; this is a support-validation boundary.
- Lua 5.4 with shared Core on all three platforms (3).
- Linux Clang 18, Lua 5.4, static Core, Debug with ASan/UBSan and leak detection
  (1). The compiler's ASan runtime is preloaded into the interpreter process.

All configurations require the complete Lua CTest/Busted suite and shared Query
corpus through the public Lua facade. Non-sanitizer configurations also install
and load a fresh LuaRocks tree, using static Core from the same source identity.
Two static Lua 5.4 configurations (Linux/Windows x64) additionally test an
installed Core package, reject missing/conflicting explicit package selections,
and validate a relocated module archive. macOS and other Lua ABIs are tested,
but have no official binary archive in this initial package policy.

The LuaJIT job runs only the LuaRocks package manager with PUC Lua 5.1 because
the public package manifest exceeds LuaJIT's per-function constant limit.
LuaRocks remains configured for LuaJIT headers and executable; Busted, the
complete Lua suite, Query corpus and installed rock still run with LuaJIT.

The always-running aggregate gate requires every named job and configuration.
Missing, duplicate, cancelled, failed or unexpectedly skipped evidence fails.
Records include repository, exact Lua/Core SHA, run, attempt, interpreter,
compiler, configuration and test counts. Gate evidence hashes workflow, source,
test and build helpers. Publication rechecks those definitions and the same
run/attempt; successful jobs from an earlier attempt are insufficient.

## Core verification

`cmake/Core.cmake` and `cmake/core-artifact.json` must agree on one immutable
Core SHA. Ordinary pinned Git and local source builds remain available and are
explicitly unverified. An installed package selected by a developer is not
automatically authenticated by CMake.

The verified resolver authenticates the Core schema-2 manifest and SHA256SUMS
against `OpenTLV/opentlv`, `core-release.yml` and the exact pinned commit. It
checks the release tag, producer attestations, Full CI identity and archive
digests. Release preflight locks both the release ID and authenticated manifest
digest; replacing either blocks publication.

A binary is preferred only when OS, architecture, compiler, configuration,
linkage and libc/MSVC runtime match and its manifest declares every feature in
`cmake/core-features.json` enabled. Missing or disabled features make a binary
incompatible; the existing verified-source fallback policy then applies.
CMake independently validates the selected package's public `tlv/config.h`.
Static non-Windows libraries also require
an explicit PIC declaration. The initial Core producer does not declare it,
so these module builds use Verified Source. macOS and sanitizer builds likewise
need source. Source fallback requires `allow_verified_source: true`; an
authentication or checksum failure never falls back to Git or another artifact.

Binary consumption also authenticates the matching source archive for the
Core-owned Query corpus/reference and LuaRocks check. The source archive's
embedded version/SHA must match. GitHub's automatic source downloads are not
substitutes for the attested named source asset.

As inspected on 2026-10-19, Core release `0.7.1` has no uploaded assets, and the
Lua migration pin predates the new producer. The artifact lock deliberately has
no release tag. Development CI works, but release preflight fails until a
reviewed matching Core release/SHA is selected in both pin files. Resolver
fixtures are not evidence of actual attestation consumption.

## Module artifacts

Official archives target Linux x64 and Windows x64, Lua 5.4, Release, static
Core. They require the consumer's matching Lua interpreter; Windows requires
its shared Lua runtime. They do not promise compatibility with arbitrary Lua
builds, toolchains, libc or operating system versions.

Each archive includes `opentlv/_core`, `opentlv/init.lua`, binding version
metadata, compiler/dependency identity and original Lua/Core licenses. After
extraction into a separate temporary tree, the runner loads only its module
paths and runs eight Lua test scripts, including public version/metadata
consistency, plus the complete Query facade corpus.
Native allocator injection remains in the build suite; the helper is not
distributed. Shared Core configurations are tested separately.

The GitHub Release contains both archives, `release-manifest.json` and
`SHA256SUMS`. Every file is attested. Publication verifies attestations again
before creating the tag. LuaRocks publication and a versioned release rockspec
remain separate work; the development rockspec stays `scm-1`.

## Timings and acceptance

[Baseline evidence](ci-baseline.json) records actual job timestamps and URLs
for the preceding successful PR, main push and tag-only release. Each new entry
point uploads attempt-scoped timings even when checks fail. The collector
retries delayed API metadata. Job seconds are elapsed runner time, not billed
minutes; execution span includes scheduling gaps but excludes initial queue time.

Compare matching event, runtime matrix, runner, toolchain and cache cohorts.
The previous tag-only release is narrower than the new artifact gate. Report
PR latency separately from Main, Full and Release cost. Summarize the baseline:

```sh
python -B scripts/ci_timings.py --baseline docs/ci-baseline.json --output build/baseline-summary.json
```

Hosted PR/Main/Full validation, actual Core artifact consumption, OIDC signing,
a controlled release and comparable after-change timings remain acceptance
requirements. Local tests cannot establish those results. Configure required
merge checks using `Lua quality gate` after its hosted check name exists; do not
require Full-only jobs. Tag rules and publisher permissions are repository
settings; workflow code does not prevent administrator policy changes.

## Local checks

```sh
python -m pip install -r requirements-dev.txt
python -B -m unittest discover -s tests -p 'test_*.py'
pre-commit run --all-files
actionlint
python -B scripts/ci.py --lua-prefix /path/to/lua --runtime 5.4 --rocks
```

Use `--smoke` for Main, `--shared ON` for shared Core, or
`--configuration Debug --sanitizer` with `CC=clang-18` on Linux. Explicit
`--core-source` is local development only and is forbidden for release builds.
Compiler/interpreter setup and ownership details are in [testing](testing.md).
