# Development dependency baseline

## Isolation

The user requires that Qt and other project dependencies stay outside system-wide
host installations. The default environment is `Dockerfile.dev`, operated through
`scripts/dev.sh` using the existing Docker installation. No Podman prerequisite,
host package-manager step, or global SDK environment modification is permitted.

Image installation runs during `docker build` inside the image filesystem. At
runtime, dependencies live in that image, source is mounted read-only, and build
output is confined to a project build directory. Network access is disabled for
build/test runs after the image has been created. Container support is a developer
tool; the application itself is a local desktop executable.

The base release is Debian 13 (`trixie-slim`). Package patch versions follow that
release's repositories when rebuilding the image, so the recipe is repeatable but
not an immutable package snapshot. Record the image ID and actual versions with
verification results; release packaging reproducibility remains M09 work.

## Source baseline

| Component | Requirement | Usage |
| --- | --- | --- |
| C++ | C++17 | Core values/operations and Qt adapters; no language extensions. |
| CMake | 3.22 or newer | Targets, optional GUI build, CTest integration. |
| Qt | 6.5 or newer | Core for internal JSON coding, Gui/Widgets for desktop adapters, Test for interaction tests. Core model types remain Qt-free. |
| Compiler | GCC or Clang with C++17 support | Both are exercised during development. |
| SQLite | 3.33 or newer | Versioned workspace records, transactions, link indexes, and online backup via its C API. JSON codec uses Qt Core internally. |
| Ninja | Container build tool | Host SDK fallback can use CMake's default generator. |

Qt 6.5 is the configured API minimum, not a claim that this change was tested
against every Qt 6.5 patch. The initial host-side validation used an isolated Qt
6.8.3 SDK under `/tmp/nebula-qt`, GCC 11.5, Clang 21.1.8, and CMake 3.31.8 on RHEL
9.8 x86_64. No host Qt packages were installed. The Docker workflow uses Debian's
Qt package and is validated separately in the feasibility report.
The temporary SDK, downloader environment, extra cursor runtime, and superseded
host build directories were removed after Docker validation. Qt development tools
now reside in the project image; the supported workflow does not depend on those
temporary paths.

The Docker image includes DejaVu, Noto CJK, and Noto Color Emoji. Its explicit
font profile is tested against Japanese, Arabic, and the sample emoji. The initial
image lacked that coverage even though UTF-8 round trips passed (BUG-002).

The core-only configuration has no Qt dependency:

```sh
cmake -S . -B build-core -DNEBULA_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --parallel 4
ctest --test-dir build-core --output-on-failure
```

It also has no SQLite dependency. A local GUI build can use an explicitly scoped
SDK without modifying global paths:

```sh
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/project/.tools/qt
cmake --build build-local --parallel 4
ctest --test-dir build-local --output-on-failure
```

Keep Qt, SQLite development files, and any additional compiler dependencies local
to that environment; do not install them through the host package manager.

`nebula_format` contains the application-owned JSON codec shared by storage and
the editor's versioned clipboard payload. It uses the existing Qt Core dependency
internally, exposes standard C++ values, and has no SQLite or Widgets dependency.
The editor does not link the storage module.

`nebula_files` provides managed-file copy/flush/locking and backup manifests. It
uses the existing Qt Core SHA-256/JSON facilities and a narrow POSIX file
adapter (BSD validation pending); no separate hashing or image library was added.
The desktop previews images using Qt Gui's existing image readers. Filesystem operations require
regular files, durable directory/file flushes, same-directory hard links and Unix
shared/exclusive locks. Linux checks do not establish BSD/amd64 filesystem behavior;
M10 must validate that adapter and the image-reader/plugin distribution.

## Paste-preview adapter

M07.8 adds a separate `nebula_previews` adapter using the existing Qt 6 baseline's
Network and Gui modules. Qt Network provides asynchronous HTTP(S), timeout and
cancellation; Qt Gui supplies text-only HTML extraction with resource loading
disabled. The document core, storage and headless interchange do not link this
target. No browser engine, HTML scripting runtime or image importer is added.
The adapter contract and remaining integration checks are in
[PREVIEW_DESIGN.md](PREVIEW_DESIGN.md).

The existing Debian 13 development image already contains Qt Network 6.8.2 and
its OpenSSL TLS plugin, with OpenSSL 3.5.7. No image rebuild or host package
installation was needed. Qt Network has the same commercial/LGPLv3/GPLv2 options
recorded by its [module documentation](https://doc.qt.io/qt-6.8/qtnetwork-index.html).
OpenSSL 3 uses [Apache License 2.0](https://openssl-library.org/source/license/).
Packaging must include the selected Qt/TLS plugin and applicable third-party
notices; this adapter does not settle release-distribution licensing choices.

M10 must verify Qt Network availability, the shipped
[TLS backend](https://doc.qt.io/qt-6.8/ssl.html), certificate trust and plugin
deployment separately on FreeBSD amd64 and OpenBSD amd64. Linux checks do not
establish BSD HTTPS support; dependency availability and supported versions
remain platform validation requirements.

## Licensing and references

The user selected MIT for Nebula's own source on 2026-10-29. The root `LICENSE`
applies to that source. No Qt or Zim source/assets are copied into this repository.
The prototype's image card is drawn by application code.

Qt is a separate dependency with its own commercial and open-source licensing
options; selected module and third-party notices must accompany a distributed
package as appropriate. This development image is not a release packaging design.
Record the exact linked libraries and notices during M09/M10.
SQLite is [public domain](https://sqlite.org/copyright.html); it has no Qt or GUI
dependency. M10 must validate its version and required features on each BSD.
The bootstrap uses the
[`sqlite_schema` name available since SQLite 3.33](https://sqlite.org/forum/info/d90adfbb0a6eea88);
FTS5 must be enabled in SQLite for schema 8 search. The development image provides
it; no new external dependency or minimum version increase is introduced.
Search normalization uses existing Qt Core Unicode operations in the storage
adapter; the document core still builds without Qt/SQLite. See
[SEARCH.md](SEARCH.md) for tokenization and rebuild behavior.

The separate interchange target uses existing Qt Core, SQLite and managed-file
adapters. Its Markdown subset has an application-owned parser; no Markdown runtime
or HTML resource loader is added. BSD/amd64 portability requires verification of
those existing dependencies on both target systems; no BSD build is claimed yet.

- [Qt licensing](https://doc.qt.io/qt-6/licensing.html)
- [Qt Widgets](https://doc.qt.io/qt-6/qtwidgets-index.html)
- [Qt Test](https://doc.qt.io/qt-6/qttest-index.html)
- [Debian 13 Qt development package](https://packages.debian.org/trixie/qt6-base-dev)
- [Docker build documentation](https://docs.docker.com/build/)
- [Docker run reference](https://docs.docker.com/reference/cli/docker/container/run/)

Minimum supported end-user Linux distributions and FreeBSD/OpenBSD versions
remain open platform decisions. Both BSD targets are amd64 only. Compiling on
Linux does not validate either BSD, and checking one BSD does not validate the other.
The Linux Docker workflow cannot supply native BSD validation: use separate
isolated amd64 machines, VMs or runners with environment-scoped dependencies.
