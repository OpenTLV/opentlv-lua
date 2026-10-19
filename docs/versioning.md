# Lua binding versioning

Lua releases belong to `OpenTLV/opentlv-lua` and use `vMAJOR.MINOR.PATCH`
Git tags, for example `v0.1.0`. They do not track the version of OpenTLV Core.
The release gate runs Full CI before creating a new remote version tag.
The package remains named `opentlv`; `scm-1` remains its development rockspec.

## Version metadata

CMake writes `lua-version.json` in the build directory at configuration time
and refreshes it whenever `opentlv_lua` is built. The rock copies that file into
its versioned LuaRocks directory, normally
`<tree>/lib/luarocks/rocks-<lua-abi>/opentlv/scm-1/lua-version.json`.

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
establish archive provenance. A shallow checkout only knows tags actually
fetched; fetch the intended release tag to identify a release. CI checks out
the full history and tags.

This metadata does not add or rename Lua API fields. `opentlv.version()` and
`opentlv._VERSION` still call `tlv_version_string()` and identify the linked
Core library. The Core dependency SHA is recorded separately in
`cmake/Core.cmake`; it is not a binding version.

## First standalone release baseline

Lua binding versioning starts with the first release tag, **v0.1.0**. Until that
tag is added, the binding remains an unversioned development build (`scm`),
packaged by `opentlv-scm-1.rockspec`. No provisional numeric release version is
assigned. The original tag-only quality gate established this initial baseline.
The current [manual release workflow](releases.md) verifies and publishes later
candidates only after Full CI succeeds. The migration's source,
CMake, test, and LuaRocks migration baseline is Core commit
`d87dcf96b2707e817dd51163a9f9efa36651e929`, which is also the pinned Core
dependency. [Migration provenance](migration.md) records the source repository,
destination, inventory, and validation boundaries. The existing API and Lua
5.1â€“5.4/LuaJIT compatibility are retained.

For a release, choose the Lua version independently of Core, close the standalone
changelog and merge to `main`. Dispatch the manual gate with its only input
`tag`, for example `v0.2.0`. It locks the dispatch SHA and runs Full CI with a
private local candidate tag, so validated artifacts contain release metadata
before the publisher creates the remote tag. See [release instructions](releases.md).

LuaRocks publication is a separate follow-up. Once the `v0.1.0` tag exists, a
versioned copy of the existing rockspec uses:

```lua
version = "0.1.0-1"
source = {
   url = "git+https://github.com/OpenTLV/opentlv-lua.git",
   tag = "v0.1.0"
}
```

Its filename is `opentlv-0.1.0-1.rockspec`. The final `-1` is the LuaRocks
packaging revision, not part of the Git tag or Core version. Keep the existing
build/install commands; the development rockspec stays `scm-1`. Validate the
release checkout and installed modules before publication. The quality gate publishes validated module archives and a GitHub Release. It
does not create a versioned release rockspec or publish to LuaRocks.
