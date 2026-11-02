# Linux release checklist

The current version is `0.1.0~alpha`, tagged `0.1-alpha` for the initial prerelease.
Its scope and limitations are in [RELEASE_NOTES](RELEASE_NOTES.md). Completing this list
provides the evidence for M09.4 in [ROADMAP](../ROADMAP.md). A passing development
build alone does not establish a final release.

## Version and source

- Set the intended version in `CMakeLists.txt`. `NEBULA_VERSION` supplies both
  executable diagnostics and package metadata. Remove the preview suffix
  only when the final release is ready.
- Record the tag, source revision and any local changes. Keep a release artifact tied
  to the source and documentation that were actually packaged.
- Record the development/runtime image IDs, base digest, signed package snapshot,
  compiler, Qt, SQLite and TLS versions, architecture and desktop/backend.
- Use upload-safe asset filenames. After uploading, download the assets and
  verify their bytes, digests and checksum manifest against the local artifacts;
  GitHub can normalize filenames without updating manifest contents (BUG-028).

## Dependency review

- Review the [Qt advisories](https://wiki.qt.io/List_of_known_vulnerabilities_in_Qt_products)
  and Debian security information for
  [Qt base](https://security-tracker.debian.org/tracker/source-package/qt6-base)
  and the image plugins actually installed. Inspect Debian backports as well as
  the upstream version; a Qt version string alone cannot show patch status.
- Record applicability and mitigation evidence. Text-only pasted previews use
  HTTP/1.1, disable caching and do not load subresources. This does not patch the
  system Qt libraries or establish that every dependency issue is mitigated.
- Review image decoders and TLS dependencies, the package's library/plugin
  inventory, and their license notices. Nebula bundles no third-party libraries
  or image plugins. A user's system may have additional plugins installed.
- Refresh the pinned base/snapshot deliberately when needed, then repeat affected
  checks. The frozen development image does not acquire security fixes by itself.

## Build and behavior

Run the documented isolated commands from [README](../README.md): GCC, Clang,
core-only, ASan/UBSan, formatter, parser fuzzing, repeated editor/window checks,
and the private IBus/Anthy/Orca desktop profile. Record unavailable checks. Use the
existing [performance fixtures and budgets](PERFORMANCE.md) for performance claims.

Run `./scripts/package-check.sh` on the final source. Confirm executable versions
match the installed package, installed documentation links work, and record the
artifact path and SHA-256 checksum. This checks installation/reinstallation/removal,
offline editing/search, preserved identities and managed files, interchange, and
backup/restore into fresh workspaces. Schema migration checks run separately in
the storage suites. The first alpha has no earlier published upgrade baseline;
future releases should verify upgrades from `0.1-alpha`.
For the separately requested EL9 RPM preview, run `./scripts/package-check.sh rpm`
using its own image and build directory; see [RPM](RPM.md). Its live repository
inventory is separate from Debian's pinned package snapshot.

## Release notes and limits

Document the delivered version, tested Linux profile and these material limits:

- Debian 13 amd64 is the verified release package baseline; EL9 x86_64 has
  separate RPM preview/container verification. Other distributions, architectures,
  input methods and compositors require their own verification.
- Autosave waits 500 ms; an abrupt kill or power loss can lose pending edits.
  Undo history is local to the open page and does not survive restart.
- Managed files, trash and recovery data have the retention rules in
  [ATTACHMENTS](ATTACHMENTS.md) and [TRASH](TRASH.md). Back up both the database and
  its referenced managed files with the documented tool.
- Markdown omits model information; use [lossless JSON](INTERCHANGE.md) for that
  contract. Incomplete exports may retain a private full-workspace scratch
  database; remove them before sharing an export.
- [Search](SEARCH.md) has documented Unicode/tokenization limits. Pasted previews
  can fail without losing the URL; [PREVIEW_DESIGN](PREVIEW_DESIGN.md) describes
  their request, redirect, timeout and size limits.
- Screen-reader block semantics use F8 Block details. The recorded reader profile
  verifies generated speech through a private null sink, without subjective audio
  quality acceptance.
- A pinned package recipe is available; byte-for-byte reproducibility is unproven.

Keep actual results and unresolved release issues in the roadmap work log. Finish
M09 only when its exit criteria are supported, then begin the scheduled M10 audit.
