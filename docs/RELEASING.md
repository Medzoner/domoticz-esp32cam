# Release guide

The project uses [Semantic Versioning](https://semver.org/), Conventional
Commits, and Release Please. Normal releases do not require manually creating a
tag.

## Normal release

1. Merge contributor pull requests with a Conventional Commit-compatible squash
   title.
2. Release Please creates or updates a release pull request containing the next
   version and `CHANGELOG.md` entries.
3. Review that pull request and ensure its explicitly dispatched CI run is
   green. The dispatch is required because GitHub does not emit normal pull
   request events for branches created with `GITHUB_TOKEN`.
4. Merge the release pull request when the project is ready to publish.
5. Release Please creates the `v<major>.<minor>.<patch>` tag and GitHub Release.
6. The release workflow rebuilds tests and all supported board environments,
   then uploads firmware binaries, an AI Thinker flash bundle, and SHA-256
   checksums to that release.

Version selection follows Conventional Commits:

| Change | Example | SemVer bump |
|---|---|---|
| Bug fix | `fix: reject malformed control values` | Patch |
| Feature | `feat: add ESP-EYE support` | Minor |
| Breaking change | `feat!: rename control endpoint` | Major |

Documentation, tests, CI, and chores appear in the changelog when relevant but
do not independently request a version bump.

## Manual recovery release

Maintainers can still push a stable SemVer tag or run the Release workflow for
an existing tag. This path is intended only to recover publication after Release
Please has already merged the matching version into `version.txt` and
`.release-please-manifest.json`:

```bash
git tag v0.1.0 <release-pr-merge-commit>
git push origin v0.1.0
```

The workflow requires the tag, `version.txt`, and release manifest to agree,
which prevents a manual recovery from desynchronizing later automated releases.
Re-running it uploads assets with `--clobber`, so a failed asset publication can
be safely retried after the underlying problem is fixed.

## Release verification

After publication:

1. verify that all four board-specific firmware binaries are attached;
2. verify the AI Thinker archive and its `SHA256SUMS` file;
3. compare downloaded artifacts against the published checksums; and
4. flash at least the primary `esp32cam` artifact before promoting a major
   release broadly.
