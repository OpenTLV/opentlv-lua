# Lua binding versioning

Lua releases belong to `OpenTLV/opentlv-lua` and use `vMAJOR.MINOR.PATCH`
Git tags, for example `v0.1.0`. They do not track the version of OpenTLV Core.
The release gate runs Full CI before creating a new remote version tag.
The package remains named `opentlv`; `scm-1` remains its development rockspec.

## Version metadata

CMake generates `lua-version.json` and a private C version header from the same
version value at configuration time and refreshes both whenever `opentlv_lua`
is built. A version change recompiles the module, keeping its public API and
packaged metadata consistent without a second version constant. The rock copies the JSON file into
its versioned LuaRocks directory, normally
`<tree>/lib/luarocks/rocks-<lua-abi>/opentlv/<rock-version>/lua-version.json`,
for example `scm-1` or `0.3.0-1`.

| Field | Meaning |
| --- | --- |
| `binding_version` | Clean Lua semantic version without the `v` prefix, or an explicitly identified development build |
| `binding_revision` | Full Lua repository commit SHA, empty when Git identity is unavailable |
| `release_tag` | Full `vMAJOR.MINOR.PATCH` Lua tag pointing at HEAD, otherwise empty |
| `dirty` | Whether Git reports tracked modifications or untracked files; ignored build files do not count |

A clean checkout tagged `v0.1.0` reports `binding_version` as `0.1.0` and
`release_tag` as `v0.1.0`. An untagged commit reports
`scm+g<12-digit-sha>`. A modified checkout adds `.dirty`; at a release tag it
reports `0.1.0+g<12-digit-sha>.dirty`. A later untagged commit returns to `scm`
rather than presenting itself as the preceding release. The full `release_tag`
is retained on a dirty tagged checkout. Multiple `vMAJOR.MINOR.PATCH` release
tags on one commit are rejected. Other tags, including unprefixed numeric tags,
are not release versions.

The source directory must be the Git repository root. Core's tags and tags in
an enclosing workspace never supply Lua metadata. A source archive without
its own Git identity reports `scm+unknown` and an empty revision/tag; `dirty`
is false because there is no Git worktree status to inspect. This does not
establish archive provenance. A build of such a tree can declare its release
with `OPENTLV_LUA_RELEASE_TAG=vMAJOR.MINOR.PATCH`, as a
[versioned rockspec](#luarocks-release-rockspec) does; it then reports that
version and tag with an empty revision and `dirty` false. The declaration is
not verified. In a Git checkout the setting never replaces the checkout's own
metadata: configuration fails unless the checkout reports that release tag,
and a modified checkout at the tag still reports its dirty version. A shallow checkout only knows tags actually
fetched; fetch the intended release tag to identify a release. CI checks out
the full history and tags.

`opentlv.version()` and `opentlv._VERSION` both return `binding_version`,
including the development/dirty/archive forms above. This changes their former
Core-version semantics without renaming either API. No Lua API exposes the Core
version or commit SHA. The pinned Core dependency SHA remains in
`cmake/Core.cmake`; CI records the selected dependency in its build evidence and
module archives retain it in `build-identity.json`. Release manifests retain
the verified Core identity separately from the binding version.

## First standalone release baseline

Lua binding versioning started with the first release tag, **v0.1.0**.
Untagged development builds remain `scm`, packaged by
`opentlv-scm-1.rockspec`; no provisional numeric release version is assigned.
The original tag-only quality gate established this initial baseline.
The current [manual release workflow](releases.md) verifies and publishes later
candidates only after Full CI succeeds. The migration's source,
CMake, test, and LuaRocks migration baseline is Core commit
`d87dcf96b2707e817dd51163a9f9efa36651e929`. The current dependency is Core
v0.8.0 at `345c77acb29266036df5f27d04ce315bb61853cc`.
[Migration provenance](migration.md) records the source repository,
destination, inventory, and validation boundaries. The existing API and Lua
5.1â€“5.4/LuaJIT compatibility are retained.

For a release, choose the Lua version independently of Core, close the standalone
changelog and merge to `main`. Dispatch the manual gate with its only input
`tag`, for example `v0.2.0`. It locks the dispatch SHA and runs Full CI with a
private local candidate tag, so validated artifacts contain release metadata
before the publisher creates the remote tag. See [release instructions](releases.md).

## LuaRocks release rockspec

LuaRocks publication is tracked in
[Lua #42](https://github.com/OpenTLV/opentlv-lua/issues/42). The rock stays one
`opentlv` package containing the native `opentlv._core` module and the pure-Lua
`opentlv` facade. On 2026-10-29 the LuaRocks root manifests for Lua 5.1–5.4
contained no `opentlv` rock, a module search found none, and the upload account
[`marekcingel`](https://luarocks.org/modules/marekcingel) (website
`github.com/OpenTLV`) existed with no modules. LuaRocks names are claimed by
upload, not reserved, so recheck both immediately before the first upload.

`scripts/rockspec.py` generates a release rockspec from the development
rockspec of an existing release tag:

```sh
python scripts/rockspec.py v0.3.0             # build/rockspecs/opentlv-0.3.0-1.rockspec
python scripts/rockspec.py v0.3.0 --revision 2
python scripts/rockspec.py v0.3.0 --worktree  # candidate before the tag exists
```

It changes exactly three things: `version = "0.3.0-1"`, a `tag = "v0.3.0"`
line in `source`, and `-DOPENTLV_LUA_RELEASE_TAG=v0.3.0` instead of the empty
value in `build_command`. The package name, description, license, dependencies
and the native/pure-Lua build and install commands remain those of the tag's
`opentlv-scm-1.rockspec`. The final `-1` is the LuaRocks packaging revision,
not part of the Git tag or the public Lua binding version; increment it only to
repackage the same tag.

The release definition is required because LuaRocks clones the tag with
`--depth=1` and deletes `.git` before building, so the Git metadata above
reports `scm+unknown`. With the declaration, `opentlv-0.3.0-1.rockspec`
reports `0.3.0` through both Lua APIs and `lua-version.json`, and
`release_tag` `v0.3.0`, with an empty revision. `luarocks make` of a versioned
rockspec in a Git checkout fails unless that checkout is at the tag.
The development rock's `scm-1` identifies its rolling package channel; its
exact build version is in the API and JSON metadata.

Tags `v0.1.0` and `v0.2.0` predate the declaration: a LuaRocks build from
either reports `scm+unknown`, so the generator rejects them, and the first rock
must come from a later release tag. The release gate publishes validated module
archives and a GitHub Release. It does not generate a release rockspec or
upload to LuaRocks; upload requires separate approval.
