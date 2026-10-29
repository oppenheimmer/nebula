# Nebula

A local, single-user note-taking and diary application in development. Database
entries will open as flowing block documents, with Notion-inspired organization
and Zim-inspired cross-references. Linux comes first; macOS is planned.

The current deliverable is the **M01 editor feasibility prototype**. It edits a
sample page in memory and compares two native editor designs. **Closing the window
discards edits.** Workspace storage, database views, managed file import, and the
full wiki-link workflow are later milestones in [ROADMAP.md](ROADMAP.md).

## Isolated development with Docker

Qt and build dependencies belong to the project development image. Nothing in
this workflow installs Qt into the host OS. It uses the existing Docker CLI and
daemon; Podman is not required.

```sh
./scripts/dev.sh build-image
./scripts/dev.sh test gcc
./scripts/dev.sh test clang
./scripts/dev.sh test core
```

The first command downloads a Debian 13 base image and installs development
packages **inside that image**. Later commands run without networking. Containers
are removed after each command. Source is mounted read-only at `/workspace`;
generated files go to `build-container-gcc/`, `build-container-clang/`, or
`build-container-core/`, all excluded from Git. Containers run as your numeric
user/group with a read-only root filesystem, temporary writable `/tmp`, no added
capabilities, and no Docker socket or home-directory mount.

Docker images share the host's Linux kernel. This supplies filesystem, process,
and network isolation analogous to the development use of a jail; it does not
provide a separate kernel. Docker daemon access is required. The wrapper does not
change socket permissions, user groups, or the daemon configuration. See
[Docker's security model](https://docs.docker.com/engine/security/) for the daemon
boundary.

For a shell in the same environment:

```sh
./scripts/dev.sh shell
```

Edit source with your host editor. The container shell can write build output in
`/build`; the `/workspace` source mount remains read-only.

## Run the prototype

After `./scripts/dev.sh test gcc`, launch it through the container:

```sh
./scripts/dev.sh run
```

The Linux GUI launcher uses X11 or Xwayland: it requires `DISPLAY`, the X11 socket,
and a readable `XAUTHORITY` file (or `~/.Xauthority`). Only that display socket and
authorization file are shared. Headless test commands do not share the display.
The launcher does not use `xhost +`, privileged mode, or host networking. Native
Wayland socket integration and macOS container GUI launching remain future work.

For an automatically closing GUI check or a screenshot of only this window:

```sh
./scripts/dev.sh run --smoke-test
./scripts/dev.sh run --screenshot /build/prototype.png
./scripts/dev.sh test-native
```

The screenshot is written to `build-container-gcc/prototype.png` on the host.
`test-native` runs the interaction suite through the same X11/Xwayland launcher.

The page-wide document tab currently supports:

- Paragraph splitting/merging, soft line breaks, and selection across blocks.
- Bold/italic formatting, headings, bullets, and to-do markers.
- Stable block identities, nesting, and subtree movement with undo/redo.
- Typed inline references and Ctrl+click navigation to blocks in the sample page.
- Painted image/attachment representations for testing layout and selection.
- Plain-text clipboard import, including multiline and Unicode text.

Use Enter for a new block, Shift+Enter for a soft break, Alt+Up/Down to move a
subtree, and Tab/Shift+Tab to indent/outdent. A split gives the trailing block a new
ID; existing children follow that trailing block. The separate-block tab is an
investigation baseline: native selection and undo remain local to each widget.

Asset cards are representations, not imported files. Rich clipboard preservation,
drag reordering, link completion/backlinks, persistence, and autosave are tracked
in the roadmap. Prototype limitations and evidence are recorded in
[the editor feasibility report](docs/EDITOR_FEASIBILITY.md).

## Tests and measurements

CTest runs a standalone core suite, Qt interaction tests, and a prototype launch
check. The core suite builds without Qt when `NEBULA_BUILD_EDITOR=OFF`. Every
implemented editing behavior has focused tests; the parent-splitting regression
is retained alongside them.

The Qt suite sends keyboard, mouse, clipboard, and synthetic input-method events.
It also checks the accessible text interface. Real OS input-method and
screen-reader behavior still require desktop validation; headless passes do not
establish those results.

```sh
./scripts/dev.sh benchmark single
./scripts/dev.sh benchmark separate
```

These commands load the same 1,000-paragraph fixture in fresh processes and emit
JSON with load time, typing latency, native editor count, and Linux peak resident
memory. Treat these as local measurements; record hardware, build type, toolkit,
and display backend when comparing results.

An explicitly scoped SDK can also be used without a container, when desired:

```sh
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/project/.tools/qt
cmake --build build-local --parallel 4
ctest --test-dir build-local --output-on-failure
```

This is an optional development fallback. Do not install project SDKs through the
host package manager or change global PATH/library settings. Exact tested versions
and dependency boundaries are in [DEPENDENCIES.md](docs/DEPENDENCIES.md).

Nebula's own source is [MIT licensed](LICENSE). Dependencies retain their own
licenses. Project management conventions are in [AGENTS.md](AGENTS.md).
