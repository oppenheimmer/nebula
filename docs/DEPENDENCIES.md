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
| Qt | 6.5 or newer | Core/Gui/Widgets for the prototype; Test for interaction tests. |
| Compiler | GCC or Clang with C++17 support | Both are exercised during development. |
| SQLite | Planned; not linked by this prototype | Storage work begins in M03. Development headers are included in the container for that milestone. |
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

## Licensing and references

The user selected MIT for Nebula's own source on 2026-10-29. The root `LICENSE`
applies to that source. No Qt or Zim source/assets are copied into this repository.
The prototype's image card is drawn by application code.

Qt is a separate dependency with its own commercial and open-source licensing
options; selected module and third-party notices must accompany a distributed
package as appropriate. This development image is not a release packaging design.
Record the exact linked libraries and notices during M09/M10.

- [Qt licensing](https://doc.qt.io/qt-6/licensing.html)
- [Qt Widgets](https://doc.qt.io/qt-6/qtwidgets-index.html)
- [Qt Test](https://doc.qt.io/qt-6/qttest-index.html)
- [Debian 13 Qt development package](https://packages.debian.org/trixie/qt6-base-dev)
- [Docker build documentation](https://docs.docker.com/build/)
- [Docker run reference](https://docs.docker.com/reference/cli/docker/container/run/)

Minimum supported end-user Linux distributions and macOS versions remain open
platform decisions. Compiling this prototype on Linux does not validate macOS.
