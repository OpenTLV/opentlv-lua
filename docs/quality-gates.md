# Lua quality gates

Lua follows Core's PR, Main, Full and Release architecture. Lua owns the public
Lua API, conversion, userdata lifetime, callbacks, errors, garbage collection,
module loading and packaging. Core owns parser/writer behavior, native suites,
properties, fuzzing, ABI, benchmarks and Callgrind. No Lua gate starts Core CI
or enables its native tests.

## Entry points

| Entry point | Trigger | Required checks |
| --- | --- | --- |
| `lua-pr.yml` | PR to `main` | Linux Lua 5.4 Debug, all Lua tests and Query facade corpus, formatting, clang-tidy, cppcheck, Python tooling regressions, Lua syntax and CodeQL; informational Core API coverage |
| `lua-main.yml` | Push to `main` | One Linux Lua 5.4 build; module identity, version and public Reader smoke; documentation change evidence after the gate |
| `lua-full.yml` | Manual dispatch or Release call | Complete runtime/platform matrix, shared Core, LuaRocks, installed Core consumption, module packages, ASan/UBSan and all source checks; informational Core API coverage |
| `release.yml` | Manual dispatch from `main`, only `tag` | Closed changelog, dispatch SHA, locked authenticated Core release, fresh Full CI, checksummed and attested packages, Git tag and GitHub Release |

PR has no path filters or branch-push trigger. Full has no push or schedule
trigger. Main does not repeat the runtime matrix or source analysis. Standalone
Full never publishes. Historical PR results cannot authorize a release.

## Documentation pin notifications

After the Main quality gate succeeds, `docs_changes` compares the push commit
with the previous successful Main run. Changes in `docs/`, `examples/`, root
README, CONTRIBUTING or CHANGELOG files, or the Main notification infrastructure
produce a positive change record. Comparing against the previous success also
covers documentation changes from cancelled or failed pushes. The first run
without a successful baseline examines the complete tracked source tree.

Main uploads `docs-notification-<attempt>/notification.json` with the exact
repository, commit, run, attempt and change flag. The separate **Notify Lua
documentation portal** workflow runs only after successful Main push completion.
It validates that record and sends `component-docs-updated` to Docs only when
relevant files changed. Unrelated changes need no dispatch credential.

The notifier performs no checkout and executes no producer artifact code. Only
its final inline step receives `OPENTLV_DOCS_DISPATCH_TOKEN`, a fine-grained token
restricted to Docs with Contents write permission. Main uses its read-only
workflow token. Missing credentials or delivery errors fail visibly; rerun the
notifier while its exact-attempt artifact remains available.

Merge the Docs Lua `updates.mode: documentation` policy before enabling this
sender. Docs authenticates the successful Main run and main ancestry, then opens
or updates `automation/component-lua` with only Lua's source revision changed.
Duplicate or older notifications do not regress the pin. The proposed revision
still passes Docs PR checks. This flow does not start Docs Full, publish Pages,
or claim runtime compatibility with the portal's Core pin. Versioned release
documentation remains owned by the separate release notifier described in
[the release guide](releases.md).

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

## Core API coverage

`Core API coverage` reports how much of the pinned Core's public API the native
Lua module uses. It runs in PR and Full, not Main. The job is informational: it
uses `continue-on-error`, is not a `Lua quality gate` prerequisite and has no
threshold yet. The coverage epic (#25) tracks raising it. The PR and Full
timings record it as `checks / Core API coverage`.

[`scripts/api_coverage.py`](../scripts/api_coverage.py) is the binding-agnostic
script of the Python and Rust repositories, with `cmake/api-coverage.json` as
its default configuration. It fetches only the locked Core commit and reads
every `TLV_API` function and data declaration under `tlv/include`. A
declaration shape it does not recognize fails the report instead of being
skipped. A symbol counts as referenced when its identifier appears in `src`
outside comments; calls through Core macros are not counted. A referenced
symbol is named by the binding, which does not prove functional parity.
Coverage is reported for core and `tlv/builtins` separately, in total and per
header, with every unreferenced symbol listed. The Markdown summary goes to the
job log and job summary. `api-coverage.json` is uploaded as an attempt-scoped
artifact.

`src` is the only reference directory. Every Core call is in the native
`opentlv._core` module; `lua/opentlv/init.lua` only re-exports it, and tests and
examples are not part of the public API.

On a pull request, the job also reads the Core lock at the base commit and lists
the symbols added and removed between the two pins. Added symbols show whether
the module already references or ignores them. An unchanged pin is reported as
such.

[`cmake/api-coverage.json`](../cmake/api-coverage.json) is the configuration and
ignore list. It names the Core lock `cmake/core-artifact.json` and the reference
directory. Each ignore entry has a `reason` and matches header paths relative to
`tlv/include` (`headers`) or symbol names (`symbols`); both accept `fnmatch`
patterns. Ignored symbols are excluded from the counts, even when the module
references them. The current entries are:

- the endian, tag, value, size and copy primitives: Lua exposes tags and values
  as strings, so comparison, hashing, slicing and copies are string operations.
  The module's `tlv_size_to_native` conversion is an implementation detail.
- the diagnostic producers (`tlv_diagnostic_init`, setters, path builders,
  `tlv_location_set` and `tlv_location_resolve`) for C format and codec
  implementations. Lua exposes no API to build diagnostics; `src/error.c` uses
  `tlv_diagnostic_init` and `tlv_diagnostic_set_location` only to shape binding
  errors. Diagnostic consumers such as the `*_string` lookups and
  `tlv_diagnostic_path_string` remain counted.
- the decode, measure and encode callbacks (and `tlv_variable_is_constructed`)
  that `tlv_fixed_format_init()`, `tlv_fields_format_init()`,
  `tlv_tagged_fields_format_init()`, `tlv_tagged_binary_format_init()`,
  `tlv_escaped_format_init()` and `tlv_variable_format_init()` install in a
  descriptor. Core calls them through the descriptor, so Lua has no separate
  path; the initializers themselves remain counted.
- the Reader, Writer and Tree Writer wrappers that call their bound `_diag`
  counterpart with a NULL diagnostic (`tlv_read`, `tlv_reader_next`,
  `tlv_reader_visit`, `tlv_tree_reader_next`, `tlv_tree_reader_next_event`,
  `tlv_element_encoded_size`, `tlv_writer_write_element`,
  `tlv_writer_copy_encoded`, `tlv_writer_preserve`, `tlv_tree_writer_begin`,
  `tlv_tree_writer_end`, `tlv_tree_writer_write_element` and
  `tlv_tree_writer_write_event`). Lua always raises structured diagnostics.
- the Tag/Value write forms (`tlv_write`, `tlv_write_diag`, `tlv_writer_write`,
  `tlv_writer_write_diag`), which build an Element for the Element entry
  points that `Writer:write()` calls directly.
- `tlv_write_element` and `tlv_write_element_diag`, which encode one element
  into caller-owned storage. Lua strings are immutable, so there is no caller
  destination; `format:encode()` calls `tlv_format_encode()`, which they use.
- `tlv_writer_copy_element`, which is `tlv_writer_write_element()` without
  diagnostics after the same checks.
- `tlv_query_parse`, the NUL-terminated form of `tlv_query_parse_n()`. Lua
  strings carry their length, so every path argument parses through
  `tlv_query_parse_n()`, which also rejects embedded NUL.
- `tlv_schema_check`, which `tlv_schema_prepare()` runs before keeping the
  checked handle. Schemas are immutable snapshots, so `schema:check()` prepares
  once and a separate definition-only check adds no operation.

Patterns that match nothing in the pinned Core are reported, so stale entries
are visible after a pin bump. Change the ignore list only for symbols the
binding intentionally leaves unbound.

The first report, for Core `d87dcf96b270` (15 commits after 0.7.1), references
118 of 254 core symbols (46.5 %) with 43 ignored, and 61 of 291 builtins symbols
(21.0 %). Builtins-specific parity is outside the coverage epic. The report
reads the headers, not a build; official builds enable every Core component and
builtin.

Custom Format composition (#27) references or ignores every symbol of
`tlv/field/*.h` and `tlv/formats/*.h`, and references all of `tlv/format.h`
except `tlv_source_preserve()`, which belongs to source preservation in the
streaming Reader and Writer work (#28). Core coverage of the same pin is 147 of 235
symbols (62.6 %) with 62 ignored:

| Header | Referenced | Ignored | Lua API |
| --- | ---: | ---: | --- |
| `tlv/field/fixed.h`, `tlv/field/variable.h`, `tlv/field/escaped.h` | 10/10 | 0 | `formats.compose()` field adapters |
| `tlv/field/packed.h` | 2/2 | 0 | `fields.read_packed()`, `fields.write_packed()` |
| `tlv/format.h` | 7/8 | 0 | Format methods, `formats.packed()`, `constructed` |
| `tlv/formats/compose.h` | 3/3 | 12 | `formats.compose()` |
| `tlv/formats/escaped.h` | 1/1 | 3 | `formats.compose()` |
| `tlv/formats/fixed.h` | 1/1 | 0 | `formats.fixed()`, `formats.compose()` |
| `tlv/formats/packed.h` | 4/4 | 0 | `formats.packed()` |
| `tlv/formats/variable.h` | 2/2 | 4 | `formats.compose()` |

Streaming Reader, Writer and tree operations (#28) reference or ignore every
symbol of `tlv/reader/*.h` and `tlv/writer/*.h`, and `tlv_source_preserve()`
completes `tlv/format.h`. Core coverage of the same pin is 166 of 215 symbols
(77.2 %) with 82 ignored:

| Header | Referenced | Ignored | Lua API |
| --- | ---: | ---: | --- |
| `tlv/format.h` | 8/8 | 0 | Format methods, `source:preserve()` |
| `tlv/reader/reader.h` | 11/11 | 2 | `reader()`, `read()`, `read_source()`, incremental input, `next_source()` |
| `tlv/reader/tree.h` | 9/9 | 2 | `tree_reader()` |
| `tlv/reader/visitor.h` | 2/2 | 1 | `visit()`, `visit_tree()`, `reader:visit()`, `tree_reader:visit()` |
| `tlv/writer/writer.h` | 10/10 | 11 | `writer()`, `copy_encoded()`, `preserve()`, `encoded_size()` |
| `tlv/writer/tree.h` | 10/10 | 4 | `tree_writer()`, `write_event()`, `measure_tree()`, `measure_events()` |

Document, Query and Schema operations (#29) reference or ignore every symbol of
`tlv/document/document.h`, `tlv/query/*.h`, `tlv/schema/schema.h` and
`tlv/schema/constraint.h`. Core coverage of the same pin is 198 of 213 symbols
(93.0 %) with 84 ignored:

| Header | Referenced | Ignored | Lua API |
| --- | ---: | ---: | --- |
| `tlv/document/document.h` | 41/41 | 0 | `document()`, `revision()`, `find_child()`, `visit()`, Node identity checks, `source_location()`, `document_builder()`, Document Query results |
| `tlv/query/query.h` | 9/9 | 1 | `query()`, `format()`, `visit()`, matcher `visit()` |
| `tlv/query/program.h` | 31/31 | 0 | `query_program()`, `reset()`, `cause_name` |
| `tlv/query/plan.h` | 2/2 | 0 | `query_plan_open()`, `plan_info()` |
| `tlv/query/adapters.h` | 3/3 | 0 | codec providers |
| `tlv/schema/schema.h` | 9/9 | 1 | `schema:check()`, `first`, `length_schema()`, `cause` |
| `tlv/schema/constraint.h` | 3/3 | 0 | `value_constraint()` |

`tlv/schema/number.h` and the Value codec headers belong to the codec work
(#30); `tlv/definition.h`, `tlv_diagnostic_path_string` and
`tlv_location_translate` remain for the final coverage review (#31).

Run it locally from the repository root:

```sh
python -B scripts/api_coverage.py
python -B scripts/api_coverage.py --core ../opentlv --commit HEAD --base-commit <old-sha>
python -B scripts/api_coverage.py --base origin/main --output build/api-coverage.json
```

Without `--core`, the script caches the shallow Core fetch in
`build/api-coverage/core.git`. `--core` reads an existing Core checkout instead;
the requested commits must already be there. `--commit` measures a different
Core revision than the lock. `--base` reads the lock at a binding revision, and
`--base-commit` names a previous Core revision directly. The parsing, ignore,
pin-diff and workflow logic is covered by `tests/test_api_coverage.py`.

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
