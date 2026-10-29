# Lua release description pilot

Lua alone pilots this format for [#52](https://github.com/OpenTLV/opentlv-lua/issues/52).
Other OpenTLV repositories retain their own release workflows. Evaluate the
readability, maintenance effort and evidence links after a real Lua release
before proposing adoption elsewhere. This does not authorize publication or
rewrite an existing GitHub Release.

## Format

The GitHub title is `OpenTLV Lua X.Y.Z`; the Git tag is `vX.Y.Z`.
The Markdown body has no duplicate title. Its overview starts with what the
release delivers, and the version appears where useful under Compatibility.

The required sections have this stable order:

| Section | Source and contract |
| --- | --- |
| Overview | Required, manually written single paragraph in `release-notes.json`; rendered as literal prose, without repeating the release title. |
| What's Changed | The selected approved changelog version only. Nonempty Added, Changed, Deprecated, Removed, Fixed and Security categories appear in that order. List contents retain their Markdown. |
| Compatibility | Binding version/tag, exact verified Core release and SHA, and runtime/platform combinations actually present in the passed Full records. |
| Installation / Distribution | Only the tested GitHub module archives and, when evidenced, the attached source rock/rockspec. Includes the isolated Linux Lua 5.4 loading command used by package verification. |
| Verification and artifacts | Exact Lua SHA, release run/attempt, per-configuration CTest/Busted/Query results, artifact SHA-256, Core manifest identity, checksum and attestation instructions. |
| Links | Changelog and docs at the exact release commit, run/attempt, manifest, checksums and the attested body. No unverified Docs portal publication claim. |

Empty changelog categories are omitted. Changelog links must use inline
absolute HTTPS URLs, because relative and reference-style links would lose
their context in a GitHub Release. The generator rejects unsupported category
names and duplicate version sections instead of silently dropping content.

Registry availability is never inferred from a GitHub source rock or a passed
LuaRocks build. This pilot does not emit `luarocks install opentlv` or claim a
registry upload. Any later registry announcement requires independent receipt
and consumer verification; the attested original body stays immutable.

## Editorial changes and issue links

Maintain [`release-notes.json`](release-notes.json) alongside the candidate:

- `schema`: `1`.
- `version`: the unprefixed binding version.
- `overview`: a human summary of the release's purpose. Automation does not
  infer intent from commits or PR titles.

Parenthesized issue references at the end of changelog bullets, such as `(#51)`,
become explicit issue links in the release body. Qualified references such as `(OpenTLV/opentlv-docs#42)`
retain their owning repository. The source changelog is never changed by the
generator. Author names and a Contributors section are intentionally omitted;
readers can follow the issue links for implementation and contribution details.
Approved changelog descriptions remain the primary release content.

## Review and publication

1. Close the candidate changelog and update its version and overview.
   While 0.3.0 is still being prepared, these changes belong
   in that candidate and require review and revalidation before dispatch.
   Do not replace the commit of a running or already-approved release.
2. Generate the checked-in review preview:

   ```sh
   python -B scripts/release_notes.py preview --tag v0.3.0 --output docs/release-preview.md
   ```

3. Review [`release-preview.md`](release-preview.md) in the candidate PR,
   including the overview, all changes and issue links. Pending
   compatibility, channels and CI evidence are explicitly pending. Approving
   the template means the final facts will be filled from verified records,
   not that the preview establishes successful CI or publication. A test and
   release preflight reject a stale preview. The maintainer reviews this
   committed preview before approving the existing manual release dispatch.
4. After Full passes, assembly produces `release-notes.md` from the same
   reviewed sources and the exact request/quality manifest. It is shown in the
   assembly job summary, added to `SHA256SUMS`, attested and uploaded with the
   release payload. The prepare job also retains the reviewed preview in its
   own attempt-scoped artifact. These preview/summary links may refer to assets
   that become public only when the publisher succeeds.
5. Before creating a tag, the publisher revalidates preflight, Full evidence,
   checksums, attestations and same-attempt jobs, and regenerates the description
   byte-for-byte. A modified body fails even if its checksum was recomputed.
   `gh release create --notes-file` uses that exact file. The GitHub PR-list
   generator is no longer the primary body (`--generate-notes` is removed).

Existing manual dispatch, permissions, same-attempt requirements and protected
LuaRocks approval stay unchanged. This adds no release-dispatch input, bypass,
remote edit or automatic registry publication. Review the final summary and
attested body as release evidence; future editorial corrections must preserve
the immutable release files.

For a local render from an authenticated manifest:

```sh
python -B scripts/release_notes.py render --tag v0.3.0 \
    --manifest build/release/release-manifest.json --output build/release-body-review.md
```

The renderer checks evidence consistency, but a local invocation is not an
attestation verifier or publication gate. The workflow performs authentication
and release authorization separately.

## Historical example and evidence boundary

The [v0.2.0 example](release-notes-v0.2.0-example.md) was generated from the
actual [Lua v0.2.0 release](https://github.com/OpenTLV/opentlv-lua/releases/tag/v0.2.0),
commit `77abc807bdd59956790497483a9d1fbad4b79019`, and its
[successful release run, attempt 1](https://github.com/OpenTLV/opentlv-lua/actions/runs/40522912843/attempts/1).
It is a read-only example, not a replacement for the published description.
Its overview was prepared for this pilot and is not represented as historical
release metadata.

The fixture manifest is byte-identical to the public asset, SHA-256
`747696b13da60b183da4af51f2198a6941a800ac4ed17a80c5cfd97efd4210f7`, checked
against the GitHub asset digest and published `SHA256SUMS`. The original
changelog comes from that exact tag without editing its old headings. The
historical option accepts those old headings and the absence of release-rock
evidence; the live release path never enables that option. No source-rock
availability or release-description asset is invented for v0.2.0.

```sh
python -B scripts/release_notes.py render --tag v0.2.0 --historical \
    --manifest tests/fixtures/release-notes-v0.2.0/manifest.json \
    --changelog tests/fixtures/release-notes-v0.2.0/CHANGELOG.md \
    --editorial tests/fixtures/release-notes-v0.2.0/editorial.json \
    --output build/historical-release-description.md
```

The regression suite compares this output with the checked-in example and
checks the fixture's pinned digest. On 2026-10-29 the published v0.1.0 and
v0.2.0 descriptions both used a PR-list format, but their mechanisms differed:
the v0.1.0 workflow only created a Git tag, while v0.2.0's publisher called
`gh release create --generate-notes`. Similar-looking bodies alone do not
prove identical automation. Neither historical release was edited.
