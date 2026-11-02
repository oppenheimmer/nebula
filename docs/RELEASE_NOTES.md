# Nebula 0.1-alpha

Initial Linux prerelease of Nebula, an always-local, single-user note-taking and
diary application. The Git tag is `0.1-alpha`; executables and packages report
`0.1.0~alpha` so package managers order this version before `0.1.0`.

## Included

- Flowing block documents with headings, lists, tasks, managed images/files,
  keyboard editing, drag reordering and undo/redo.
- Databases, typed properties, saved filtered/sorted views and dated diary entries.
- Stable local page/heading/block links, backlinks and full-text search.
- Autosave with visible persistence status, trash/recovery, integrity checks and
  verified backup/restore including managed attachments.
- Lossless JSON interchange and Markdown import/export with documented limits.
- Removable text-only previews fetched once when pasting a web URL. Ordinary
  note operations work offline; previews do not fetch images or refresh on open.
- Desktop launchers and a headless `nebula_workspace` tool for data operations.

## Downloads and installation

Download the package for your distribution, `SHA256SUMS` and `BUILD_INFO.txt`
from the [GitHub release](https://github.com/oppenheimmer/nebula/releases/tag/0.1-alpha).
The build information records the exact source revision and verification profile.
From the download directory:

```sh
sha256sum --check --ignore-missing SHA256SUMS
```

On Debian 13 amd64:

```sh
sudo apt install ./nebula-0.1.0~alpha-x86_64.deb
```

On Enterprise Linux 9 x86_64 with compatible EPEL and CRB/CodeReady Builder
repositories configured:

```sh
sudo dnf install ./nebula-0.1.0~alpha-x86_64.rpm
```

The EL9 package requires Qt 6.10 runtime symbols; it was built and checked in
AlmaLinux 9.8 containers. Debian uses its Qt 6.8 runtime. Both packages dynamically
link distribution-provided libraries and are unsigned. Checksums detect damaged
downloads; they are not publisher signatures. See [Debian installation](INSTALL.md),
[RPM requirements](RPM.md) and [dependency licensing](DEPENDENCIES.md).

Launch `nebula` from a terminal or the application menu, choose a workspace folder,
then follow the [first-use guide](USER_GUIDE.md). `nebula --version` and
`nebula_workspace --version` work without a display.

## Alpha limits

This prerelease precedes completion of the stable-release checklist and the
scheduled code audit. Keep independent workspace backups when evaluating it.

- Linux x86_64 only. The package checks use isolated offscreen Qt environments;
  they do not establish support for every distribution, compositor, input method
  or screen reader. Native RHEL desktop acceptance has not been established.
- Autosave waits 500 ms. A forced kill or power loss can lose pending edits;
  check the saved status. Page undo history does not survive restart.
- Back up the workspace using the documented tool to include both the database
  and its managed attachments. Removal of the application preserves workspaces.
  There is no earlier public-package upgrade baseline.
- Markdown omits properties, views, trash and recovery history. Use
  [lossless JSON](INTERCHANGE.md) when those must be preserved. Incomplete exports
  can retain a private full-workspace scratch database; remove them before sharing.
- [Search](SEARCH.md) has Unicode/tokenization limits. Text previews can fail
  without losing the pasted URL. Screen-reader block details are available with F8.
- No AppImage is included. Byte-for-byte package reproducibility is unproven;
  EL9 dependency repositories are live, while Debian uses a pinned snapshot.

Verification evidence and remaining work are recorded in
[ROADMAP](../ROADMAP.md) and the [release checklist](RELEASE_CHECKLIST.md).
