# Lua release quality gates

Lua releases use independent `vMAJOR.MINOR.PATCH` tags, starting with
`v0.1.0`. The manual workflow in [release.yml](../.github/workflows/release.yml)
creates a tag only after the exact candidate passes the full Lua test matrix
and the final job verifies the results. It runs only when manually dispatched.

## Start a gate

Merge the intended Lua changes, including the release workflow and changelog,
into `main`. In GitHub Actions, select **Lua release gate**, choose **Run
workflow**, and select branch **main**. Provide:

| Input | Value |
| --- | --- |
| `version` | Numeric version without a `v` prefix or leading zeros, initially `0.1.0` |
| `commit` | Full 40-character lowercase Lua commit SHA to test and tag |

The workflow converts the numeric `version` input into a validated `v`-prefixed
Git tag: input `0.1.0` creates `v0.1.0`.

The candidate must belong to `main` and be the dispatched workflow revision or
one of its ancestors. Use the complete immutable commit SHA, not a branch,
abbreviated SHA, or tag. The version tag must not already exist, and the
candidate must not already carry another `vMAJOR.MINOR.PATCH` release tag. The
workflow verifies the canonical Core source URL and the single immutable SHA pinned in
the candidate's `cmake/Core.cmake`; Core's release version does not determine
the Lua version.

The first hosted quality-gate test is intended to create the real `v0.1.0` tag.
Running local validation or merging this workflow does not create that tag.
Before that first run, merge and validate the `v`-prefix correction, dispatch
from the updated `main`, and select a candidate that includes the correction.

## What the gate runs

The release workflow calls the same [Lua workflow](../.github/workflows/lua.yml)
used for ordinary binding CI. It checks out the exact requested candidate and
tests all nine combinations:

| Operating systems | Lua versions |
| --- | --- |
| Ubuntu 24.04, Windows latest, macOS latest | 5.1, 5.3, 5.4 |

Each job builds the standalone binding with the pinned Core, executes the
existing CTest suite including Busted and examples, and runs the Lua Query
facade corpus against that same Core checkout. It also runs versioning checks
and the isolated LuaRocks build/install/loading checks. See [tests and CI](testing.md)
for the preserved coverage and setup. Core's own complete test suite remains
owned by Core and is not part of this gate.

Preparation and tests have read-only repository permissions. Only the final
tag job receives `contents: write`. That job verifies the current Actions run
and run attempt through the GitHub API: exactly nine expected matrix jobs must
be completed successfully, their required steps must succeed, and their
evidence must identify the requested Lua SHA and pinned Core SHA. Missing,
skipped, cancelled, failed, duplicated, or mismatched evidence prevents tagging.

## Results and tag creation

Matrix summaries record the selected Lua and Core commits, platform/runtime,
results, and actual CTest, Busted, Query fixture, and Query check counts. The
final Actions summary collects the verified matrix results and tag outcome.
Counts come from completed test output, so a successful job alone is not a
substitute for test evidence. Inspect the summary and individual job logs when
reviewing a gate.

After verification, the final job creates `refs/tags/v<version>` at the tested
Lua commit. It rechecks that the tag does not exist and creates the reference
without replacing an existing one; a concurrent attempt that creates the same
tag causes creation to fail. Existing tags are never moved.

The workflow creates only the Git tag. It does not upload release artifacts,
create a GitHub Release, or publish a LuaRocks package. Gate results remain in
the Actions summary and logs. See [versioning](versioning.md) for the distinction
between the Lua tag, development metadata, and a future versioned rockspec.

## Failed gates and reruns

A failed gate leaves tag creation blocked. Fix the failure before retrying.
Use **Re-run all jobs** for a retry of the same workflow revision: the final
job accepts only evidence from its current run attempt. Re-running only failed
jobs or only the tag job leaves successful jobs from an older attempt without
current-attempt evidence and is rejected.

If release workflow or validation logic changes, merge the fix and start a new
manual dispatch from the updated `main`. Re-running the old run continues to
use its original workflow revision. If a successful gate already created the
tag, do not rerun it to replace that tag; choose the next independent Lua
version for a later release.
