# Lua releases

Run **Lua Release Gate** (`release.yml`) manually from `main`. Its only input
is **tag**, for example `v0.2.0`. Do not create the tag beforehand. The workflow
selects the exact dispatch SHA, even if main advances during verification.

## Preparation

1. Select an authenticated Core release produced by Core's current artifact
   workflow. Set its exact SHA in `cmake/Core.cmake` and the matching release/SHA
   in `cmake/core-artifact.json`. Verify the full Lua matrix after changing Core.
2. Close the changelog and merge the candidate to `main`. The first release
   heading must match the tag's version without `v`: for tag `v0.3.0`, use
   `## [0.3.0] - 2026-10-29`, followed by at least one change.
   No `Unreleased` occurrence is allowed. Keep the usual
   Unreleased section during development and close it only for release.
3. Dispatch the gate from `main` with that tag. No alternate SHA or
   skip-verification input is available. Existing tags, including another
   version tag on the candidate, are rejected.

GitHub Release display titles use `OpenTLV Lua X.Y.Z`, for example
`OpenTLV Lua 0.3.0`. The publisher takes this version from the validated
release request; Git tags retain `vX.Y.Z` and LuaRocks retains `X.Y.Z-1`.
Review the [Lua release-description preview and template](release-notes.md)
before dispatch; the body is generated from the approved changelog and verified
release evidence, with issue links in the release body.
Changes to an unreleased candidate must be reviewed and revalidated before
dispatch. An approved or running release continues to use its locked commit
and release scripts.

## Published Core v0.8.0 baseline (#40)

Lua **v0.2.0** was [published on 2026-10-27](https://github.com/OpenTLV/opentlv-lua/releases/tag/v0.2.0),
independently versioned from Core. The minor increment covers the public streaming,
Format composition, Document, Query, Schema, codec and Generator APIs added
since that baseline, together with the release and licensing work recorded in
the changelog.

Both dependency pins select [Core v0.8.0](https://github.com/OpenTLV/opentlv/releases/tag/v0.8.0)
at `345c77acb29266036df5f27d04ce315bb61853cc` (Core release ID `425487758`).
The Core release supplies a manifest, checksums and named source/binary archives;
the resolver must authenticate them at execution time under the
[Core verification policy](quality-gates.md#core-verification).
Original migration records continue to identify the initial imported sources.

The changelog section for this release is `## [0.2.0] - 2026-10-27`. Its GitHub Release
contains Linux/Windows Lua 5.4 module archives, `release-manifest.json` and
`SHA256SUMS`, but no release rockspec or source rock. Do not redispatch this
existing tag for LuaRocks; it predates the required packaging changes.

Local preparation on 2026-10-27 verified the Core manifest, checksums and source
archive against their downloaded attestation bundles, requiring the Core
release workflow and exact source commit. Linux GCC 13.3 / Lua 5.4.6 passed
28/28 CTest entries, 165 Busted tests, 171 Query fixtures / 3422 checks,
installed-Core smoke and extracted development-package checks. The 91 Python
tests, API completeness and pre-commit checks also passed.

A prepared pin update or a passing local suite is not a published release.
LuaRocks upload is outside this candidate's publication requirements.

## First LuaRocks release (#46)

The next Lua release candidate is **v0.3.0**, with rock version **0.3.0-1**.
The candidate changelog is closed as `## [0.3.0] - 2026-10-29`. Core
remains v0.8.0 at the pin above. Neither a prepared candidate nor these
instructions authorize GitHub or LuaRocks publication.

On 2026-10-29, `main` at `2fece68b7b2286656b6f080694f931d3e1730cd6`
contained the merged packaging and publication changes (#43–#45).
[Main CI passed](https://github.com/OpenTLV/opentlv-lua/actions/runs/40856570297)
on that commit. The latest
[Full CI success](https://github.com/OpenTLV/opentlv-lua/actions/runs/40855547302)
was on the earlier `f59ee3fc05eecea03f69e2200fee0eee05a43dc8` commit;
it does not replace the release gate's Full run on the final candidate.
Only v0.1.0 and v0.2.0 existed, and none of the Lua 5.1–5.4 root manifests
listed `opentlv`. These are dated observations, not a namespace reservation.

Before dispatch, confirm the [environment and secret setup](#one-time-setup)
in the live repository settings, recheck tag and namespace availability,
confirm the changelog date matches the actual release date, and merge the
reviewed candidate to `main`. Record its final SHA. The GitHub
release gate publishes a tag and release, so obtain explicit approval before
dispatching it. LuaRocks then requires its separate environment approval.

Keep [#46](https://github.com/OpenTLV/opentlv-lua/issues/46) open until the
[external consumer verification](#external-consumer-verification) passes
against the public release and its evidence is recorded. Passing preparation
checks alone does not meet that issue's acceptance criteria.

## Verification and publication

Preflight locks the dispatch commit, Core release ID and authenticated manifest
digest. Release calls Full CI in the same run: all 17 configurations, LuaRocks,
sanitizers, static checks, CodeQL and Core API completeness are required. Candidate workers create a
private local tag so packaged metadata contains the intended release version;
no remote tag exists yet. `opentlv.version()` and `opentlv._VERSION` report the
same binding version as the packaged `lua-version.json`, without the tag's `v`.

Always-running aggregate validation rejects missing, failed, cancelled,
duplicate or skipped required evidence. The assembler revalidates the original
request and dependency lock, actual test/package records and exact run attempt.
It creates two tested Lua 5.4 module archives, the release rockspec and source
rock, a manifest, the deterministic `release-notes.md` and SHA256SUMS, then
attests every file. It rebuilds
`opentlv-<version>-1.rockspec` and `opentlv-<version>-1.src.rock` from the
release commit and requires the digests that every static-Core configuration
recorded when it installed them. The publisher verifies identities, digests, attestations and
successful same-attempt jobs again before creating the tag and GitHub Release.

Rerun **all jobs** when retrying: successful artifacts from an older attempt do
not authorize publication. A service failure after tag creation can leave a tag
without a complete release; inspect and recover manually. The workflow never
overwrites an existing tag or release. A fresh dispatch rejects that tag.

The publishing token does not trigger new workflows from its tag push.
`opentlv-scm-1.rockspec` remains the development package. The attached
[release rockspec](versioning.md#luarocks-release-rockspec) and source rock are
immutable release assets; uploading them to LuaRocks is a separately approved
step. Tags before the first release containing them (v0.1.0, v0.2.0) cannot be
packaged for LuaRocks.

## LuaRocks publication

**Lua LuaRocks Publication** (`luarocks-release.yml`) uploads the rock of an
already published GitHub Release to luarocks.org. Dispatch it manually from
`main` with the release **tag**. It never runs automatically, never builds a
new rock and never replaces a LuaRocks version.

### One-time setup

A repository administrator configures:

1. A GitHub environment named `luarocks` with required reviewers, deployment
   restricted to the `main` branch, and preferably self-review prevented. The
   reviewers' approval is the explicit approval of each upload.
2. The environment secret `LUAROCKS_API_KEY`: an API key of the LuaRocks
   account `marekcingel`, created in its luarocks.org settings. Store it only in
   that environment, never as a repository or organization secret, so no other
   workflow or job can read it. LuaRocks reported a security incident in
   September 2026; use a key created afterwards, and revoke and replace it if it
   may have been exposed.

The workflow fails closed if the environment, the approval or the key is
missing. Its file is protected by the same review as other workflows.

### Procedure

1. Complete the [release](#verification-and-publication) and confirm the GitHub
   Release lists `opentlv-<version>-1.rockspec` and
   `opentlv-<version>-1.src.rock`.
2. Dispatch **Lua LuaRocks Publication** from `main` with `tag=v<version>`.
3. `Verify Lua release rock` runs without the key. It requires:
   - a published, non-draft GitHub Release whose tag resolves to the manifest's commit;
   - exactly the files listed in `SHA256SUMS`, with matching digests;
   - attestations of every asset by `release.yml` for that commit;
   - `release-manifest.json` describing that tag and commit, with every Full
     record passed in the release run and attempt, and release rock evidence from
     every static-Core LuaRocks configuration matching the attached rock;
   - successful `Lua quality gate`, assembly and publication jobs in that same
     attempt;
   - the source rock and rockspec rebuilt from the tag being byte-identical.
4. Review the verification log and the uploaded `luarocks-plan-<attempt>`, then
   approve the `luarocks` deployment. Rejecting it ends the run without upload.
5. `Publish Lua rock to LuaRocks` repeats the verification and uploads exactly
   the attested files. It then reads both files back from
   `https://luarocks.org/manifests/marekcingel/` and records
   `luarocks-receipt-<attempt>`.

The publisher calls the luarocks.org upload API that `luarocks upload` uses.
The key is part of each API URL, so it is removed from the environment of child
processes, never passed as an argument and never printed; failures report only
the API method and HTTP status. The upload never uses `--force`:

- if LuaRocks already has that rockspec version with different bytes, the run
  fails;
- if it has identical files, the run succeeds as `already-published`;
- after an interrupted upload, it adds only the missing source rock to the
  existing version;
- before the first upload, it fails if another account's `opentlv` appears in
  the root manifests;
- it fails if LuaRocks does not add the new module to the root manifest.

A version on LuaRocks cannot be corrected in place: to republish the same tag,
release a new packaging revision (`-2`) instead. Clean consumer installation from
LuaRocks is verified separately.

### External consumer verification

Run this after publication on a clean Linux machine with Lua 5.4 and its
development headers, LuaRocks configured for that interpreter, CMake 3.24+,
a C99 compiler, Git, Python 3, curl, unzip and GitHub CLI. This independent
consumer check supplements the supported-platform Full matrix; it does not
claim a new Windows/macOS test. Use a fresh shell without Lua search-path,
Lua initialization, CMake or Core overrides and without the upload key.
The commands below use the v0.3.0 candidate; substitute the approved version
consistently if it changes before publication.

1. Download and authenticate the actual release outside any source checkout:

   ```sh
   set -eu
   tag=v0.3.0
   rock_version=0.3.0-1
   work=$(mktemp -d)
   cd "$work"
   mkdir release registry
   gh release download "$tag" --repo OpenTLV/opentlv-lua --dir release
   lua_sha=$(gh api "repos/OpenTLV/opentlv-lua/commits/$tag" --jq .sha)
   (cd release && sha256sum --check SHA256SUMS)
   for asset in release/*; do
       gh attestation verify "$asset" --repo OpenTLV/opentlv-lua \
           --signer-workflow OpenTLV/opentlv-lua/.github/workflows/release.yml \
           --source-digest "$lua_sha"
   done
   ```

   Check that `release-manifest.json` and the publication receipt identify
   this same tag, Lua SHA, Core pin, GitHub release ID and successful release
   run/attempt. Retain the approved publisher's plan and receipt artifacts.

2. Compare the public registry bytes with the authenticated release assets:

   ```sh
   for suffix in rockspec src.rock; do
       name="opentlv-$rock_version.$suffix"
       curl --fail --location "https://luarocks.org/manifests/marekcingel/$name" \
           --output "registry/$name"
       cmp "release/$name" "registry/$name"
   done
   luarocks --tree="$work/tree" --only-server=https://luarocks.org \
       install opentlv "$rock_version"
   ```

   Save the install log showing public server resolution and a successful
   native build. Do not install from the local files or add a Core override.
   Compare the installed rockspec with `registry/opentlv-<version>-1.rockspec`.
   Confirm `doc/LICENSE` and `doc/LICENSE-Core` match the Lua release and pinned
   Core licenses, and `lua-version.json` has the approved binding version and
   tag, an empty `binding_revision`, and `dirty: false`.

3. Load only the new tree and run the API, Query and quick-start scripts from
   the authenticated source rock:

   ```sh
   unzip -q "registry/opentlv-$rock_version.src.rock" -d source
   export LUA_PATH="$work/tree/share/lua/5.4/?/init.lua"
   export LUA_CPATH="$work/tree/lib/lua/5.4/?.so"
   export OPENTLV_EXPECTED_VERSION="${tag#v}"
   lua -e 'local m = require("opentlv"); assert(m == require("opentlv._core"));
       assert(m.version() == os.getenv("OPENTLV_EXPECTED_VERSION"));
       assert(m._VERSION == m.version())'
   metadata="$work/tree/lib/luarocks/rocks-5.4/opentlv/$rock_version/lua-version.json"
   lua source/opentlv-lua/tests/version.lua "$metadata"
   for test in smoke codec_spec query_document_spec schema_spec writer_spec \
       stream_spec visitor_spec program_spec document_query_schema_spec \
       codec_generator_spec definition_path_spec; do
       lua "source/opentlv-lua/tests/$test.lua"
   done
   lua source/opentlv-lua/examples/quick_start.lua
   ```

   There are no default-path suffixes in these search paths, and the working
   directory is outside the unpacked sources. `require` must load the installed
   facade and native module. Keep the temporary directory and logs as evidence.

4. Check the public module/version page, its README and installation command,
   and the Docs portal's Lua version and examples. After publication succeeds,
   replace the README's pending-publication statement with the verified version,
   module URL and registry installation command. Track Docs notification and
   actual portal publication separately.

Record the registry URL, release/tag/SHA, both file digests, release and
publication run URLs/attempts, environment approval evidence, consumer OS/Lua/
LuaRocks/compiler versions, install and test logs, and documentation URLs in
#46. Record failures and pending checks explicitly; do not mark the issue or
epic complete until the actual public installation and documentation pass.

## Automatic release documentation

After GitHub confirms the release is published, the gate records its release ID,
tag, exact Lua/workflow commits and run/attempt in `release.json`, uploaded as
`component-release-<attempt>`. `Notify Lua release documentation` runs after the
successful gate, verifies that publication succeeded in that attempt, then
notifies Docs with `component-release-published`. It performs no checkout and
executes no artifact code with the cross-repository credential.

Merge the Docs receiver/configuration first. Configure
`OPENTLV_DOCS_DISPATCH_TOKEN` with Contents write permission restricted to
`OpenTLV/opentlv-docs`. Docs uses `OPENTLV_COMPONENT_READ_TOKEN` with Contents and
Actions read access to the configured component repositories. It independently
checks the trusted producer, attempt-scoped artifact digest, published release
ID and tag/commit before publishing `lua/<version>/`, its version catalog and
stable alias. Core/other component snapshots and development pins are preserved.

Retry a failed notifier while its producer receipt is retained. Duplicate
delivery reuses the immutable snapshot; old releases cannot move stable backwards.
After receipt expiry, a newly verified producer receipt is required. Partial
tag/release failures still require the manual recovery described above.
Local mocked tests do not establish hosted notification or Pages deployment.

The [quality gate guide](quality-gates.md) documents package compatibility,
ownership, source fallback, CI timings and remaining hosted acceptance. Local
fixtures cannot verify GitHub OIDC permissions or prove production publication.
