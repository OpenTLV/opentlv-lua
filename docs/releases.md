# Lua releases

Run **Lua Release Gate** (`release.yml`) manually from `main`. Its only input
is **tag**, for example `v0.2.0`. Do not create the tag beforehand. The workflow
selects the exact dispatch SHA, even if main advances during verification.

## Preparation

1. Select an authenticated Core release produced by Core's current artifact
   workflow. Set its exact SHA in `cmake/Core.cmake` and the matching release/SHA
   in `cmake/core-artifact.json`. Verify the full Lua matrix after changing Core.
2. Close the changelog and merge the candidate to `main`. The first release
   heading must match the tag, for example `## [v0.2.0] - 2026-10-19`, followed
   by at least one change. No `Unreleased` occurrence is allowed. Keep the usual
   Unreleased section during development and close it only for release.
3. Dispatch the gate from `main` with that tag. No alternate SHA or
   skip-verification input is available. Existing tags, including another
   version tag on the candidate, are rejected.

## Core v0.8.0 release candidate (#40)

The next planned Lua release is **v0.2.0**, independently versioned from Core.
On 2026-10-27 the only existing Lua tag was v0.1.0; recheck tag availability
immediately before dispatch. The minor increment covers the public streaming,
Format composition, Document, Query, Schema, codec and Generator APIs added
since that baseline, together with the release and licensing work recorded in
the changelog.

Both dependency pins select [Core v0.8.0](https://github.com/OpenTLV/opentlv/releases/tag/v0.8.0)
at `345c77acb29266036df5f27d04ce315bb61853cc` (Core release ID `425487758`).
The Core release supplies a manifest, checksums and named source/binary archives;
the resolver must authenticate them at execution time under the
[Core verification policy](quality-gates.md#core-verification).
Original migration records continue to identify the initial imported sources.

The candidate changelog is closed as `## [v0.2.0] - 2026-10-27`.
Before publication:

1. Confirm the changelog date matches the actual release date, merge the
   candidate and record its exact Lua SHA.
2. Verify Main and Full on that SHA, including Core API completeness. Earlier
   runs against the migration pin do not establish Core v0.8.0 compatibility.
3. Dispatch `release.yml` on `main` with `tag=v0.2.0`. Its own Full run and
   same-attempt evidence still govern publication.
4. Verify the published tag resolves to the candidate, the release notes, both
   `OpenTLV-Lua-0.2.0-{linux,windows}-x64-lua54.zip` archives,
   `release-manifest.json`, `SHA256SUMS` and producer attestations. Inspect the
   release receipt and Docs notification separately.
5. Link the published release and gate evidence in
   [Lua #40](https://github.com/OpenTLV/opentlv-lua/issues/40) and
   [Core epic #718](https://github.com/OpenTLV/opentlv/issues/718).

Local preparation on 2026-10-27 verified the Core manifest, checksums and source
archive against their downloaded attestation bundles, requiring the Core
release workflow and exact source commit. Linux GCC 13.3 / Lua 5.4.6 passed
28/28 CTest entries, 165 Busted tests, 171 Query fixtures / 3422 checks,
installed-Core smoke and extracted development-package checks. The 91 Python
tests, API completeness and pre-commit checks also passed.

A prepared pin update or a passing local suite is not a published release.
LuaRocks upload is outside this candidate's publication requirements.

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
rock, a manifest and SHA256SUMS, then attests every file. It rebuilds
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
