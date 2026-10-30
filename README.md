# Nebula

An always-local, single-user note-taking and diary application. Database entries
open as flowing block documents, with Notion-inspired organization and
Zim-inspired cross-references. Linux comes first; FreeBSD and OpenBSD on amd64
are planned.

Workspace pages and properties are stored in a local SQLite database. The separate
`--demo` mode edits an in-memory sample and discards it on close. Implementation
status, milestones, bugs, and verification results belong in [ROADMAP.md](ROADMAP.md).

New here? Start with the [first-use guide](docs/USER_GUIDE.md), including save
checks, recovery and common corrections.

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
location; Open workspace creates `nebula.sqlite3`, `attachments/`, and the lock files
`.nebula.lock` and `.nebula-session.lock` there. New workspace folders are private to
your account (0700) and the database file is 0600. Cancel leaves the location
untouched. The selected host folder survives container exit and is shown above the
editor. Backup and export folders are refused, so restore or import them instead; a
non-empty folder that is not a workspace asks for confirmation first. A workspace can
be open in only one Nebula process at a time. Unsupported or modified schema objects
(for example triggers) are refused.

Choose a database, create a page, and select its row to edit the flowing document.
Double-click table cells to edit titles or typed properties. Numbers accept a decimal
point or comma; tags are comma-separated, and empty or repeated entries are ignored.
New view/Edit view/Delete view manage filters, sorting, and visible columns over the
same pages; Rename property and Delete property change the database's fields
(deleting removes that field's values after confirmation). Text sorts
case-insensitively in natural order ("Item 9" before "Item 10"), and Contains ignores
case. A new workspace includes a Journal database with Entry date, Mood, and Tags.
Journal entry asks for the intended calendar date. Trash and Restore preserve
page/database identities. Restore also lists deleted block fragments after restart.
Restore a trashed page, database, or missing parent first; restoring a fragment
preserves edits to blocks that are already present.

The small red trash icons act on three different scopes: beside the database selector
for the whole database, beside the page title for that page, and in the block toolbar
for the active block and its nested blocks. Each opens a confirmation with Cancel as
the default. Database trash also requires an acknowledgement checkbox. These controls
retain content and attachments for Restore; block trash can also be undone while the
page stays open. Restore… also offers **Empty trash permanently…** for content you
want gone for good; see [TRASH.md](docs/TRASH.md) for scope, recovery and permanent
deletion.

Edits save after a 500 ms pause, before navigating to another page, and when closing
the window. The status distinguishes pending edits, successful saves, and failures.
Ctrl+S saves immediately. A forced kill or power loss can lose edits still pending.
On failure, edits remain visible and closing asks whether
to keep the window open or discard pending changes. If the page changed elsewhere,
Workspace → Keep my version as a new page saves your edits as a separate copy. Undo
survives autosave but is cleared when switching pages and does not survive restart.
Terminating the process (for example `docker stop`) saves pending edits first.
Workspace → Check workspace integrity… runs the full database check, which also runs
automatically after upgrades and weekly on open.

Ctrl+Shift+F opens local workspace search over titles, blocks and properties.
Select a database or search all active notes, then open a matching page/block.
Words are AND-combined; a trailing * requests a prefix. Add reference filter finds
incoming/outgoing page and block references, including with no search words.
New view/Edit view saves the same reference conditions alongside properties.
Search → Rebuild search and reference indexes repairs derived data. Language
behavior and limits are documented in
[SEARCH.md](docs/SEARCH.md).

Import / Export provides Markdown folder conversion with a visible report.
Imports create new pages in the selected database and remap links within the
folder. Exports include active pages and referenced managed files. Markdown
omits properties, views, trash and recovery history; see the supported subset and
conversion limits in [MARKDOWN.md](docs/MARKDOWN.md).

Lossless JSON folder export preserves IDs, properties, views, reference states,
trash, recovery records and retained attachment bytes. JSON import validates into
a new workspace and opens it in a separate window; existing destinations are
refused. See [INTERCHANGE.md](docs/INTERCHANGE.md) for the versioned format,
validation rules, size limit and headless Docker commands.

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
| Insert a divider followed by an empty paragraph | Divider in the toolbar |
| Move a block and its children | Alt+Up/Down, or Move up/down in the toolbar |
| Nest under the previous sibling | Tab, or Indent |
| Leave the current parent | Shift+Tab, or Outdent |
| Bold / italic | Ctrl+B / Ctrl+I |
| Paste only plain text | Ctrl+Shift+V, or Paste plain text in the context menu |
| Undo / redo | Ctrl+Z / Ctrl+Shift+Z |
| Move the active block and its children to another page | Ctrl+Shift+M, or Workspace → Move block to page… |
| Undo / redo the latest block transfer | Ctrl+Alt+Z / Ctrl+Alt+Shift+Z, or the Workspace menu |
| Follow a sample block reference | Ctrl+click the link |
| Follow the reference at the editor cursor | Ctrl+Enter, or Navigate → Follow reference at cursor |
| Return to the previous position / go forward | Alt+Left / Alt+Right |

The first sibling cannot be indented. Unavailable toolbar actions are disabled.
Moving and nesting keep the caret and selection where they were. Wrapping text does
not create a block. Joining two blocks with Backspace/Delete does not add a recovery
entry, since their text is still on the page. Splitting a parent gives the trailing
block a new identity and transfers its children there; structural edits can be
undone. Enter on an empty list/to-do block returns it to a paragraph. Consecutive
typing forms one undo group until navigation, another command, a focus change, or a
pause of at least one second. Use Attachments → Import image… / Import file… to save
a managed copy and insert a card. Large files show progress and can be cancelled.
Moving or deleting the original source does not affect the note. Image cards preview
local files; Open current attachment hands other programs a read-only copy named like
the original, so they cannot change the managed bytes. Save a copy… uses the selected
card. Import is undoable and saves immediately.

For importing host files in Docker, use `./scripts/dev.sh run --import-root
/path/to/source-folder`; the chosen folder appears read-only at `/imports`.
Check managed files… reports missing/changed bytes and can clean unregistered
interrupted imports. Registered imports remain retained for undo and recovery.
See [ATTACHMENTS.md](docs/ATTACHMENTS.md) for retention and preview limits.

Workspace → Move page to database… keeps the page and block identities. Property
values follow by matching name and type; missing destination properties are added.
A conflicting property type cancels the whole move without dropping values.
Moving a block to another page appends its subtree with the original identities,
so existing links follow it. Both pages save together. An emptied source receives
an empty paragraph. Moves clear the open page's editing undo. The latest block
transfer has separate workspace undo/redo, available during this window's lifetime
while neither affected page has changed since the transfer or its last undo/redo.

Copy/cut/paste within Nebula retains block types, nesting, bold/italic text, card
labels, and references. New blocks receive new IDs; the first pasted block may
merge into the existing block. Links between copied blocks point to their pasted
counterparts. References to other targets retain their IDs. Cut/paste also creates
pasted identities; links elsewhere continue to identify the original blocks.
Cross-workspace targets may therefore be unavailable. HTML from other applications
supports headings, lists, bold/italic text, and paragraphs. Other styling and URL
links become text; images become `[image]` without fetching resources. Plain text
keeps paragraph boundaries and normalizes line endings. Invalid Nebula clipboard
data is rejected visibly without replacing the selection.
The full contract is in [CLIPBOARD_FORMAT.md](docs/CLIPBOARD_FORMAT.md).

Use Link… to select a page, heading, or block across databases. Ctrl+click follows
it; incoming/outgoing link lists include source excerpts and support Enter to open
the exact destination. Navigate → Back/Forward restores the prior page, block,
selection, and scroll position during the window's lifetime. Unavailable history
destinations retain their entries so you can restore the target and retry.
Targets retain their IDs
through renaming and trash/restore. The link picker disambiguates page names by
database. The picker can insert an unresolved page name without creating it;
creating another page with that title does not silently bind the link. Type
`[[Page]]`, `[[Page#Heading]]`, or `[[Page#^block-id]]` for wiki entry; append
`|label` for a custom label. Completion shows database/target context, and duplicate
matches require choosing a specific target. Link… also searches page/heading/block
text and IDs. Default labels follow target titles/text, while explicit labels stay
fixed. Following an unresolved reference offers existing-target selection or
explicit page creation in a chosen database. Page creation and source binding
commit together; undoing the binding keeps the created page. Escaping,
completion, and unresolved behavior are described in [LINKS.md](docs/LINKS.md).

The Link… picker loads pages and blocks in batches of 100. Expand a page to see
its matching blocks; select **Show more pages… / Show more blocks…** and press
Enter or double-click for the next batch. Search examines all cached targets,
including blocks beyond the displayed batch and unsaved text on the open page.

## Pasted web links

Pasting a complete HTTP or HTTPS URL keeps the link and attempts a text-only
preview once. The status bar offers **Cancel preview**. A successful preview is a
separate editable paragraph; trash that block to keep just the link. Insertion
has its own undo step. Save/reopen, clipboard, backup and lossless export preserve
the text without fetching it again. Pictures and thumbnails are never imported.

The normal workspace GUI has network access for this paste action. Ordinary notes
work offline; builds, tests, the chooser, demo/smoke runs and data commands stay
offline. Unsupported pages, timeouts, cancellation and network errors leave the
URL in place. Requests send the pasted path/query to that origin, without cookies,
credentials or Referer. Redirects must remain on the same scheme, host and port;
pages requiring login or a cross-origin redirect may have no preview. See
[PREVIEW_DESIGN.md](docs/PREVIEW_DESIGN.md) for limits and data handling.

## Backup and restore

```sh
./scripts/dev.sh backup /path/to/nebula /path/to/new-backup
./scripts/dev.sh restore /path/to/new-backup /path/to/new-workspace
```

These commands work without a display. Destinations must be new folders outside the
source workspace. The source is mounted read-only, and the container can write only
to a private staging folder that is renamed into place afterwards; the database is
copied using SQLite's backup API. Restore creates a new workspace copy and preserves
the original backup. Managed attachment availability and hashes are verified; a
manifest covers the database and attachment files. Missing or changed files fail the
backup, and restore rejects an incomplete or accidentally modified backup. The
manifest detects corruption, not deliberate tampering: anyone who can edit a backup
can also rewrite its manifest. Data commands refuse uncommitted source changes unless
`NEBULA_ALLOW_DIRTY=1` is set. The format and durability rules are documented in
[WORKSPACE_FORMAT.md](docs/WORKSPACE_FORMAT.md).

## Checks and measurements

`test gcc` and `test clang` run CTest for the document core, launcher, editor,
workspace lifecycle/recovery, application window, fuzz smoke cases and launch check,
with warnings treated as errors. `test core` builds the document core without Qt or
SQLite. `test asan` repeats the suite in a Debug build with AddressSanitizer and
UndefinedBehaviorSanitizer.

```sh
./scripts/dev.sh test asan
./scripts/dev.sh fuzz 30
./scripts/dev.sh format --check
./scripts/dev.sh stress 20
./scripts/dev.sh test-native
./scripts/dev.sh run --smoke-test
./scripts/dev.sh run --screenshot /build/prototype.png
./scripts/dev.sh benchmark single
./scripts/dev.sh benchmark separate
./scripts/dev.sh benchmark workspace
```

`stress` repeats the editor and desktop suites to expose timing-dependent failures.
`test-native` runs editor and desktop interactions on X11/Xwayland. The screenshot
appears at `build-container-gcc/prototype.png`. Smoke tests and screenshots skip the
chooser unless `--workspace` is supplied. `benchmark single`/`separate` print JSON
measurements for the 1,000-block editor fixture; see
[EDITOR_FEASIBILITY.md](docs/EDITOR_FEASIBILITY.md) for methodology. `benchmark
workspace` measures the real window over a 10,000-block page, a 5,000-entry Journal,
a page referenced by 1,000 entries and a page with 200 images.
`.github/workflows/ci.yml` runs the same Docker checks on GitHub-hosted Linux.

`fuzz` uses Clang with coverage-guided libFuzzer, ASan and UBSan for each parser
target; its corpus, logs and crash artifacts stay in ignored `build-container-fuzz/`.
Rebuild the development image to supply the fuzzing and formatting tools.
`format --check` checks C++ formatting without writing; `format` applies the
repository style through the container. These commands need a Git checkout.

Nebula's source is [MIT licensed](LICENSE). Dependencies retain their own licenses.
Project management conventions are in [AGENTS.md](AGENTS.md).
