## Overview

Prepares the first official LuaRocks distribution with reproducible source rocks, release validation and a separately approved registry publisher\. Lua remains independently versioned from its pinned OpenTLV Core dependency\.

## What's Changed

### Added

- Pilot a deterministic Lua GitHub Release description with a reviewed editorial overview and preview, changelog-derived changes with issue links, compatibility and artifacts from same-attempt Full evidence, and checksum/attestation coverage for the final body. Keep Git tags and release titles distinct, avoid repeating the title in the overview, and leave LuaRocks availability unclaimed until its separate publication is verified. Include a read-only example from the real v0.2.0 manifest without changing historical release bodies. ([#52](https://github.com/OpenTLV/opentlv-lua/issues/52))
- Document first LuaRocks release preparation and independent public consumer verification: authenticate release assets, compare registry bytes, install into a fresh tree, check version metadata and licenses, and run the installed public API, Query and quick-start scripts. Distinguish the published v0.2.0 baseline from the proposed next release and require publication, approval and documentation evidence before closing #46. ([#46](https://github.com/OpenTLV/opentlv-lua/issues/46))
- Add separately approved LuaRocks publication. The release gate rebuilds the release rockspec and source rock from the release commit, requires the digests recorded by every static-Core Full configuration, and attaches both files to the GitHub Release, covered by `SHA256SUMS` and attestations, as immutable release assets. The manual `luarocks-release.yml` workflow publishes the tag of an existing GitHub Release. Without the credential, it verifies the published release, tag commit, asset digests and attestations, the Full evidence and successful release jobs of the same attempt, and that the rock rebuilds byte-identically from the tag. A second job then runs only after approval in the `luarocks` environment, which alone holds `LUAROCKS_API_KEY`. It repeats the verification and uploads exactly the attested files through the luarocks.org upload API, without `--force`. The key is kept out of child processes, arguments, output and error messages. An existing version is never replaced: different remote files fail the run, identical ones succeed, and an interrupted upload only adds the missing source rock. The upload fails if another account owns the name or the module is not added to the root manifest. The run reads both files back from the account manifest and records a receipt. The release guide documents the environment setup, procedure and recovery. ([#45](https://github.com/OpenTLV/opentlv-lua/issues/45))

- Validate the release rock on every static-Core Full configuration: Linux, Windows and macOS with Lua 5.1–5.4 and Linux LuaJIT. `scripts/check_release_rock.py` builds the `.src.rock` of the tested commit the way `luarocks pack` would, byte-identical on every platform, then unpacks and makes it into a fresh tree without a Core override. It checks that the fetched Core is the pin, compares the installed rockspec, facade, `LICENSE`, `LICENSE-Core` and release metadata with the commit and the verified Core, loads the tree's native module from its search paths alone, and runs the twelve relocated module scripts, the Query corpus and the quick start. Release runs require the candidate tag to select the tested commit; other Full runs use the fixture tag `v0.0.0`. The aggregate and release gates require this evidence from all 13 configurations, with identical rock and rockspec digests and, for a release, the requested tag. The rock now installs Core's license as `doc/LICENSE-Core`, from the Core source or an installed package. A release build no longer searches for installed Core packages, so an unrelated installed OpenTLV cannot replace the pin. The versioning guide documents the rock contents, Core selection, build requirements and supported targets. ([#44](https://github.com/OpenTLV/opentlv-lua/issues/44))

- Prepare the `opentlv` LuaRocks rock: `scripts/rockspec.py vMAJOR.MINOR.PATCH` generates `opentlv-MAJOR.MINOR.PATCH-N.rockspec` from the `opentlv-scm-1.rockspec` of that release tag, changing only the version, the source tag and the declared release tag, so the single rock keeps the native `opentlv._core` module and pure-Lua facade with their build and install commands. LuaRocks deletes `.git` from the tag it clones, so a release rock reported `scm+unknown`; the rockspec now passes `OPENTLV_LUA_RELEASE_TAG`, which identifies a source tree without Git and must match the tag of a Git checkout, and the development rockspec passes it empty. Tags `v0.1.0` and `v0.2.0` lack it and are rejected. The versioning guide records the verified LuaRocks name and account and the release rockspec procedure; upload remains separate and requires approval. ([#43](https://github.com/OpenTLV/opentlv-lua/issues/43))

### Changed

- Normalize release headings to `[X.Y.Z]`, preserving release dates and history. The release gate matches those headings to `vX.Y.Z` Git tags; regression tests also protect the existing `OpenTLV Lua X.Y.Z` GitHub Release titles. Git tags, LuaRocks versions and packaging behavior remain unchanged. ([#51](https://github.com/OpenTLV/opentlv-lua/issues/51))

### Fixed

- Regenerate the release preview after candidate changelog formatting changes so the checked-in description matches its source and passes the stale-preview CI checks. ([#52](https://github.com/OpenTLV/opentlv-lua/issues/52))
- Report only the method of a rejected LuaRocks API request, without the server's error text: a response that quotes the request could otherwise put the API key from its URL into CI logs. A regression test returns errors containing the key and checks that the exception omits it. ([#45](https://github.com/OpenTLV/opentlv-lua/issues/45))

## Compatibility

Lua binding **0.3.0**. Requested baseline: [Core v0.8.0](https://github.com/OpenTLV/opentlv/releases/tag/v0.8.0) at `345c77acb29266036df5f27d04ce315bb61853cc`.

Verified runtimes and platforms will be listed from the successful release Full records.

## Installation / Distribution

Pending release evidence. The final body will link only the tested GitHub assets. LuaRocks publication requires separate approval and verification; no registry install command is promised here.

## Verification and artifacts

Preview only: Full CI, artifact hashes and attestations are pending. The final body will include the exact commit, run/attempt and tested configurations.

## Links

[Full changelog](https://github.com/OpenTLV/opentlv-lua/blob/v0.3.0/CHANGELOG.md) · [Lua API documentation](https://github.com/OpenTLV/opentlv-lua/blob/v0.3.0/docs/api.md)

These candidate links become available when the approved tag is published.
