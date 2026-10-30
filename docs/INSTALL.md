# Linux package preparation and verification

The current artifact is a **development preview**, `0.1.0~dev1`, for Debian 13
amd64. It is not a published release. Release verification and remaining issues
are tracked in [ROADMAP](../ROADMAP.md).
Other distributions and architectures are not established by this verification.

From the checkout, using the existing Docker daemon:

```sh
./scripts/dev.sh build-image
./scripts/package-check.sh
```

The development image pins the official Debian manifest digest and the signed
2026-10-30 Debian/security package snapshot in `packaging/debian.sources`.
Package creation runs offline. Output is
`build-container-gcc/packages/nebula-0.1.0~dev1-x86_64.deb` and stays out of Git.
The script then prepares an ephemeral Debian runtime image, installs the package
offline, and runs checks with networking disabled, read-only host mounts and no
compiler/CMake in that runtime. It does not install anything on the host.

The package installs `nebula`, the desktop launcher and `nebula_workspace` for
headless data operations. CPack derives ELF library dependencies; Qt platform
plugins, TLS trust and fonts are explicit runtime dependencies. Qt/SQLite/system
libraries and image plugins are supplied by Debian packages rather than copied
into Nebula's package. Nebula's MIT notice and the documentation install under
`/usr/share/doc/nebula/`. Third-party licenses remain separate; see
[DEPENDENCIES](DEPENDENCIES.md).

For a prepared Debian desktop with the package installed, launch `nebula` or use
the application menu. Choose a workspace folder; no service, account or network
setup is required. The default is `~/Document/nebula/`. The database, managed
attachments and lock files remain in the chosen folder, outside package-owned
directories. Follow the [first-use guide](USER_GUIDE.md) after opening it.

Before replacing an installation, save and close Nebula, then back up the entire
workspace with the existing binary:

```sh
nebula_workspace backup /absolute/workspace /absolute/new-backup
```

An ordinary package upgrade replaces program files and leaves workspaces alone.
The application migrates supported older workspace schemas transactionally when
opened. Unsupported/newer formats are refused. Do not downgrade a workspace by
opening it with an older binary. Restore a verified backup into a fresh folder
when recovering or trying a prior version:

```sh
nebula_workspace restore /absolute/new-backup /absolute/new-workspace
nebula --workspace /absolute/new-workspace
```

Package verification checks reinstallation, package removal and reinstallation
against a workspace owned by an unprivileged user. It checks retained IDs,
references, date values and attachment bytes after backup/restore and lossless
JSON import, plus actual window editing/search tests and preview TLS tests. These
checks complement the schema-migration regression suite; they are not a claim of
upgrade compatibility with an earlier public package, since none exists yet.

Before publishing, complete the remaining M08 acceptance and M09 release checks,
review dependency/image-plugin security updates, advance the pinned snapshot
deliberately, rerun all applicable checks, review Debian dependency notices and
record the exact source, image, package checksum and known limitations. Package
verification downloads dependencies during image preparation; all note workflow
checks run offline. A reproducible recipe is recorded, but byte-for-byte package
reproducibility has not been established.
