# Nebula

An always-local, single-user note-taking and diary application. Database entries
open as flowing block documents, with Notion-inspired organization and
Zim-inspired cross-references. Linux comes first; macOS is planned.

Workspace pages and properties are stored in a local SQLite database. The separate
`--demo` mode edits an in-memory sample and discards it on close. Implementation
status, milestones, bugs, and verification results belong in [ROADMAP.md](ROADMAP.md).

## Build with Docker

Use the existing Docker CLI and daemon. Qt and build dependencies stay inside the
development image; this workflow does not install packages into the host OS.

```sh
./scripts/dev.sh build-image
./scripts/dev.sh test gcc
./scripts/dev.sh test clang
./scripts/dev.sh test core
```

`build-image` downloads Debian and installs packages inside the image. Subsequent
build/test containers run without networking, as your numeric user/group, with a
read-only root filesystem and source mount. Build output goes to the ignored
`build-container-gcc/`, `build-container-clang/`, or `build-container-core/` folder.
Containers are removed after each command. Docker daemon access is required; the
wrapper does not change daemon settings, socket permissions, or user groups.

For a development shell, run `./scripts/dev.sh shell`. Source is read-only at
`/workspace`; build output is writable at `/build`. Dependency requirements and
the optional project-local SDK workflow are described in
[DEPENDENCIES.md](docs/DEPENDENCIES.md).

## Run

```sh
./scripts/dev.sh run
```

The launcher rebuilds the executable, then asks where to keep the workspace. It
suggests `~/Document/nebula/`. Choose an existing Nebula folder or enter a new
location; Open workspace creates `nebula.sqlite3` and `attachments/` there.
Cancel leaves the location untouched. The selected host folder survives container
exit and is shown above the editor.

Choose a database, create a page, and select its row to edit the flowing document.
Double-click table cells to edit titles or typed properties. New view/Edit view
configure filters, sorting, and visible columns over the same pages. A new workspace
includes a Journal database with Entry date, Mood, and Tags. Journal entry asks for
the intended calendar date. Trash and Restore preserve page/database identities.
Restore also lists deleted block fragments after restart. Restore a trashed page,
database, or missing parent first; restoring a fragment preserves edits to blocks
that are already present.

Edits save after a 500 ms pause, before navigating to another page, and when closing
the window. The status distinguishes pending edits, successful saves, and failures.
Ctrl+S saves immediately. On failure, edits remain visible and closing asks whether
to keep the window open or discard pending changes. Undo survives autosave but is
cleared when switching pages and does not survive restart.

The folder chooser sees your home directory read-only. The editor receives only
the selected workspace as a writable data mount at `/data`. To select a location
directly, including a location outside your home:

```sh
./scripts/dev.sh run --workspace /path/to/your/nebula
```

The Linux GUI uses X11 or Xwayland and requires `DISPLAY`, `/tmp/.X11-unix`, and a
readable `XAUTHORITY` file (or `~/.Xauthority`). GUI containers share that display
socket and authorization file. Headless tests do not share the display. No
`xhost +`, privileged mode, or host networking is used.

To open just the in-memory sample or the editor comparison:

```sh
./scripts/dev.sh run --demo
./scripts/dev.sh run --demo --compare-editors
```

The comparison tab uses separate text widgets with independent selection and undo.
Use the **Block editor** tab for structural editing; block controls are disabled
in the comparison tab.

## Edit blocks

Click a block to make it active. A thin outline with a subtle shadow identifies
the active block; the label above the editor shows its position and nesting level.
Drag its grip dots to move the block and its children. Drop above/below a block to
reorder it; move the pointer to the right of its text to nest under it. The drop
marker shows the destination, Escape cancels, and dragging near the top/bottom
scrolls the document.

| Action | Keyboard or control |
| --- | --- |
| Create a block | Enter |
| Soft line break inside a block | Shift+Enter |
| Move a block and its children | Alt+Up/Down, or Move up/down in the toolbar |
| Nest under the previous sibling | Tab, or Indent |
| Leave the current parent | Shift+Tab, or Outdent |
| Bold / italic | Ctrl+B / Ctrl+I |
| Undo / redo | Ctrl+Z / Ctrl+Shift+Z |
| Follow a sample block reference | Ctrl+click the link |

The first sibling cannot be indented. Unavailable toolbar actions are disabled.
Wrapping text does not create a block. Splitting a parent gives the trailing block
a new identity and transfers its children there; structural edits can be undone.
Image and file cards in demo mode are layout representations, not imported files.

Use Link… to select a page, heading, or block across databases. Ctrl+click follows
it; the backlinks list opens the referring page/block. Targets retain their IDs
through renaming and trash/restore. The link picker disambiguates page names by
database. The picker can insert an unresolved page name without creating it;
creating another page with that title does not silently bind the link. Wiki syntax
and the complete unresolved-target workflow are tracked in the roadmap.

## Backup and restore

```sh
./scripts/dev.sh backup /path/to/nebula /path/to/new-backup
./scripts/dev.sh restore /path/to/new-backup /path/to/new-workspace
```

These commands work without a display. Destinations must be new folders outside
the source workspace. The source is mounted read-only; the database is copied
using SQLite's backup API. Restore creates a new workspace copy and preserves the
original backup. The attachment directory is copied, but managed file import and
complete attachment recovery are not implemented yet. The format and durability
rules are documented in [WORKSPACE_FORMAT.md](docs/WORKSPACE_FORMAT.md).

## Checks and measurements

`test gcc` and `test clang` run CTest for the document core, launcher, editor,
workspace lifecycle/recovery, application window, and launch check. `test core` builds the
document core without Qt or SQLite.

```sh
./scripts/dev.sh test-native
./scripts/dev.sh run --smoke-test
./scripts/dev.sh run --screenshot /build/prototype.png
./scripts/dev.sh benchmark single
./scripts/dev.sh benchmark separate
```

`test-native` runs editor and desktop interactions on X11/Xwayland. The screenshot
appears at `build-container-gcc/prototype.png`. Smoke tests and screenshots skip
the chooser unless `--workspace` is supplied. Benchmarks print JSON measurements
for the same 1,000-block fixture; see
[EDITOR_FEASIBILITY.md](docs/EDITOR_FEASIBILITY.md) for methodology.

Nebula's source is [MIT licensed](LICENSE). Dependencies retain their own licenses.
Project management conventions are in [AGENTS.md](AGENTS.md).
