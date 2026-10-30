# Nebula roadmap

Last updated: 2026-10-30 (Asia/Tokyo)

## Project goal

Build a Linux desktop note-taking and diary application inspired by Notion's
block editing and database organization and Zim Desktop Wiki's interconnected
notes. The primary interface is a database view: each entry is a page with editable
properties and a flowing document of movable blocks. Users should be able to write
naturally, organize entries through typed properties, cross-reference documents,
and keep their data usable for the long term.

The application is **always local and single-user**. Linux is the only target
platform. The architecture must be modular, with a documented storage format and
conservative C/C++ dependencies.

This describes the behavior we intend to build, not either application's internal
architecture or a promise of full Notion or Zim format compatibility.

## Agreed scope and design direction

| Area | Direction |
| --- | --- |
| Runtime | Local desktop application; no server, account, synchronization, or collaboration service. |
| Pasted link previews | User-authorized exception: fetch a pasted web page once for a removable text-only preview. No images/thumbnails, background refresh, or network requirement for ordinary notes. |
| Organization | User-defined databases containing pages; saved views share the underlying pages. |
| Editor | Flowing document with movable, nestable blocks; not a spatial canvas. |
| Block affordance | Active block has a thin outline and subtle shadow; mouse reordering will use grip dots as its drag target. |
| Cross-referencing | Links to pages, headings, and blocks across databases in one workspace; backlinks and link-aware navigation/search. |
| Language | C++17 baseline with conservative dependencies; C libraries where useful. |
| Application license | MIT, selected by the user; dependencies retain their own licenses. |
| Interface | Qt 6 Widgets is the preferred starting point, subject to the editor feasibility milestone. |
| Storage | SQLite for structured records, with small versioned JSON payloads for flexible block content and view settings. |
| Workspace location | Ask where to create/open the workspace, suggesting `~/Document/nebula/`; keep the database and managed attachments together. |
| Attachments | Prefer managed files beside the database; evaluate SQLite BLOBs if a single-file workspace becomes a requirement. |
| Build | CMake; keep compiler and platform requirements explicit. |
| Development isolation | Existing Docker CLI with a project development image. Qt and build dependencies must not be installed system-wide on the host. |
| Modularity | UI-independent document core, application operations, storage interfaces, and separate desktop adapters. |
| Platform | Linux only; the selected package target is Debian 13 amd64. |
| Data ownership | Documented schema, migration paths, backup/restore, and portable exports. |

The following remain implementation decisions rather than settled facts:

- Desktop/input-method/accessibility coverage beyond the recorded Linux
  IBus/Anthy/Orca profile.
- Release dependency pinning and third-party distribution notices. The current
  module/API baseline is recorded in [DEPENDENCIES.md](docs/DEPENDENCIES.md).
- Final Linux release verification/version and distribution method.
- Portable performance expectations beyond the recorded reference machine;
  budgets are in [PERFORMANCE.md](docs/PERFORMANCE.md) and search behavior in
  [SEARCH.md](docs/SEARCH.md).
- Interchange behavior and limitations are documented in
  [INTERCHANGE.md](docs/INTERCHANGE.md),
  [MARKDOWN.md](docs/MARKDOWN.md) and
  [WORKSPACE_FORMAT.md](docs/WORKSPACE_FORMAT.md).

### Options considered

| Option | Reason to consider it | Tradeoff and disposition |
| --- | --- | --- |
| C++ with Qt Widgets | Rich-text facilities, desktop controls, and model/view tables. | Preferred; custom block interactions still need a prototype. |
| C++ with Qt Quick/QML | Flexible layouts and animated interfaces. | Adds QML and another interface model; a fallback if the editor investigation warrants it. |
| C with GTK 4, or C++ with gtkmm | Strong Linux foundation; suitable if C or GNOME integration becomes the priority. | Would require a different editor adapter and dependency baseline. |
| C++ with wxWidgets | Established native desktop controls. | Expected to require more custom work for this editor; keep as an alternative. |
| Markdown files with metadata | Easy external editing and inspection. | Block identity, rich formatting, database queries, and atomic changes require additional machinery. Use for interchange. |
| JSON files per page | Inspectable, flexible page representation. | Transactions, indexing, and recovery become additional systems. Use versioned JSON for lossless interchange and selected payloads. |
| SQLite | Fits a database-centered local application with transactions and indexed queries. | Selected storage direction. |

Qt currently recommends Qt Quick for new interfaces. The Widgets preference is a
project-specific choice based on C++ use and desktop text/table editing, not a
claim that Widgets is Qt's preferred new-interface technology.

### Zim-inspired document cross-referencing

[Zim Desktop Wiki](https://github.com/zim-desktop-wiki/zim-desktop-wiki) is an
additional product reference for connected notes. Its documentation describes
[page links, custom link labels, and heading/object anchors](https://zim-wiki.org/manual/Help/Links.html),
a [backlinks pane](https://zim-wiki.org/manual/Plugins/BackLinks_Pane.html), and
[incoming/outgoing link searches](https://zim-wiki.org/manual/Help/Searching.html).
Its [page navigation](https://zim-wiki.org/manual/Help/Pages.html) also supports
starting a new page from a destination that does not yet exist.

The following is Nebula's planned adaptation of those ideas, using its own block
model and keeping database views as the primary organization method:

- **Page references:** insert a link using a picker or by typing `[[Page title]]`.
  Offer completion across the workspace and show the database context when titles
  collide. Require selection of a specific target when a name is ambiguous.
- **Display labels:** allow readable custom labels, for example
  `[[Page title|reading notes]]`. A default label follows the current target title;
  an explicitly customized label stays unchanged when the target is renamed.
- **Heading and block references:** link to a precise location in the same page or
  another page. The picker can show heading text, but the persisted reference uses
  the target block's permanent ID. Resolve a moved block through its current page.
- **Backlinks and outgoing links:** show which pages reference the current page,
  include source context, and navigate to the referring block. Aggregate references
  to a page's blocks into its backlinks while allowing the exact target to be shown.
- **Navigation:** support keyboard-accessible following of references and back/
  forward history that restores the previous page position.
- **Unresolved references:** preserve intended names and any database scope, show
  an unresolved state, and offer an explicit create-or-select-target action. New
  target creation selects a destination database; merely displaying or following
  an unresolved link must not silently create a page.
- **Lifecycle:** resolved references retain their identity through renaming and
  moves within the workspace. Trashed or missing targets remain identifiable;
  restore reconnects the original target. Reusing a deleted page's title must not
  redirect existing references to the new page.
- **Link-aware queries:** provide incoming/outgoing reference filters in search
  and database views, using selected page identities rather than title matching.
- **Interchange:** preserve structured targets and labels in lossless exports.
  Markdown export maps supported links to exported filenames/anchors and reports
  targets outside the export or references the chosen format cannot preserve.

For example, a Journal entry can link to a project page in another database and
to a particular meeting-note heading. The project page then lists the journal
entry as a backlink, and renaming the project does not break either reference.

Zim is a behavioral reference; this plan keeps the selected C++/Qt/SQLite stack.
Zim notebook import, its namespace/path resolution rules, cross-workspace links,
and visual graph browsing are possible later investigations, not first-release
commitments. First-release cross-referencing works entirely within a local
workspace.

## Product and data model

| Concept | Responsibility |
| --- | --- |
| Workspace | Local collection of databases, pages, settings, and managed attachments. |
| Database | Logical collection of pages with user-defined properties; not a separate SQLite file per collection. |
| Page | Permanent identity, title, properties, and an ordered tree of blocks. |
| Block | Permanent identity, type, parent/page relationship, sibling order, content, and payload format version. |
| Property definition | Name and type of a database field. Initial types: text, number, date, checkbox, and tags/select values. |
| Property value | Typed value attached to a page; suitable for correct filtering and sorting. |
| View | Saved filters, sorting, visible properties, and presentation settings over the same pages. |
| Attachment | Managed file identity and metadata, referenced by blocks. |
| Link reference | Inline reference with a target page or block ID and optional custom label; unresolved references retain their intended name/scope separately. |
| Link index | Rebuildable incoming/outgoing edges derived from canonical inline references, with source page/block context for navigation. |

A Journal database might contain Title, Entry date, Mood, and Tags. Opening a row
opens its page. A future calendar can show those same pages without duplicating
their contents.

Document invariants:

- Page and block identifiers survive renaming and reordering.
- Internal references resolve by target identity, not by display title. Block
  targets resolve through their current owning page when moved.
- Backlinks are derived from explicit references. Rebuilding an index must not
  change targets, create documents, or bind unresolved names automatically.
- Parent relationships form a valid tree; moving a block cannot create a cycle.
- Sibling order is explicit, deterministic, and updated atomically.
- A paragraph is normally one block. Visual line wrapping never changes storage.
- Enter splits a paragraph into blocks; Shift+Enter inserts a break within a block.
- Rich-text content has an application-owned, versioned representation. Toolkit
  objects or opaque toolkit serialization are not the permanent document format.
- Journal entry date is distinct from creation/modification timestamps, allowing
  an entry to describe a different day from the day it was written.
- Saved views reference the same pages; editing through one view updates the data
  visible through other views.

## Architecture and storage

```text
Desktop interface: database views, page editor, navigation
                         |
Application operations: editing, queries, validation, undo/redo
                  /                      \
UI-independent document core       Storage interfaces
                                          |
                             SQLite + managed attachment files

Import/export adapters use the application operations and document model.
Platform adapters handle paths, dialogs, menus, shortcuts, and packaging.
```

Use separate modules/build targets within one executable initially. Runtime
plugins are not required to achieve modularity. The core must be testable without
opening a window and must not depend on Qt GUI types.

SQLite tables should hold page membership, property definitions and typed values,
block relationships/order, attachment metadata, and view definitions. Small JSON
payloads can hold rich-text runs, typed inline references, and block-specific
presentation data. Inline references are authoritative; a derived SQLite link
index supports backlinks and outgoing-link queries without scanning all note text.
Keep persisted link edges consistent with committed content changes and provide a
rebuild path. Full-text search indexes are also derived data; FTS5 is the starting
candidate. Link queries operate on structured references rather than text matches.

The preferred workspace layout contains the database and an `attachments/`
directory. Import copies files into managed storage so moving the original file
does not break the note. Complete the file write before committing its reference;
interrupted imports may leave orphan files that can be reconciled safely.

Startup asks for the workspace folder and suggests `~/Document/nebula/` (singular
`Document`, matching the requested default). The user can browse, type a new path,
or cancel without creating it. The Docker launcher selects a host folder in a
separate chooser container with the home directory mounted read-only, then mounts
only the selected data folder writable at `/data` in the editor container. An
explicit `--workspace HOST_FOLDER` bypasses the chooser and can select folders
outside the home directory. The host path remains visible in the window.

The original version 1 bootstrap format was deliberately small: `nebula.sqlite3`,
SQLite `application_id = 0x4e454255` (`NEBU`), `user_version = 1`, and
`workspace_meta(id INTEGER PRIMARY KEY CHECK(id=1), workspace_id TEXT NOT NULL
UNIQUE)`. Its single row contains a generated 32-character hexadecimal identity.
Initialization occurs in a transaction; reopening preserves that identity. Unrelated
databases and unsupported versions are rejected without modifying their contents.
**Version 1 stores no pages or notes.** The current version 10 format adds persistent
databases, pages, block payloads, properties, views, derived link edges, search rows,
managed attachments, and deleted-block recovery.
[WORKSPACE_FORMAT.md](docs/WORKSPACE_FORMAT.md) documents the schema, JSON versions,
ownership, autosave/undo boundaries, and backup contract. The prototype notice
applies only to explicit in-memory demo mode.

Application operations must give related edits coherent validation and undo
semantics. Autosave needs explicit saved/unsaved/error states. A change is saved
only after persistence succeeds; the durability and pending-edit window must be
documented and tested.

Backups must capture a consistent database snapshot and all referenced attachment
files. Use SQLite's backup facilities rather than assuming a live database file
copy is a complete backup. Schema changes require versioned migrations and
recovery/restore verification.

## First-release deliverables

- Database tables with editable typed properties, filtering, sorting, and saved
  views; selecting an entry opens its page.
- Paragraphs, headings, lists, checkboxes, links, images, attachments, and dividers.
- Block reordering and basic nesting, natural text selection/navigation, clipboard
  operations, inline formatting, and coherent undo/redo.
- Wiki-style page/heading/block references, target completion, custom labels,
  backlinks/outgoing links, navigation history, and unresolved-target handling.
- Journal templates and an explicit entry-date property.
- Autosave, trash/restoration, backup, and restore.
- Full-text search, Markdown interchange, and a versioned lossless JSON export
  with attachment and internal-reference handling; incoming/outgoing link filters
  in search and saved database views.
- A documented, installable Linux release that works offline.

Calendar and other database presentations are possible later extensions. Advanced
relations, formulas, and broader Notion feature parity are not first-release
commitments. Dragging files or images into the editor and translating the user
interface are deferred until after the first release (SUG-43). A spatial canvas,
synchronization, shared workspaces, and a hosted service are outside the agreed
product scope.

## Progress conventions

Milestone states: **Not started**, **In progress**, **Blocked**, **Complete**.
Use task IDs and checkboxes for individual steps. A milestone is complete only
when every required step and its exit criteria have supporting verification.
Record partial progress explicitly; do not infer completion from code existing or
from a proposed design. An in-memory editor prototype and test/build foundation
now exist, with a workspace chooser and persistent SQLite database-to-page workflow.
Content persistence, autosave, typed properties, and saved table views are
implemented, including the interactive wiki workflow and managed attachments.
Local search, structured reference queries, Markdown conversion and lossless JSON
interchange and pasted-link previews are implemented. Release validation remains
unfinished.

Update the relevant milestone and work log in the same change as implementation.
Record actual defects in the bug log, anticipated problems in the risk register,
and learned constraints in the gotcha log. Preserve the history of resolved bugs
and changed decisions. Avoid unsupported completion percentages.
Keep progress and verification updates here; `README.md` is for setup and usage,
including material data-handling limitations, rather than a running progress log.

| ID | Milestone | Dependencies | Status |
| --- | --- | --- | --- |
| M00 | Project definition and working conventions | None | Complete |
| M01 | Editor feasibility and architecture decisions | M00 | Complete |
| M02 | Modular application foundation | M01 | Complete |
| M03 | Persistent workspace and recovery foundations | M02 | Complete |
| M04 | Database-to-page workflow | M03 | Complete |
| M05 | Complete first-release block editing | M01, M04 | Complete |
| M06 | Attachments and diary workflow | M03, M05 | Complete |
| M07 | Search and portable interchange | M04, M06 | Complete |
| M08 | Reliability, accessibility, and performance | M04–M07 | Complete |
| M09 | Linux release | M08 | In progress |
| M10 | Thorough code audit and simplification | M00–M09 complete | Not started |

### M00 — Project definition and working conventions

Deliverables: this roadmap and repository management instructions in `AGENTS.md`.

- [x] M00.1 Record the project goal, local-only scope, and Linux-only direction.
- [x] M00.2 Capture the preferred stack, alternatives, and unresolved decisions.
- [x] M00.3 Define milestones, deliverables, exit criteria, and tracking conventions.
- [x] M00.4 Establish project management and Git authorship conventions.

Exit criteria: documents agree with the discussion and clearly distinguish plans
from completed implementation.

### M01 — Editor feasibility and architecture decisions

Deliverables: a runnable editor prototype, recorded comparison of editor
approaches, and a decision describing the selected representation and interaction
model. Prototype code is investigative and is not automatically production-ready.

- [x] M01.1 Compare a page-wide `QTextDocument` approach with separate block
  components using the same representative editing scenarios.
- [x] M01.2 Demonstrate paragraph splitting/merging, cross-block selection,
  navigation, formatting, inline references, clipboard operations, reordering,
  and undo/redo.
- [x] M01.3 Demonstrate nesting and an image/attachment representation alongside
  text; investigate how embedded controls participate in selection and focus.
- [x] M01.4 Verify Unicode, synthetic input-method composition, keyboard access,
  font fallback, and the accessible text interface through automated real-toolkit
  checks.
- [x] M01.5 Exercise a long document; define reproducible fixtures and measurable
  interaction/memory thresholds for subsequent regression checks.
- [x] M01.6 Record the chosen editor approach, model-to-widget mapping, undo
  ownership, limitations, and reasons for accepting or revisiting Qt Widgets.
- [x] M01.7 Resolve the application license and initial toolkit/module baseline;
  record remaining platform constraints explicitly.

Exit criteria: the selected design demonstrates the essential editing interactions
without a known fundamental selection, input-method, or undo limitation. Remaining
issues have concrete follow-up tasks or bug entries. Record validation conditions
and results instead of judging feasibility from screenshots alone.

Completion evidence: `nebula_prototype` contains both editor approaches, backed by
core and Qt interaction tests. [EDITOR_FEASIBILITY.md](docs/EDITOR_FEASIBILITY.md)
records the provisional page-wide editor decision, measured fixture/budgets,
regressions, and remaining validation. Automated checks cover Unicode, synthetic
composition, keyboard/mouse interaction, font fallback, and accessible text.
The application uses MIT and a Docker development environment with isolated Qt
dependencies.

### M02 — Modular application foundation

Deliverables: a buildable desktop shell, core/application/storage module boundaries,
and a repeatable developer workflow.

- [x] M02.1 Establish CMake targets and the C++17 baseline; document dependencies
  and supported Linux compiler configurations.
- [x] M02.2 Implement the initial UI-independent document types and invariants,
  including permanent IDs, typed references, hierarchy validation, and deterministic
  ordering.
- [x] M02.3 Define application operations, storage interfaces, and ownership of
  undo/redo without duplicating editing rules in individual views.
- [x] M02.4 Add workspace navigation, database/page placeholders, and platform
  adapters for paths, dialogs, menus, and shortcuts.
- [x] M02.5 Establish meaningful automated core tests and documented build/check
  commands; exercise both GCC and Clang where available.
- [x] M02.6 Ask for a workspace location at startup, suggest `~/Document/nebula/`,
  support browse/type/cancel and an explicit path, and preserve the selected host
  location through Docker launches. Test full-window focus and block controls.

Exit criteria: a fresh checkout builds and launches on the chosen Linux baseline;
core tests run without a GUI, and platform assumptions are documented.

Completion evidence: GCC/Clang Docker builds, Qt-free core tests, native window
interaction, workspace navigation, and application/storage ownership pass their
checks. The normal window contains one block editor; the comparison baseline
requires `--compare-editors`. Full-window tests cover toolbar clicks, shortcut
routing, Enter, nesting feedback, and the active block outline.
[README.md](README.md) documents the Docker build/test and GUI commands.

### M03 — Persistent workspace and recovery foundations

Deliverables: versioned SQLite storage, workspace lifecycle, persistence of core
records, and an initial backup/restore path.

- [x] M03.1 Define and document tables, relationships, typed property storage,
  rich-text/reference payload versions, derived link edges, and schema versioning.
- [x] M03.2 Create/open a versioned workspace identity database and attachments
  directory at the chosen location. Test reopening, process restart, unsupported
  versions, unrelated files/databases, invalid attachment paths, and read-only
  errors.
- [x] M03.3 Implement create/open/close operations, storage adapters, transactions,
  and validation of persisted document relationships.
- [x] M03.4 Implement versioned migrations and safe behavior for unsupported newer
  workspace formats; verify preservation of existing data.
- [x] M03.5 Implement autosave with visible persistence state and actionable write
  errors; define commit/durability behavior and the pending-edit window.
- [x] M03.6 Persist resolved and unresolved inline references; maintain a rebuildable
  index of source/target page and block relationships. Verify consistency across
  commits, undo/redo, trash, and restoration without resolving targets by title.
- [x] M03.7 Implement trash and restore semantics for pages/blocks, including how
  links and attachment references behave.
- [x] M03.8 Implement database snapshot backup and restore; reserve the manifest
  and attachment inclusion mechanism completed in M06.
- [x] M03.9 Verify interrupted operations, reopening, rollback, migration, and
  backup/restore using representative persisted fixtures.

Current implementation: version 7 stores application-owned block JSON and typed
properties, with deferred same-page parent foreign keys, global block IDs,
transactional page/index replacement, and optimistic revisions. `PageSession`
owns pending values and save failures; the Qt adapter owns per-page undo. Autosave
waits 500 ms and flushes before navigation/close. Tests exercise failed writes,
revision conflicts, killed writers before/after commit, migration rollback, and
read-only-source backup/restore. Deleted-block fragments retain content, IDs,
hierarchy, and sibling order across restart; restoration preserves already returned
edits. Unresolved references retain their intended names and database scope without
binding by title. GCC/Clang tests verify migrations from versions 1–3, recovery,
and index rebuilding. Native window tests verify the restore and link controls.
Managed attachment ownership and verified manifest backup/restore are documented
in [ATTACHMENTS.md](docs/ATTACHMENTS.md).

Exit criteria: supported records survive closing/reopening; failed operations do
not leave partial logical edits; migrations and database backup/restore preserve
the tested data. Tests distinguish pending edits from confirmed saved data. Link
index rebuilds reproduce the references in persisted content.

### M04 — Database-to-page workflow

Deliverables: the first complete organization workflow, from a database table to
an editable page and back.

- [x] M04.1 Create, rename, and remove logical databases and their pages using the
  established trash policy.
- [x] M04.2 Add editable text, number, date, checkbox, and tags/select properties,
  with correct validation and type-aware ordering.
- [x] M04.3 Build the table model/view interface, visible-column settings, filters,
  sorting, and persistence of saved views.
- [x] M04.4 Open a row as its page, edit basic text through application operations,
  and keep page properties and database views consistent.
- [x] M04.5 Verify that multiple saved views share data and retain their settings
  across restart; changing presentation must not duplicate content.

Exit criteria: create a Journal database, add properties and entries, edit a page,
filter/sort the table, save a view, restart, and recover the same data and view.

Completion evidence: GCC and Clang offscreen suites and native Xwayland tests pass
for all five steps, including the actual view dialog, database/trash controls,
typed table edits, multiple views over shared IDs, navigation, autosave, and reopen.
Storage tests reopen saved numeric filters and sorting and verify page identities.
The native workspace window was also inspected.

### M05 — Complete first-release block editing

Deliverables: the production block editor implementing the approach chosen in M01.

- [x] M05.1 Support paragraphs, headings, lists, checkboxes, links, and dividers,
  including a practical way to insert blocks and change their types.
- [x] M05.2 Implement Enter/Shift+Enter behavior, split/merge operations, inline
  formatting, and natural cursor movement across block boundaries.
- [x] M05.3 Implement cross-block selection, copy/cut/paste, and defined handling
  of plain text and supported rich-text clipboard content.
- [x] M05.4 Implement drag reordering, keyboard-accessible move operations, and
  basic nesting while preserving IDs and preventing invalid trees. Keep the active
  block's thin outline/subtle shadow and add grip dots as the mouse drag target;
  verify dragging a subtree, nesting, canceled drops, undo, and keyboard access.
- [x] M05.5 Integrate consistent undo/redo for text and structural edits with
  autosave; document undo boundaries and whether history survives restart.
- [x] M05.6 Implement page/block link resolution using permanent IDs and predictable
  behavior when a target is in trash or unavailable. Preserve targets across page
  renames, database moves, heading edits, and block moves between pages.
- [x] M05.7 Recheck Unicode, synthetic composition, accessible text, and the
  long-document scenarios from M01 against the production editor.
- [x] M05.8 Add wiki-style link entry, completion and a target picker for pages,
  headings, and blocks, with duplicate-title disambiguation and custom labels.
- [x] M05.9 Add backlinks/outgoing-link navigation with source context and exact
  block destinations; provide keyboard access and back/forward position history.
- [x] M05.10 Surface unresolved references and explicit create-or-select-target
  actions, including the destination database for a new page. Keep default and
  custom label behavior consistent through renames and undo/redo.

Exit criteria: the M01 interaction scenarios pass with persisted documents. Text
wrapping does not alter block structure; structural edits preserve identity and
can be undone coherently. A journal page can reference a page and heading in
another database, navigate to them and back, and expose the corresponding backlink.
Renaming or moving targets preserves references; ambiguous/unresolved references
require an explicit target choice. Missing first-release behavior is tracked
explicitly.

Implementation evidence: divider insertion, empty-list exit, split/merge, formatting,
rich clipboard, structural movement, typing groups, and undo after autosave pass
GCC/Clang and native window tests. The shared format module preserves canonical
values without linking the editor to SQLite. [CLIPBOARD_FORMAT.md](docs/CLIPBOARD_FORMAT.md)
defines copied identities, reference remapping, and the supported HTML subset.
Direct ID resolution, page moves with typed property preservation, and atomic
cross-page subtree transfers have storage/full-window lifecycle and undo tests.
Wiki entry/completion and searchable explicit target selection cover page,
heading/block, duplicate-title, custom-label, escaping, Unicode, and undo cases.
Incoming/outgoing source-context navigation, keyboard following, and stable
position/selection/scroll back/forward history pass storage/editor/window checks.
Default/custom label modes, same-page title/heading undo, explicit unresolved
selection/creation, and transactional source binding have focused regression
coverage. [LINKS.md](docs/LINKS.md) defines entry, labels and navigation behavior.

### M06 — Attachments and diary workflow

Deliverables: image/file blocks, reliable managed attachments, complete workspace
backup, and a useful daily journal workflow.

- [x] M06.1 Implement managed file import, attachment metadata, image display, and
  file attachment blocks; removing the source file must not break imported data.
- [x] M06.2 Handle interrupted imports, missing files, and orphan detection;
  cleanup must account for live documents, trash, and active backups.
- [x] M06.3 Complete database-plus-attachment backup/restore with a consistent
  manifest and verification of referenced files.
- [x] M06.4 Add a Journal template with Title, Entry date, Mood, and Tags, plus a
  convenient way to create an entry for a chosen day.
- [x] M06.5 Keep date-only entry values distinct from timestamps and verify diary
  behavior around date/timezone changes.

Exit criteria: create a dated entry with an image and file, move/remove the original
source files, restart, and restore the workspace into a fresh location. The entry,
properties, blocks, and attachments remain usable.

Completion evidence: managed import/preview/copy, file availability and checksum
audit, conservative undo/recovery retention, locked manifest backup/restore, and
dated journal restoration pass GCC/Clang and native-window tests. Schema 7 and block
payload 3 preserve prior notes/cards/recovery through migration. Registered imports
remain retained; cleanup removes unregistered leftovers only. Contracts and
data-handling limits are in [ATTACHMENTS.md](docs/ATTACHMENTS.md).

### M07 — Search and portable interchange

Deliverables: searchable notes and documented export/import formats.

- [x] M07.1 Index appropriate page text and properties using FTS5; keep the index
  consistent with edits and trash changes and provide a rebuild path.
- [x] M07.2 Define and test search/tokenization behavior for supported languages;
  search results should navigate to the relevant page or block.
- [x] M07.3 Implement a documented Markdown import/export subset with explicit
  handling of unsupported formatting, attachment references, internal link paths/
  anchors, and targets outside the exported set.
- [x] M07.4 Implement versioned JSON export/import that preserves the supported
  model, including IDs, properties, blocks, views, internal reference targets/
  labels/unresolved states, and attachment references/files.
- [x] M07.5 Define import validation and identity-collision behavior; malformed or
  partially supported input must not silently overwrite existing data. Remap
  internal references consistently if imported identities must change.
- [x] M07.6 Verify semantic round trips through the lossless format and test the
  documented Markdown limitations with representative fixtures.
- [x] M07.7 Add incoming/outgoing link filters to search and saved database views,
  including references to blocks on a selected page; verify structured link queries
  independently of full-text matches.
- [x] M07.8 When pasting a web link, try to extract a text-only preview that the
  user can remove. Fetch text only as part of that paste action; do not import
  pictures or thumbnails. Keep the pasted link usable when extraction fails and
  verify removal, save/reopen, clipboard and undo/redo behavior.

Exit criteria: edits and deletions appear correctly in search; rebuilding produces
equivalent results. A full export imported into a fresh workspace preserves the
supported model, reference relationships, and attachments, with equivalent backlinks
after rebuilding derived indexes. Markdown limitations are visible and documented.
Pasted web links can produce a removable text-only preview without importing
images, while failed extraction leaves the pasted link usable.

### M08 — Reliability, accessibility, and performance

Deliverables: verified recovery behavior, interaction checks, benchmark results,
and a release candidate with known limitations documented.

- [x] M08.1 Exercise crash/interruption scenarios, failed writes, migration errors,
  trash restoration, and backup restoration across the complete workflow.
- [x] M08.2 Recheck keyboard navigation, focus, screen-reader semantics, clipboard,
  Unicode, and input-method composition in the integrated application. Use an
  actual OS IME to check candidate selection, composition updates, commit/cancel,
  and Enter/cursor/selection/undo at block boundaries. Check screen-reader
  navigation/selection for headings, lists, to-do
  state, references, and asset labels. Record IME, reader, desktop, Qt, and results.
- [x] M08.3 Benchmark long pages and large databases against the thresholds agreed
  in M01; investigate bottlenecks using measured evidence.
- [x] M08.4 Verify storage invariants and regression coverage for actual defects;
  check that attachments and derived indexes remain consistent.
- [x] M08.5 Document the workspace format, recovery procedure, export limitations,
  supported environments, and unresolved release issues.
- [x] M08.6 Verify references/backlinks through duplicate titles, renames, moves,
  heading changes, target trash/restore, reuse of deleted titles, undo/redo, backup
  restore, import identity remapping, and derived-index rebuilding.
- [x] M08.7 Clarify recoverable trash at block/subtree, page and whole-database
  scope. Give each scope its own small red trash-icon button with an accessible
  name and clear confirmation; require an unchecked acknowledgement checkbox
  before confirming database trash. Default to Cancel, preserve pending edits on
  failure, retain attachments, and verify cancel/Escape, restore, block undo/redo
  and restart behavior. These buttons must never invoke permanent purge. Rename
  the visible reference lists to “Referenced by:” and “References”.
- [x] M08.8 Build the Link… picker incrementally from cached link-target rows,
  instead of creating a tree item for every block (SUG-28).
- [x] M08.9 Move view editing and import/export coordination out of
  `WorkspaceWindow` into Qt-free application services with focused tests (SUG-34).
- [x] M08.10 Share one set of Qt UTF-8 conversion helpers across storage, format,
  interchange and desktop modules (SUG-36).
- [x] M08.11 Rebuild the development image with `clang-format` and the Clang
  libFuzzer runtime, run the coverage-guided libFuzzer targets and record results,
  then reformat existing dense files in formatting-only commits (SUG-38/SUG-40).
- [x] M08.12 Confirm the GitHub CI workflow passes on a real runner and record
  which source revision it verified (SUG-40).
- [x] M08.13 Measure GUI autosaves against concurrent headless backups/exports
  of a large workspace; record whether chunked backup steps avoid save failures
  (SUG-22). Integrated benchmark budgets belong to M08.3.

Exit criteria: required checks have recorded outcomes; supported saved data
survives the documented recovery scenarios; no unresolved release-blocking defect
remains. Performance meets the agreed thresholds on recorded hardware/fixtures.

Completion evidence: the recorded reliability/performance checks plus real Linux
IBus/Anthy composition, Orca speech/selection through F8 block details, full native
interaction suites and controlled Xvfb/Xwayland focus stress. BUG-023–BUG-025 have
focused regressions; detailed environments, results and coverage limits are in
the final M08 work-log checkpoint.

### M09 — Linux release

Deliverables: an installable Linux package, release notes, and user/developer
instructions matching the shipped behavior.

- [x] M09.1 Pin the development/release base image by digest (`BASE_IMAGE` in
  `Dockerfile.dev`) with a package snapshot; ship only the Qt image plugins Nebula
  needs and add Qt image-plugin security updates to the release checklist (from
  SUG-08/SUG-42). Select and document a packaging approach, minimum supported
  environment, dependency versions, and required license notices.
- [x] M09.2 Validate installation, launch, workspace creation, editing, search,
  export, backup, and restore in a clean supported environment without network.
- [x] M09.3 Verify upgrade behavior and provide build, installation, data-location,
  backup, and recovery instructions.
- [ ] M09.4 Record release verification, known limitations, and the release version.

Exit criteria: a reproducible package installs and runs the agreed offline workflow
on the documented Linux target. Release documentation and the roadmap reflect the
actual delivered features and remaining issues.

### M10 — Thorough code audit and simplification

Deliverables: a recorded audit of the completed application and a simpler,
well-organized implementation using the
[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html).
Begin only after all M00–M09 milestones and their exit criteria are complete.

- [ ] M10.1 Capture the audited source revision and baseline evidence: Linux
  GCC/Clang/sanitizer builds/checks, recovery and interchange fixtures, desktop
  workflows and the established performance measurements.
- [ ] M10.2 Audit the complete codebase, tests, build/dependency configuration and
  packaging for correctness, data safety, security, ownership/lifetime errors,
  platform assumptions, duplicated rules, unnecessary abstraction and complexity.
  Publish prioritized findings with code references, impact and reproducible
  evidence; give confirmed defects stable BUG IDs and regression tests.
- [ ] M10.3 Define and document the Google C++ style baseline for naming, headers,
  includes, class/function organization, ownership, comments and formatting.
  Record narrow, justified exceptions for Qt integration, C++17 compatibility or
  established error/data contracts; do not change those contracts implicitly.
  Configure the existing formatter and appropriate isolated automated checks.
- [ ] M10.4 Address audit defects and simplify modules in small reviewable changes:
  reduce duplication and long functions, clarify interfaces and ownership, and
  remove unnecessary indirection while preserving offline scope, stable IDs,
  persistent formats, transactions, recovery and observable behavior. Separate
  mechanical formatting/renaming from semantic fixes/refactoring in Git history.
- [ ] M10.5 Re-run applicable compiler, sanitizer, parser, Linux desktop, storage,
  migration, backup/restore, interchange and package checks.
  Compare representative performance with the recorded baseline, account for
  every audit finding, and document remaining limitations and final evidence.

Exit criteria: the thorough audit is reviewable, findings have verified fixes or
explicitly documented dispositions, the code follows the recorded Google C++
style baseline, and simplification preserves tested behavior/data compatibility
without unexplained performance regressions on the supported Linux environments.

Progress from the Suggestions implementation (2026-10-30), before the subsequent M08
verification checkpoint: integrated benchmarks over representative fixtures now exist for M08.3
(measured values and proposed budgets are in the work log, not yet agreed gates);
sanitizer, fuzz and stress modes support M08.1/M08.4; Empty trash, schema hardening
and conflict recovery support M08.1. Each M08 task stays open until its own exit
evidence is recorded.

## Risk register

These are anticipated risks, not encountered bugs.

| ID | Risk | Resolution or investigation | Status |
| --- | --- | --- | --- |
| R01 | Native editor composition may make selection, focus, or undo inconsistent across blocks. | Compare approaches and validate interactions in M01 before committing to the production editor. | Open |
| R02 | Toolkit representation may lose application block identity or formatting. | Own the persistent schema and verify model/editor and export round trips. | Open |
| R03 | Attachment files and database references may diverge after interruption or backup. | Define import ordering, reconciliation, retention, and a consistent backup manifest. | Open |
| R04 | Search tokenization may be unsuitable for some writing systems. | Define supported behavior and test representative language fixtures in M07. | Open |
| R05 | Large documents or databases may expose rendering/query bottlenecks. | Establish workloads and thresholds in M01; measure again in M08. | Open |
| R06 | Distribution and desktop differences may invalidate Linux release assumptions. | Record the supported distribution, dependencies and desktop environment; validate filesystem, Qt/TLS and package behavior on that target. | Open |
| R07 | Dependency licensing or platform requirements may constrain distribution. | Resolve the license/module baseline in M01 and validate packaging requirements before release. | Open |
| R08 | Autosave or migration failures may lose data or falsely report that it was saved. | Define durability semantics, expose failures, and test recovery and migrations. | Open |
| R09 | References may be redirected by duplicate/reused titles, or backlinks may become stale after edits/imports. | Use permanent target IDs, explicit unresolved states, a rebuildable derived link index, and lifecycle/round-trip checks. | Open |

## Bug log

The following defects were observed during prototype implementation. Anticipated
problems remain in the risk register until reproduced.

### BUG-001 — Splitting a parent detached its children

- **Milestone/state/impact:** M01; fixed and verified. A structural edit flattened
  descendants, changing the intended hierarchy.
- **Environment/reproduction:** Qt 6.8.3, RHEL 9.8; open the sample, place the cursor
  inside block `list`, and press Enter. The subsequent `child` block became a root.
- **Expected:** the trailing split receives a new ID and retains the existing
  children; undo restores the original tree and identities.
- **Cause:** inserting a new sibling closed the original ancestor in preorder;
  generic normalization then promoted the children to the root.
- **Resolution:** `PageEditor::split_block` reparents direct children to the trailing
  block within the same undo group; deeper descendants retain their relationships.
- **Verification:** `splitting_parent_keeps_children_with_trailing_block` first
  reproduced the failure and then passed after the fix. The surrounding core and
  interaction suites also passed. The regression remains in `tests/editor_tests.cpp`.

### BUG-002 — Development image lacked sample-script fonts

- **Milestone/state/impact:** M01/M02; fixed and verified. Japanese text and the
  sample emoji appeared as missing-glyph boxes in the Docker GUI preview.
- **Environment/reproduction:** Debian 13 development image with Qt 6.8.2 and only
  DejaVu fonts; launch the sample through Xwayland.
- **Expected:** sample Japanese, Arabic, and emoji characters render using fallback
  fonts while their stored UTF-8 text remains unchanged.
- **Cause:** the image lacked CJK and emoji font coverage; passing text round-trip
  tests did not establish rendering coverage.
- **Resolution:** install Noto CJK and Noto Color Emoji inside the development
  image, with a UTF-8 locale. No host fonts or Qt packages are installed.
- **Verification:** `container_font_baseline_renders_sample_scripts` reproduced the
  missing Japanese glyph and is mandatory in the image. It is explicitly skipped
  outside that configured font baseline. Native preview and suite results are
  recorded in the feasibility report.

### BUG-003 — Undo of a structural rewrite moved focus to the first block

- **Milestone/state/impact:** M01; fixed and verified. After converting a later
  block's type and undoing, the next command could affect the first block instead.
- **Environment/reproduction:** Qt 6.8.2 in the Docker image; focus the sample's
  `reference` block, convert it to a heading, undo, and convert it to a bullet.
- **Expected:** undo restores the edited block and the caret remains there, so the
  subsequent command still acts on `reference`.
- **Cause:** structural rewrite/normalization groups began with a fresh document
  cursor at position zero instead of the user's current cursor.
- **Resolution:** begin those edit groups with `textCursor()`, preserving the
  native undo engine's starting position.
- **Verification:** `block_kind_conversion_preserves_content_and_references`
  reproduced the wrong target, then passed with an explicit focus-restoration
  assertion after the fix. Compiler/native suite results are in the report.

### BUG-004 — Reported block controls behaved like plain text editing

- **Milestone/state/impact:** M01/M02; report investigated, exact prior-session
  cause unconfirmed. The user reported Alt+arrows scrolling, Tab inserting spaces,
  and Enter not creating application blocks after launching with `dev.sh`.
- **Environment/reproduction:** user launched the Docker prototype on Linux;
  focused tab and binary revision were not established. Expected controls move,
  nest, and split blocks. The complete reported failure has not been independently
  reproduced in the page-wide editor.
- **Diagnosis:** the old normal launch exposed a separate-widget comparison tab
  with native text behavior, and `run` did not rebuild stale executables. Exact
  modifier equality also excluded keypad arrow events. These are confirmed paths
  to confusing behavior, not proof of which affected the user's session.
- **Resolution:** normal launch now shows only the block editor; comparison mode
  is explicit and disables block controls. `run` rebuilds automatically. Block
  position/depth are visible, unavailable actions are disabled, toolbar operations
  restore editor focus, and keypad modifier bits do not suppress structural keys.
- **Verification:** full-window tests pass for toolbar movement/nesting, focus,
  Alt+Down, Tab/Shift+Tab, Enter increasing the block count, keypad-modified keys,
  boundary actions, and explicit comparison mode on offscreen and Xwayland Qt.
  The original session's exact cause remains unconfirmed; mouse drag is planned
  in M05.4 and was never implemented in this prototype.

### BUG-005 — Initial active-block outline was shorter than the text

- **Milestone/state/impact:** M01/M02; fixed and verified during implementation.
  The outline's lower edge crossed the first heading instead of enclosing it.
- **Environment/reproduction:** Qt 6.8.2, Xwayland screenshot of the default sample.
  Expected a thin outline around the whole active block, including its text height.
- **Cause:** `blockBoundingRect()` already excluded the bottom inter-block margin;
  subtracting that margin again made the outline ten pixels too short.
- **Resolution:** use the layout's full block height with a small outside padding.
- **Verification:** the heading outline must contain the cursor rectangle; wrapped
  paragraphs, moving the outline, painted edge pixels, and scrolling are covered
  by desktop tests. The native preview was inspected as well.

### BUG-006 — Workspace bootstrap could misclassify an unrelated database

- **Milestone/state/impact:** M03; fixed and verified during implementation.
  An unrelated database with only a user table named `sqliteNotes` was treated as
  empty and gained Nebula metadata instead of being rejected.
- **Environment/reproduction:** SQLite 3.46.1 in the development image; put that
  table and a record in `nebula.sqlite3`, then open the folder as a workspace.
  Expected rejection with byte-for-byte preservation of the original file.
- **Cause:** filtering internal names with SQL `LIKE 'sqlite_%'` treated `_` as a
  wildcard and incorrectly excluded a legal user table without the reserved prefix.
- **Resolution:** count every existing schema object before initializing a blank
  database, avoiding name-based exclusions entirely.
- **Verification:** `unrelated_database_is_not_modified` first failed with this
  fixture, then passed after the fix, checking rejection and unchanged file bytes.

### BUG-007 — Restoring deleted siblings in reverse order reordered them

- **Milestone/state/impact:** M03; fixed and verified with GCC. Recovering B before
  A from the original sequence A, B, C produced B, A, C.
- **Environment/reproduction:** Qt 6.8.2 / SQLite 3.46.1 in Docker; save removal
  of sibling blocks A and B, then restore B followed by A from deleted-block trash.
  Expected the original A, B, C order with unchanged identities and content.
- **Cause:** both fragments used surviving block C as their only insertion anchor.
- **Resolution:** schema version 4 stores a shared deletion operation identity and
  original positions; restoration prefers a later recovered sibling as its anchor.
  A partially recovered sibling also anchors insertion while its children remain
  in trash, preserving edits made after a partial undo.
- **Verification:** `restoring_siblings_in_reverse_order_keeps_their_order`
  reproduced the defect and passes after the fix. The partial-undo case also
  failed before broadening the anchor lookup. Final checks are recorded below.

### BUG-008 — Qt's lazy clipboard object hid Nebula's block payload

- **Milestone/state/impact:** M05; fixed and verified. Copy/paste fell back to HTML,
  losing structured references and hierarchy even though custom data had been set.
- **Environment/reproduction:** Qt 6.8.2 in Docker; select the sample document,
  copy it, and inspect clipboard formats or paste into another page. Expected the
  Nebula MIME type and preserved internal/unresolved references.
- **Cause:** Qt's lazy text MIME subclass supplies its own `formats()` behavior,
  hiding data added directly to that object.
- **Resolution:** materialize native formats into an ordinary `QMimeData`, then add
  the versioned Nebula payload. New IDs and copied-target remapping remain explicit.
- **Verification:** rich clipboard hierarchy and unresolved-reference cases failed
  before the fix and now pass with GCC, Clang, and native Xwayland. The full-window
  copy/paste/save/undo/redo regression also passes.

### BUG-009 — Exiting an empty list retained the native list marker

- **Milestone/state/impact:** M05; fixed and verified during implementation.
- **Environment/reproduction:** Qt 6.8.2; press Enter in an empty checked to-do.
  Expected a paragraph with the same ID and no list; the kind changed but the
  native block still belonged to its list.
- **Cause:** the saved block format reintroduced Qt's list object index after
  removing the block from that list.
- **Resolution:** clear the object index before applying the paragraph format.
  Empty-list exit is specific to Enter; the shared splitter still preserves
  paragraph separators during paste and card insertion.
- **Verification:** `empty_list_enter_exits_without_an_extra_block` failed before
  the fix and passes with GCC, Clang, and Xwayland, including undo of checked state.

### BUG-010 — Move dialogs left keyboard shortcuts without an active window

- **Milestone/state/impact:** M05.6; fixed during implementation. After canceling
  the block-transfer dialog, the next workspace shortcut did not run. Accepting a
  page move likewise left editor focus null.
- **Environment/reproduction:** Debian 13 Docker image, Qt 6.8.2, Linux offscreen
  backend; open/cancel Move block to page…, then press Ctrl+Shift+M. The full-window
  test observed no active window even after waiting for activation.
- **Expected:** returning from either move dialog restores the workspace window
  and editor focus, including the cancel path.
- **Cause/resolution:** the dialog's modal event loop did not restore parent
  activation. Both move commands now explicitly activate the workspace window and
  focus its editor after the dialog returns; tests wait for toolkit activation.
- **Verification:** the cancel/shortcut and page-move focus tests failed before
  the change and pass in the GCC suite. Final compiler/native checks are recorded
  in the execution checkpoint below.

### BUG-011 — Shortcut modifiers and cursor movement triggered false autosaves

- **Milestone/state/impact:** M05; fixed during implementation. Workspace transfer
  undo rejected an unchanged pair of pages as stale because its own modifier
  keystrokes triggered a save.
- **Environment/reproduction:** Debian 13 Docker image, Qt 6.8.2; transfer a block,
  follow its reference, and press Ctrl+Alt+Z. A focused editor test also presses
  Control/Alt without entering text: it observed two `textChanged` emissions.
- **Expected:** modifier keys, ordinary navigation, selection, and copy leave the
  saved content/revision and edit history unchanged; structural Alt+arrows remain
  undoable edits.
- **Cause:** every ordinary key entered `mutate`; even an empty QTextCursor edit
  group emitted `textChanged`, causing the desktop to mark the page pending.
- **Resolution:** dispatch modifiers, cursor/selection navigation, copy, and
  select-all directly to QTextEdit without an edit group. Preserve typing-group
  boundaries and the existing structural shortcuts.
- **Verification:** the focused no-edit test failed before the fix (two signals)
  and passes afterward. The full-window transfer/undo/redo shortcut case also
  passes; final compiler/native results are recorded below.

### BUG-012 — Keyboard following missed a just-inserted reference

- **Milestone/state/impact:** M05.9/M05.10; fixed during unresolved-target work.
  Ctrl+Enter immediately after typing an unresolved link did not open its actions.
- **Environment/reproduction:** Debian 13 development image, Qt 6.8.2 offscreen;
  type `[[FutureName]]` and press Ctrl+Enter at the end of the inserted reference.
  The full-window create-target test observed that it remained unresolved.
- **Expected:** keyboard following recognizes the reference immediately before
  the caret, including after insertion has cleared the typing format.
- **Cause:** at a block's end, lookup reused the cursor's cleared insertion
  format rather than inspecting the preceding document character.
- **Resolution:** inspect the preceding character when the current/next format
  has no reference, staying within the current block.
- **Verification:** the full-window typed-reference/create flow failed before
  the fix and passes after it. The focused keyboard test now also follows a link
  immediately after insertion; final compiler/native results are recorded below.

### BUG-013 — Cleanup could remove bytes still needed by editor undo

- **Milestone/state/impact:** M06; fixed during managed-file implementation.
  Deleting a card's object while retaining the block ID made its imported file
  appear unused even though the editor could still undo that deletion.
- **Environment/reproduction:** Debian 13 development image, Qt 6.8.2/SQLite
  3.46.1; import a file, remove its reference without removing the block, save, then
  ask cleanup to remove the file. The focused test observed deletion succeeding.
- **Expected:** cleanup must retain bytes that may still be restored by undo.
- **Cause:** deleted-block recovery records removed block identities, not every
  object/text edit inside a surviving block. Current canonical references alone
  therefore did not represent all in-memory retention needs.
- **Resolution:** registered imports are retained conservatively and included in
  backups even when unreferenced. Manual cleanup removes only unregistered
  interrupted-import leftovers after rechecking references and file locks. A
  permanent registered-file purge policy is not claimed.
- **Verification:** `registered_files_removed_inside_a_block_remain_available_for_undo`
  failed before the retention change and passes afterward, including backup/restore
  of retained bytes and subsequent reference restoration. Final checks are below.

### BUG-014 — Intermittent native search shortcut check

- **Milestone/state:** M07/M08; Closed as a validation issue after controlled
  native stress. One earlier native suite run reported
  a failed search shortcut assertion while other builds were running.
- **Environment/reproduction:** RHEL 9.8 Xwayland display, Debian 13 container,
  Qt 6.8.2/GCC 14.2.0. Run native editor/desktop checks alongside Clang/core builds.
  The first desktop search test types pending text and sends Ctrl+Shift+F.
- **Expected/actual:** the workspace search opens and its result is selected.
  The retained failure was `opened == false`; that run reported two failed
  assertions, with the other assertion's detail unavailable in retained output.
  A complete retry captured to the ignored build folder passed all 60 desktop
  cases without a product code change.
- **Diagnosis/resolution:** no product defect has been established. Activation/
  focus timing is a candidate, consistent with G16. The shortcut test now verifies
  both preconditions before sending the key. Repeat integrated native checks in
  M08 before treating this as resolved; the passing retry alone is not a fix claim.
- **Latest verification:** a second captured native run, with the explicit
  activation/focus preconditions, passed all 52 editor and 60 desktop cases.
  At that checkpoint, stress verification of the intermittent failure remained pending.
- **Final verification:** the original workflow passed 20 repetitions on RHEL
  Xwayland and another 20 on private Xvfb/Openbox, with explicit focus/activation
  preconditions. The original timing cause remains unconfirmed; no product
  defect or product-fix claim is inferred. See the M08.2 checkpoint.

### BUG-015 — Block restoration fell through to page restoration

- **Milestone/state:** M08.7; Fixed in the concurrent integration, with an
  automated restore regression. The last committed implementation was unaffected.
- **Environment/reproduction:** Debian 13 Docker, Qt 6.8.2/GCC 14.2.0,
  offscreen Linux. In the combined uncommitted snapshot, remove a block, open
  Restore, select its block group and choose Restore selected.
- **Expected/actual:** restore the blocks once and remove the recovery row from
  the list. The blocks restored, but an unexpected error dialog then stalled both
  desktop and trash-safety CTest entries until their 60-second timeouts.
- **Diagnosis/resolution:** the new reparenting notification used a separate
  `if` whose `else` also tried to restore the block-group ID as a page. Keep the
  notification inside the block-group branch so scope dispatch stays exclusive.
- **Verification:** the same captured source with that correction passed the
  desktop and trash-safety entries, including ordinary block recovery,
  undo/redo and three-scope recovery after reopening. The new dialog driver
  reports unexpected error dialogs directly instead of hanging on them.

### BUG-016 — Ranged structural rewrite returned the caret to the wrong block on undo

- **Milestone/state:** SUG-27/SUG-14 (M05 behavior); fixed and verified during the
  Suggestions implementation, before commit.
- **Environment/reproduction:** Debian 13 Docker, Qt 6.8.2/GCC 14.2.0, offscreen.
  Convert the sample `reference` block to a heading and undo; separately, place the
  caret at the start of block 1000 of a 2,000-block page, press Alt+Down and undo.
- **Expected/actual:** undo restores the document and leaves the caret in the
  edited/moved block. The first version put it in the first block (the existing
  `block_kind_conversion_preserves_content_and_references` BUG-003 regression
  failed), then, after that fix, in the following block.
- **Diagnosis:** the ranged rewrite first opened its undo group from a cursor at
  position 0. Starting from the user's cursor fixed BUG-003's case. When the first
  edit began exactly at the caret, Qt recorded no separate cursor position and placed
  the caret after the re-inserted text on undo (G23).
- **Resolution:** begin the group from `textCursor()` and, when a previous block
  exists, start the removal at that block's end so the first edit is away from the
  caret.
- **Verification:** the BUG-003 test and the new
  `structural_commands_rewrite_only_the_affected_range` (which asserts the post-undo
  block) failed before each fix and pass with GCC, Clang, ASan/UBSan and native
  Xwayland.

### BUG-017 — Custom paste modified a read-only editor

- **Milestone/state:** M07.8/M08.4; Fixed and verified.
  Bounded editor correctness issue; this does not concern filesystem permissions.
- **Environment/reproduction:** Debian 13 project container, Qt 6.8.2/GCC 14.2.0,
  Linux offscreen. Set `PageEditor::setReadOnly(true)`, put a URL on the clipboard
  and call the custom `paste()` command (also used by its context menu).
- **Expected/actual:** leave the document and undo history unchanged. The URL was
  inserted into the canonical snapshot despite the read-only flag. The focused
  `read_only_editor_does_not_paste_or_fetch` regression failed before the fix;
  output is retained at `/tmp/nebula-readonly-before.log`.
- **Diagnosis/resolution:** the custom paste commands invoked `insertFromMimeData`
  directly, bypassing QTextEdit's ordinary read-only command guard. Reject null
  input and read-only insertion before parsing or mutating any clipboard data.
  This covers normal, rich and plain-text paste and emits no preview request.
- **Verification:** the regression also checks plain-text paste, unchanged undo
  availability and zero preview requests. Final outcomes are recorded below.

### BUG-018 — Preview event filter downcast during editor destruction

- **Milestone/state:** M07.8/M08.4; Fixed and verified; undefined behavior in
  window teardown introduced by the uncommitted preview integration.
- **Environment/reproduction:** Debian 13 Docker, Qt 6.8.2, Debug ASan/UBSan,
  offscreen. Close a workspace window with a preview controller attached, even
  without starting a request. `test asan` failed four desktop-related entries.
- **Expected/actual:** clean teardown; UBSan reported a `QPointer<PageEditor>`
  downcast of an object whose dynamic type had already become `QWidget`.
- **Diagnosis/resolution:** the event filter compared its watched QObject to a
  typed QPointer on every event, including events emitted by base destructors.
  The filter is installed only on the editor, so remove the comparison. Cancel
  pending work when the editor is destroyed without accessing its derived type.
- **Verification:** the new `editor_destruction_cancels_pending_fetch` case and
  existing window teardown paths pass. Full GCC, Clang and ASan/UBSan suites
  passed all 14 entries; native Xwayland also passed the preview and window suites.

### BUG-019 — Intermittent native outgoing-link activation check

- **Milestone/state:** M08.2; Closed as a validation issue after controlled
  native stress. An earlier test failure; no product defect
  or data loss is established.
- **Environment/reproduction:** RHEL 9.8 Xwayland, Qt 6.8.2/Debian 13 Docker.
  `test-native` ran alongside Clang/ASan/core checks. The existing
  `unresolved_existing_choice_replaces_only_the_selected_reference` failed at
  tests/desktop_tests.cpp:934: the chosen reference retained an empty target ID.
- **Expected/actual:** selecting an existing target resolves only the second
  reference. That run left it unresolved. The full native retry passed all 60
  desktop behavior cases without a product change to the picker.
- **Initial diagnosis:** not established; activation/focus timing was a candidate
  as in BUG-014. M08.2 needed native activation preconditions and repeated checks.
  The initial passing retry was not claimed as a fix.
- **Final verification:** added explicit activation/list-focus preconditions;
  the exact unresolved-choice and outgoing-link workflows each passed 20
  repetitions on RHEL Xwayland and 20 on private Xvfb/Openbox. The original cause
  remains unconfirmed; this closes validation without claiming a picker defect
  was found or fixed. See the M08.2 checkpoint.

### BUG-020 — Reused read-only export falsely reports a corrupt search index

- **Milestone/state:** M08.1/M08.4/M08.13; Fixed and verified. The export fails safely; no damaged notes were observed.
- **Environment/reproduction:** Debian 13 Docker, SQLite 3.46.1, Qt 6.8.2.
  Keep one read-only workspace connection, commit a page change on another
  connection, then export lossless JSON repeatedly through the first connection.
- **Expected/actual:** each export includes the committed changes. Without the
  fix, the retained regression aborts with `Invalid workspace integrity:
  malformed inverted index for FTS5 table main.page_search`.
- **Diagnosis/resolution:** SQLite retains stale virtual-table state on the
  reused connection. Read the FTS table inside the export snapshot before
  `quick_check`; retain integrity and canonical-row validation. Actual corrupted
  derived content must still be refused.
- **Verification:** deliberately removing this refresh reproduced the failure
  (exit 134) in `reused_read_only_export_includes_other_connection_commits`.
  The source was restored immediately; the final GCC, Clang and ASan/UBSan
  suites pass the regression, including refusal of genuinely inconsistent search
  content. Native Xwayland passes it as well.

### BUG-021 — Large export read transactions can delay GUI autosave

- **Milestone/state:** M08.4/M08.13/M09.2; Fixed and verified on Linux. Pending
  edits remain available; no lost saved notes were observed.
- **Environment/reproduction:** pinned Debian 13 Docker, Qt 6.8.2, SQLite 3.46.1,
  GCC Debug ASan/UBSan. Type in the full window while a second process repeatedly
  backs up/exports 2,001 pages (about 66 MiB on disk). Both a loaded run and a
  subsequent sanitizer run reproduced the failure.
- **Expected/actual:** at least three autosaves without failure. The retained test
  observed `Save failed — database is locked. Press Ctrl+S to retry.` The worker
  itself completed; this differs from the earlier tmpfs/manifest-size refusals.
- **Diagnosis/resolution:** JSON and Markdown exports retained a live SQLite read
  transaction through validation, serialization and file writes. On this large
  sanitizer fixture that exceeded the writer's three-second busy timeout. Export
  now copies the database in bounded backup steps into a private disk snapshot
  under the new output folder, then validates/serializes only that copy. A shared
  managed-file lock retains immutable attachment consistency. The scratch database
  is removed before success; uncommitted source transactions are refused.
- **Verification:** extended the concurrency worker to cover backup, JSON and
  Markdown; captured the exact save error. Added checks for snapshot privacy,
  query-only access, independence from later saves, scratch removal and refusal
  during an active source transaction. GCC, Clang and ASan/UBSan pass 15/15
  groups; native Xwayland passes all interaction/reliability cases. The expanded
  worker covers JSON and Markdown without any save-failure status. M08.13 is
  complete again.

### BUG-022 — Installed documentation linked to omitted project files

- **Milestone/state:** M09.3; Fixed and verified. Broken local navigation in the
  development package's documentation.
- **Environment/reproduction:** Debian 13 amd64 development `.deb`. The installed
  README and guides link to `LICENSE`, `AGENTS.md` and `ROADMAP.md`, but the install
  manifest initially omitted those three files.
- **Expected/actual:** these relative links resolve; the target files were absent
  from the package. The separate MIT copyright notice was already included.
- **Diagnosis/resolution:** install all linked project documents, retain their
  original names/relative layout and check their presence after package install.
  Debian slim's separate documentation-exclusion policy required an explicit
  include for Nebula docs in the verification image (G30).
- **Verification:** the new presence check failed before the fix; after inclusion,
  the complete isolated offline package check passes, including reinstall/removal
  preservation, window editing/search, interchange, backup/restore and TLS.

### BUG-023 — Read-only block details did not support keyboard selection

- **Milestone/state:** M08.2; Fixed and verified. Reproduced in the resumed, uncommitted accessibility
  implementation. The existing committed editor is unaffected.
- **Environment/reproduction:** Debian 13 optional desktop image, Qt 6.8.2,
  GCC 14.2.0, Xvfb/Openbox/Orca 48.1; open F8 Block details and press
  Ctrl+Home, Shift+End. The reader sees text, but no selection is exported.
- **Expected/actual:** keyboard navigation/selection must work in the read-only
  text field. The focused offscreen regression also fails `keyboard_selection`;
  later F8 invocations lose activation after closing the dialog with Escape.
- **Diagnosis:** Qt's read-only plain-text field defaults to mouse selection.
  Keyboard selection needs an explicit text interaction flag. Dialog return also
  needs the same explicit window/editor activation used by other modal commands.
- **Resolution/verification:** enable keyboard selection, reuse the dialog for
  AT-SPI lifetime stability and restore editor activation/focus. Qt, real Orca
  and native-window regressions pass; final results are in the M08.2 work-log
  entry. The failing regression is retained in `tests/desktop_tests.cpp`;
  its pre-fix output is in ignored `build-container-gcc/details-before.log`.

### BUG-024 — IME preedit and mode keys marked unchanged pages dirty

- **Milestone/state:** M08.2; Fixed and verified. Reproduced during real OS desktop acceptance.
  Unnecessary saves advance revisions and can invalidate workspace move undo;
  no lost note content was observed.
- **Environment/reproduction:** Debian 13, Qt 6.8.2, IBus 1.5.32/Anthy,
  Xvfb/Openbox. The full native desktop suite's cancelled/invalid-image import
  check observed revision 2 instead of 1 without a document edit. A focused
  synthetic preedit regression then reproduced one `textChanged` signal for
  unchanged canonical text (ignored `build-container-gcc/ime-before.log`).
- **Diagnosis:** non-editing input events and IME mode/function/Escape keys opened
  empty edit groups, as in BUG-011/G21. A second focused check showed that Qt
  also emits native `textChanged` for visual preedit updates themselves, without
  canonical edits; simply removing the group did not prevent false saves.
- **Resolution/verification:** distinguish canonical edits from native display
  notifications in the editor adapter and bind autosave to the canonical signal.
  Keep native selection/cursor notifications and group commits/replacement
  deletion normally; dispatch mode/function/Escape keys without an edit group.
  Focused preedit/update/cancel, replacement deletion/undo, mode-key and real-IME
  unchanged-revision checks pass. Results are in the M08.2 checkpoint.

### BUG-025 — Backspace at a nested block's start did not leave its parent

- **Milestone/state:** M05.2/M08.2; Fixed and verified after the user's report.
- **Environment/reproduction:** Debian 13 development container, Qt 6.8.2,
  GCC 14.2.0. Nest a block below a parent, place the caret at offset zero with
  no selection and press Backspace. The user expects one level of outdent;
  the block remains nested. Empty blocks behave the same way.
- **Diagnosis:** the editor delegated this key to QTextEdit, which changed visual
  indentation without changing Nebula's parent identity. Normalization correctly
  restored indentation from that unchanged identity, leaving the block stuck.
- **Resolution:** route unmodified Backspace at the start of a nested editable
  block through the existing subtree outdent operation. Retain text, block type,
  IDs, descendants and sibling order; keep normal deletion/selection behavior
  elsewhere and group the outdent for undo/redo.
- **Verification:** the new focused regression first failed on unchanged block
  order (`build-container-gcc/backspace-before.log`, ignored). Empty/nonempty
  paragraph, heading, bullet and checked-task cases pass, including successive
  outdents, descendants, formatting, identity and undo/redo. Persisted-window
  checks retain backlinks, create no trash and survive reopen; native Xwayland
  checks pass. Final compiler/sanitizer results are in the work-log checkpoint.

For each future defect, record:

| Field | Required content |
| --- | --- |
| ID and milestone | Stable `BUG-001` style identifier and affected work. |
| State and impact | Open, investigating, fixed, or verified; severity and release impact. |
| Reproduction | Environment, steps, input/fixture, expected behavior, and actual behavior. |
| Diagnosis | Evidence and cause, when established; distinguish hypotheses. |
| Resolution | Fix or workaround, relevant commit, and any remaining limitation. |
| Verification | Regression check and its actual result. |

## Gotchas and design constraints

These are planning constraints established during discussion, not findings from a
running implementation. Append implementation discoveries with a date, affected
milestone, evidence, and the resulting action.

- **G01 — Wrapping versus structure:** display width must not create new blocks.
- **G02 — Identity versus presentation:** renaming, sorting, and reordering must
  preserve page/block identity and references.
- **G03 — Toolkit blocks versus application blocks:** Qt's text hierarchy supplies
  editing infrastructure, not a complete Notion-style persistent model.
- **G04 — Editor composition:** separate block widgets simplify specialized UI but
  require coordinated selection/navigation/undo; a single document has embedding
  and mapping constraints. Resolve this through M01 evidence.
- **G05 — Backups include assets:** a database-only copy cannot restore a workspace
  whose attachments live in separate files.
- **G06 — Markdown is not lossless:** database properties, views, IDs, and some rich
  formatting need the versioned full export format.
- **G07 — Diary dates are intentional:** the entry's date must not be inferred solely
  from creation time or shifted as if it were a timestamp.
- **G08 — Language stability is insufficient alone:** toolkit versions, storage
  formats, compiler baselines, and platform assumptions also affect longevity.
- **G09 — Always local is a product boundary:** modularity does not imply future
  synchronization or a server architecture.
- **G10 — Titles are labels, not identities:** duplicate names require explicit
  target selection; renaming a heading must preserve its target identity. Display
  labels and persistent targets have different responsibilities.
- **G11 — Backlinks are derived:** the inline reference is authoritative. Index
  rebuilds must preserve the document's intended references, including unresolved
  and unavailable targets, without silently binding them by name.
- **G12 — Host toolchains can mix incompatible libraries:** the initial RHEL build
  picked a Linuxbrew linker and newer GLib/fontconfig libraries, producing glibc
  symbol errors. An isolated Docker toolchain avoids that mixture. Host SDK checks
  used the system toolchain explicitly; no global PATH settings were changed.
- **G13 — Text integrity and glyph rendering are different checks:** UTF-8 and
  synthetic composition tests passed even when the container lacked CJK fonts.
  The development image now has an explicit font baseline and regression check.
- **G14 — Native display checks need explicit access:** the container GUI shares
  only the X11/Xwayland socket and its authorization file. Headless tests use the
  offscreen backend. Neither establishes real screen-reader or OS IME validation.
- **G15 — Workspace identity is not a saved note (2026-10-29, M03):** creating a
  real SQLite file does not establish content persistence. Keep the prototype's
  discard-on-close notice visible until page storage and autosave are verified.
- **G16 — Full-window tests need window-manager timing (2026-10-29, M01/M02):**
  offscreen focus was immediate, but Xwayland activation was asynchronous. Wait
  for exposure/activation before sending events to the focused widget; an immediate
  focus query initially returned null in the native test harness.
- **G17 — Docker mount and fixture paths (2026-10-29, M02):** bind-mount options
  use CSV quoting, not shell quoting alone; commas and quotes in host folders are
  exercised by launcher tests and a real GUI launch. The container's temporary
  filesystem is non-executable, so mock executable fixtures live in the writable
  build directory rather than `/tmp`.
- **G18 — C++17 lambda portability (2026-10-29, M02):** Clang flagged capture of a
  structured binding as a C++20 extension even though GCC accepted it. Use ordinary
  local references when a lambda needs values from map entries; both compilers
  must remain clean under the selected language baseline.
- **G19 — Backups must not initialize or migrate their source (2026-10-29, M03):**
  use a read-only SQLite connection for backup/restore input and migrate only the
  restored copy. Tests back up a file without write permission and compare its
  original bytes. Docker checks source/destination host overlap before mounting,
  since different container paths can otherwise alias a nested host folder.
- **G20 — Clipboard text is insufficient for blocks (2026-10-30, M05):** Qt's
  native HTML does not preserve application identities or reference metadata.
  Use versioned semantic JSON alongside external text/HTML formats. A copy uses
  fresh inserted identities and remaps references within the copied selection;
  identity-preserving cross-page movement remains a separate application operation.
- **G21 — Empty editor groups can signal changes (2026-10-30, M05):** Qt 6.8.2
  emits `textChanged` for empty cursor edit groups around shortcut modifiers.
  BUG-011 reproduced unnecessary autosaves and revision conflicts. Keep read-only
  keyboard actions outside mutation groups; exercise shortcuts in the full window
  in addition to triggering their QAction directly.
- **G22 — Block archives do not cover every attachment undo (2026-10-30, M06):**
  BUG-013 removes an object while keeping its block ID, so no deleted-block fragment
  exists. Retain registered imports independently of current reference counts; clean
  only unregistered leftovers until a deliberate purge policy can account for undo,
  recovery and retained backups. Empty trash (SUG-05) is now that policy: it runs
  only after clearing editor and block-transfer undo.
- **G23 — Qt undo positions the caret from the group's edits (2026-10-30, M05):**
  QTextDocument restores the cursor from where an undo group began unless the first
  edit starts exactly there, in which case the caret lands after the re-inserted
  text. Start structural groups from the user's cursor and make the first edit begin
  elsewhere (BUG-016).
- **G24 — Disabled triggers change fault injection (2026-10-30, M08):** Nebula
  connections now refuse and disable triggers (SUG-03), so tests can no longer inject
  write failures with SQL triggers from a second connection. Tests drop the derived
  `page_search` table instead; every write path updates it, so the whole operation
  still rolls back.
- **G25 — QCollator ignores numeric mode in the C locale (2026-10-30, M04):** the
  container runs `C.UTF-8`, where Qt's collator compared "Item 10" before "Item 9".
  View ordering therefore uses an application-owned, locale-independent natural
  comparison of case-folded text, which also keeps orderings identical across
  machines.
- **G26 — FTS5 deletes by unindexed column scan the table (2026-10-30, M07):**
  `DELETE FROM page_search WHERE page_id=?` reads every row. Version 10 keeps a
  `page_search_rows` map from page/block to FTS rowid so updates delete by rowid.
- **G27 — Concurrent agents can share one working tree (2026-10-30, process):**
  another agent committed `963cdfb` and `382aad4` from this tree while the
  Suggestions work was uncommitted; both edited `workspace_window.cpp`,
  `CMakeLists.txt` and this roadmap. Partial staging kept each change set apart, and
  BUG-015 surfaced only in the combined tree. Inspect `git status`/`git log` before
  committing, stage by hunk, and rerun the combined suites.

- **G28 — Size limits apply to the whole fixture (2026-10-30, M08):** a
  64 MiB body fixture exceeds the 64 MiB JSON manifest contract after metadata.
  The container also caps `/tmp` at 256 MiB. Large concurrency fixtures use
  supported-size data in the ignored build directory; record code 13 as disk
  exhaustion rather than diagnosing backup locking from a generic error.
- **G29 — Refresh FTS state before repeated read-only checks (2026-10-30,
  M08):** BUG-020 reproduced a false integrity failure after another connection
  committed. A normal FTS read within the export snapshot refreshes SQLite 3.46.1
  state without dropping integrity or canonical-content checks.

- **G30 — Slim images can list documentation excluded during unpacking (2026-10-30,
  M09):** `dpkg-query -L` listed the README while no Markdown existed on disk.
  Debian slim's dpkg path-exclude policy intentionally drops it. The verification
  image explicitly includes `/usr/share/doc/nebula/*` before installation so local
  documentation checks exercise the actual files. That include applies only to
  the verification image.

- **G31 — Read-only Qt text needs explicit keyboard selection (2026-10-30,
  M08.2):** the first Block details regression could read the field but could not
  select a line with Shift+End (BUG-023). Set `TextSelectableByKeyboard` alongside
  mouse selection. Keep reusable dialog objects valid for AT-SPI clients and wait
  for the reader's actual focus announcement before testing the next keystroke.
- **G32 — Native text-change signals include visual composition (2026-10-30,
  M08.2):** after removing empty edit groups, a focused Qt 6.8.2 check still saw
  `textChanged` for preedit without canonical text changes (BUG-024). Autosave
  must observe canonical edits; preserve native display/cursor/selection signals.
- **G33 — Reader debug files can buffer successful speech (2026-10-30,
  M08.2):** Orca 48.1 opens its debug file with block buffering. A live assertion
  timed out although the requested focus speech appeared in the file on exit.
  Its private test-user customization sets line buffering for that log only;
  speech generation and the installed reader remain unchanged.
- **G34 — New CMake deprecates the older SQLite target alias (2026-10-30,
  M09.1):** earlier toolchain checks with CMake 4.3.5 warned that
  `SQLite::SQLite3` should become `SQLite3::SQLite3`. The build/tests passed.
  Future CMake upgrades must retain compatibility with the agreed 3.22 baseline.

## Suggestions

This is an architecture, security and quality review of commit `761dd84`, requested
by the user on 2026-10-30. The entries are proposals, not scheduled work. They do not
change milestone status or authorize implementation. Move accepted items into the
relevant milestone task list. Before fixing a confirmed defect, reproduce it with a
failing test and record it in the bug log. Line references are to `761dd84`.

- **Severity:** High means likely data loss, exposure, or a release-blocking slowdown
  in normal use. Medium means real but bounded impact. Low means an edge case or
  polish.
- **Basis:** *Confirmed* means traced through the source, plus existing tests where
  cited. *Plausible* means the code path exists but has not been reproduced.

Several things are already solid. All SQL values are bound parameters, and FTS terms
are quoted, so no SQL or FTS injection path was found. HTML paste loads no resources.
Managed files are created with `O_EXCL`/`O_NOFOLLOW` and flushed before commit.
Interchange paths reject traversal and symlinks. Saves are transactional with
optimistic revisions. The suggestions below build on that.

Recommended order:

1. Before writing M07.8 code, agree on the network guardrails (SUG-01) and the
   representative performance fixtures (SUG-33). M07.8 and M08 then start from
   measured baselines.
2. Make the small, contained fixes, each with a regression test: SUG-02, SUG-11,
   SUG-14, SUG-15, SUG-16, SUG-07, SUG-29 and SUG-04.
3. For the M08 core: SUG-25–SUG-28 and SUG-30 (these refine R05), the retention
   policy in SUG-05/SUG-12, conflict handling in SUG-13/SUG-37, schema hardening in
   SUG-03, and the verification work in SUG-40.
4. Before M09: SUG-06, SUG-08, SUG-10, SUG-41 and SUG-42.

### Implementation status — 2026-10-30

At the user's request every suggestion was implemented or decided in the same day.
The entries below remain as the original review; this table records the outcome.
Evidence and checks are in the work log ([Suggestions implementation
checkpoint](#suggestions-implementation-checkpoint--2026-10-30)).

| ID | Outcome | Change and evidence |
| --- | --- | --- |
| SUG-01 | Addressed by M07.8 | The concurrent M07.8 work recorded and implemented the fetch guardrails ([PREVIEW_DESIGN.md](docs/PREVIEW_DESIGN.md), `382aad4`). Editor integration remains in M07.8. |
| SUG-02 | Done | Data commands mount only a new 0700 staging folder and rename it into place; existing destinations are refused before Docker runs. Launcher test. |
| SUG-03 | Done | Defensive mode, `trusted_schema` off, triggers/views disabled; schema object whitelist before and after migration and for read-only/restore. Storage test. |
| SUG-04 | Done | New workspace, backup, restore and export folders are 0700 and databases 0600. Storage test. |
| SUG-05 | Done | Restore… → Empty trash permanently… purges trash, recovery records and unreferenced files, clears undo and runs `VACUUM`. Storage and window tests. |
| SUG-06 | Done | Opening hands out a checksum-verified, read-only, original-named copy that is removed with the window. Window test. |
| SUG-07 | Done | Message boxes and data-bearing labels use plain text. Window test. |
| SUG-08 | Done | Image decoding is bounded and off-thread. The Debian preview bundles no image plugins and uses system Qt packages; the release checklist requires image-plugin security updates. |
| SUG-09 | Done | `SQLITE_OPEN_NOFOLLOW` and component-by-component `O_NOFOLLOW` opens for every copy and digest. Storage test. |
| SUG-10 | Done | README and ATTACHMENTS.md describe the manifest as corruption detection. |
| SUG-11 | Done | Storage refuses backup/export folders; the chooser also confirms non-empty unrelated folders. Storage and window tests. |
| SUG-12 | Done | The editor reports merged blocks, which are not archived; `deleted_block_members` replaces per-save fragment decoding. Editor, storage and window tests. |
| SUG-13 | Done | Exclusive cross-process session lock; conflicts get their own status, Keep my version as a new page, and a close option. Subprocess and window tests. |
| SUG-14 | Done | Structural commands keep the caret offset and selection, including after undo (BUG-016). Editor test. |
| SUG-15 | Done | Paste and Paste plain text (Ctrl+Shift+V) are separate. Editor test. |
| SUG-16 | Done | Percent-decoding, file-name labels and reported missing files; traversal still rejects the import. Storage test. |
| SUG-17 | Done | Saves refresh only changed rows, outside item signals, and leave open cell editors alone. Window test. |
| SUG-18 | Done | JSON import rejects revisions of 2^62 or more; saves guard the bound. Storage test. |
| SUG-19 | Done | Fragments whose parent moved away return as top-level blocks with a notice. Storage test. |
| SUG-20 | Done | `create_journal` and `create_dated_entry` are single transactions. Storage and window tests. |
| SUG-21 | Done | SIGTERM/SIGINT/SIGHUP reach the event loop through a self-pipe and flush pending edits. Window test. |
| SUG-22 | Done; WAL not adopted | Three-second busy timeout, 256-page backup steps and private disk snapshots for exports. Full-window concurrent backup/JSON/Markdown tests pass, including BUG-021 under ASan/UBSan. |
| SUG-23 | Done | Locale-independent natural case-insensitive order, case-folded Contains, tag-set equality, decimal comma, tolerant tags. Storage and application tests. |
| SUG-24 | Decided | `workspace_id` names a lineage; documented in WORKSPACE_FORMAT.md and INTERCHANGE.md. |
| SUG-25 | Done | Typing fast path, per-page label and card-path caches, neighbour-based toolbar state, partial repaint, visible-only dividers. Editor tests and benchmark. |
| SUG-26 | Done | Identity-diffed saves with reused statements, label-aware referrer reindexing, and row/link refresh only on change. Storage tests and benchmark. |
| SUG-27 | Done | Structural edits rewrite only the affected block range. Editor test and benchmark. |
| SUG-28 | Done | Cached link-target rows with incremental 100-page/block batches, full-cache search and pending-page overlays. Widget/full-window tests. |
| SUG-29 | Done | `links(source_page)` index in migration and rebuild. Query-plan test. |
| SUG-30 | Done; contentless rejected | Title/properties and each block body are stored once; result context comes from canonical blocks. Contentless deletes need SQLite 3.43. |
| SUG-31 | Done | Prepare/copy/commit import with worker-thread progress and cancel; backups hash once and restore twice. Storage tests. |
| SUG-32 | Done | Full checks after migrations, weekly or on request, recorded in `maintenance`. Storage and window tests. |
| SUG-33 | Done | Three-run M08.3 measurements meet the recorded M01/integrated reference-machine budgets; methodology in PERFORMANCE.md. |
| SUG-34 | Done | Qt-free navigation, property text, link-target, view-edit and import/export services with focused application tests; dialogs remain desktop adapters. |
| SUG-35 | Done | Interchange is free functions over `WorkspaceInternals`; `sql.hpp` is a documented internal header. |
| SUG-36 | Done | Shared format versions, view validation and length-aware Qt UTF-8 helpers; Unicode/NUL/invalid-encoding regressions. |
| SUG-37 | Done; English messages | `StorageError` categories drive conflict handling; translation stays with SUG-43. |
| SUG-38 | Done | The development image supplies clang-format; the C++ tree was normalized in formatting-only commit d6aac43 and CI checks the style. |
| SUG-39 | Done | `valid_identity` enforced by core validation, link filters, JSON and clipboard decoding. Storage test. |
| SUG-40 | Done | Full GCC/Clang/ASan/UBSan, deterministic/coverage-guided fuzzing and stress modes pass locally and on the recorded real GitHub runner. |
| SUG-41 | Done | Configure-time FTS5 check and startup probe. |
| SUG-42 | Done | Dirty-tree guard, immutable official Debian manifest digest and signed Debian/security package snapshot, with deliberate update/retest instructions. |
| SUG-43 | Decided | Delete view, rename/delete property and Empty trash are first-release features (implemented). File/image drag-and-drop and UI translation are deferred until after the first release. |

### Security and privacy

- **SUG-01 — Set guardrails before M07.8 adds the first network code** (advisory,
  M07.8). No target links a network module today, so the preview fetcher will set the
  pattern for the only exception in AGENTS.md. Proposed contract:
  - Fetch only after an explicit paste of an `http`/`https` URL.
  - Send GET only, with no cookies, credentials or Referer.
  - Follow a small number of redirects, and only to http(s) URLs.
  - Enforce a timeout and a byte cap while streaming.
  - Allow cancellation without blocking the UI thread.
  - Extract text without running scripts or loading subresources.
  - Store the preview as plain runs, with control and bidirectional-override
    characters removed.

  Decide whether loopback and private-network hosts are fetched without confirmation:
  a pasted link can reach a router or local admin page. Record the Qt Network/TLS
  backend licence and Linux deployment requirements in DEPENDENCIES.md. Test the success,
  failure, timeout, oversize and cancel paths against a loopback server inside the
  network-less test container.
- **SUG-02 — `dev.sh` mounts the destination's parent read-write** (Medium,
  Confirmed, tooling). The `backup`, `restore`, `export-md`, `export-json` and
  `import-json` commands mount `dirname NEW_FOLDER` writable at `/output`
  (`scripts/dev.sh:102-115`). As a result, `./scripts/dev.sh backup ~/nebula
  ~/nebula-backup` gives the container write access to the whole home directory,
  including the "read-only" source through a second path. This undermines the README
  statement that the source is mounted read-only. Instead, create an empty staging
  folder beside the destination on the host, mount only that folder, and rename it
  into place after success. Assert the exact mount set in `tests/launcher_tests.sh`.
- **SUG-03 — Treat opened and restored databases as untrusted schema** (Medium,
  Confirmed absence of checks, M08.1). `Workspace::open` and `Workspace::read_only`
  accept any file with the NEBU application ID, a known `user_version` and a passing
  `quick_check` (`src/storage/workspace.cpp:82-198`, `src/storage/backup.cpp:9-26`).
  Restore then copies the schema verbatim. A crafted or hand-edited backup can
  therefore carry triggers or views that rewrite or delete rows during ordinary
  saves. Its manifest cannot detect this because the manifest travels with the
  backup. Compare `sqlite_schema` with the objects expected for each supported
  version, allowing FTS5 shadow tables, and fail on any extras. Enable
  `SQLITE_DBCONFIG_DEFENSIVE` and `PRAGMA trusted_schema=OFF`. Where the SQLite
  baseline allows, also disable triggers and views on Nebula connections. This
  also applies when opening backups or importing exported data from other machines.
- **SUG-04 — Use private file modes for diary data** (Medium, Confirmed, M08).
  Workspace, backup and export folders get default directory modes
  (`src/storage/workspace.cpp:58`), and SQLite creates `nebula.sqlite3` with its
  default 0644. Attachments, manifests and export files, by contrast, are written
  0600 (`src/attachments/files.cpp`). On a shared machine with a traversable home
  directory, other accounts can read the notes but not the attachments. Create
  Nebula-owned folders as 0700 and the database as 0600: pre-create the file, or
  `fchmod` it after creation (SQLite gives journals the database's mode). Document
  the policy in WORKSPACE_FORMAT.md.
- **SUG-05 — Content cannot be permanently deleted** (Medium, Confirmed, M08 policy
  decision). Nothing ever purges `deleted_blocks`. Registered attachments are always
  kept (G22). Trashed pages and databases have no permanent delete. All of this is
  copied into every backup and every lossless JSON export. For a diary, this means
  deleted text keeps travelling indefinitely. SQLite free pages also keep removed
  bytes until `VACUUM` or `secure_delete`. Define a retention and purge policy that
  covers fragments, trash, attachments, undo safety and existing backups, with a
  user-visible "empty trash" action.
- **SUG-06 — Open current attachment hands out the managed file itself** (Medium,
  Confirmed path, M08). `QDesktopServices::openUrl` receives the writable,
  extensionless file `attachments/<id>` (`src/app/workspace_window.cpp:182-185`). An
  external editor that saves in place changes bytes that ATTACHMENTS.md calls
  immutable, and backup and JSON export then fail their checksums. Without an
  extension, the desktop picks the handler by sniffing the content. Open a read-only,
  non-executable temporary copy named after the original basename, or store managed
  files as 0400. Clean up the temporary copies afterwards.
- **SUG-07 — Untrusted strings go through rich-text auto-detection** (Low, Confirmed,
  M08.2). Most `QLabel`s and every `QMessageBox` use Qt's default `AutoText`. Their
  text includes user- or import-controlled values, such as property names, JSON field
  names, manifest paths and unresolved reference names
  (`src/app/workspace_window.cpp:77`, `:332`, `:697`). If the first line contains a
  known HTML tag, Qt renders it as HTML. This allows spoofed messages and `<img>`
  loads of local files. Set `Qt::PlainText` wherever data is shown, as is already
  done for `workspaceLocation` and the search status label.
- **SUG-08 — Untrusted images are decoded on the UI thread** (Low, M08/M09). Import
  validation (`src/app/workspace_window.cpp:853`) and card previews
  (`src/editor/page_editor.cpp:74-86`) decode user files synchronously during
  painting, using every installed Qt image plugin. Ship only the image plugins Nebula
  needs, and set an explicit `QImageReader::setAllocationLimit`. Decode previews
  outside the paint path. Add Qt image-plugin security updates to the release
  checklist.
- **SUG-09 — Symlink checks run before link-following opens** (Low, M08).
  `Workspace::open` rejects a symlinked `nebula.sqlite3` with `fs::is_symlink`, then
  opens the path with `sqlite3_open_v2`, which follows links
  (`src/storage/workspace.cpp:64-70`). Directory walks use the same check-then-use
  pattern. Exploiting this needs a concurrent local attacker, but the race is cheap
  to remove: use `SQLITE_OPEN_NOFOLLOW` (supported within the SQLite 3.33 baseline)
  and `openat`/`O_NOFOLLOW` traversal.
- **SUG-10 — Describe the backup manifest as corruption detection** (Low,
  documentation, M08.5). Restore detects only accidental changes, because anyone who
  can edit a backup can also regenerate `backup-manifest.json`. Say so in the README
  and ATTACHMENTS.md so users do not treat it as tamper evidence. Signing would be a
  separate decision.

### Data reliability and correctness

- **SUG-11 — Opening a backup or export folder as a workspace changes it** (Medium,
  Confirmed, M08.1). The chooser accepts any folder, and `Workspace::open` does not
  check for `backup-manifest.json` (`src/storage/workspace.cpp:50`). Opening a backup
  with an older schema migrates it immediately. With the current schema, any edit or
  attachment import modifies it. Either way, a later restore fails the manifest
  check, so the backup is unusable exactly when it is needed. Refuse folders that
  contain a backup manifest, and offer a restore into a new folder instead. Warn
  before creating a workspace in a non-empty folder that is not a workspace, such as
  a JSON or Markdown export or the home directory.
- **SUG-12 — Merges create recovery fragments for text that still exists** (Medium,
  Confirmed, M08.4). Backspace or Delete at a block boundary keeps the first block's
  ID and drops the second (`backspace_merges_and_undo_restores_identity`). The next
  save then archives the second block's text (`archive_removed`,
  `src/storage/records.cpp:41-72`), even though that text now lives in the merged
  block. Ordinary writing therefore fills Restore… with duplicates, and restoring one
  duplicates its text. Each save also decodes every unrestored fragment of the page
  (`records.cpp:63-70`), and Restore… lists every unrestored fragment in the
  workspace. Skip archiving when a merge keeps the content, and store fragment
  membership in a table rather than re-decoding JSON on every save. Apply the
  retention policy from SUG-05.
- **SUG-13 — A revision conflict can only be resolved by discarding edits** (Medium,
  Confirmed, M08.1). Nothing stops two Nebula processes, such as two `dev.sh run`
  sessions, from editing the same workspace. A stale save fails with "Record is
  missing or changed in another window" (`src/storage/sql.hpp:65`). Retries fail the
  same way, navigation is blocked, and closing offers only Discard. Hold an exclusive
  workspace-open lock, separate from the attachment lock. On a conflict, offer to
  keep the unsaved document (for example as a new page) before reloading.
- **SUG-14 — Structural commands move the caret to the start of the block** (Low to
  Medium, Confirmed, M05 behavior). Move, indent, outdent, block-kind changes and
  grip drops rewrite the document, then call `go_to_block`
  (`src/editor/page_editor.cpp:325-387`, `:898-941`). That places the caret at offset
  0 and drops the selection, so typing resumes in the wrong place after Tab in
  mid-sentence or Alt+Up while editing. Restore the previous in-block offset and
  anchor, as `restore_position` already does, and assert this in the existing
  keyboard tests.
- **SUG-15 — "Paste plain text" pastes rich content** (Low, Confirmed). The
  context-menu action calls `PageEditor::paste` (`page_editor.cpp:1129`), which
  prefers the Nebula payload or HTML (`:805`). Rename the action "Paste", and add a
  separate plain-text paste (for example Ctrl+Shift+V) that reads only `text/plain`.
- **SUG-16 — Common attachment syntax aborts a whole Markdown import** (Low,
  Confirmed, M07.3). `![](attachments/a.png)` produces an empty asset label, which
  later fails `validate` (`src/core/document.cpp:72-77`). Any `%` in a path,
  including the common `%20`, is rejected as unsafe
  (`src/interchange/markdown.cpp:45`). Fall back to the file name as the label, and
  percent-decode before the existing traversal checks. Report a single unusable link
  instead of aborting the import.
- **SUG-17 — Autosave refreshes can discard table edits** (Low, Plausible, M04
  behavior). Every successful flush calls `refresh_table()`
  (`src/app/workspace_window.cpp:330`), which clears the table (`:373`). If the 500
  ms timer fires while a cell editor is open, the in-progress edit is lost. The
  `itemChanged` handler also rebuilds the table while Qt is still delivering that
  item's change (`:258-273`). Update the affected row in place, and skip refreshes
  while a cell editor is active. Queue refreshes instead of running them inside item
  signals.
- **SUG-18 — Imported revisions near `INT64_MAX` make pages unsavable** (Low,
  Plausible, M07.5). JSON import accepts any int64 revision
  (`src/interchange/json.cpp:243`). The next save evaluates `revision+1` in SQLite,
  which overflows to a REAL, and `++page.revision` in C++, which is undefined
  behavior. After that, the revision check can no longer match. Bound accepted
  revisions (for example below 2^53), and add the boundary to the JSON rejection
  tests.
- **SUG-19 — Some recovery fragments can never be restored** (Low, Plausible). If a
  fragment's original parent was later moved to another page,
  `restore_deleted_blocks` reports "Restore the deleted parent block first"
  (`src/storage/records.cpp:389`), even though the parent is live elsewhere. Restore
  such fragments as roots, or at their recorded anchor, with an explicit message.
- **SUG-20 — Bootstrap and journal helpers are not atomic** (Low, Confirmed). The
  default Journal database, its three properties and its view are written as separate
  autocommit statements (`src/app/workspace_window.cpp:306-313`). A failure part-way
  leaves an incomplete Journal that is never repaired, because the database list is
  no longer empty. Journal entry likewise adds "Entry date" to whichever database is
  selected before it creates the page (`:229-233`). Perform each of these as one
  storage transaction.
- **SUG-21 — Termination signals skip the pending-edit flush** (Low, Confirmed,
  documented). Closing a window flushes pending edits. SIGTERM and SIGINT (`docker
  stop`, `kill`, service managers), however, end the process without saving the last
  500 ms. A signal handler that forwards to the event loop (self-pipe plus
  `QSocketNotifier`), then flushes and quits, would narrow the documented loss
  window.
- **SUG-22 — Measure lock contention between the GUI and headless tools** (Low,
  M08.1). With the rollback journal and a 3 s busy timeout, a long headless backup (a
  single `sqlite3_backup_step(-1)` call) can make GUI autosaves fail with "database
  is locked". Read-only tool connections set no busy timeout at all
  (`src/storage/backup.cpp:17`). Measure this with large fixtures. Then consider
  stepping the backup in chunks, adding a busy timeout to read-only connections, and
  evaluating WAL mode. WAL offers fewer syncs and non-blocking readers, but its
  `-wal`/`-shm` files need handling on read-only mounts.
- **SUG-23 — Ordering and entry rules surprise users** (Low, UX). Title and text
  sorting and Contains filters use case-sensitive byte comparisons
  (`src/core/database.cpp:34-92`, documented in WORKSPACE_FORMAT.md), so "apple"
  sorts after "Zebra". Number entry ignores locale decimal separators. A trailing
  comma in tags is rejected rather than ignored. Tag Equals depends on tag order.
  Consider a display collation key in the desktop layer, while keeping storage
  ordering deterministic.
- **SUG-24 — JSON import keeps the source workspace identity** (Low, design).
  `import_json` replaces the new workspace's ID with the exported one
  (`src/interchange/json.cpp:231`). Importing one export twice therefore produces two
  independent workspaces with the same identity and indistinguishable backup
  manifests. Decide whether `workspace_id` identifies a lineage or an instance. If it
  identifies an instance, generate a new ID on import and record the source ID
  separately.

### Performance and scalability

- **SUG-25 — Each keystroke makes several whole-document passes** (High, M08.3).
  - `mutate` takes a full snapshot before every edit (`page_editor.cpp:580`), and
    `normalize` walks every block.
  - When default labels exist, `refresh_reference_labels` scans all fragments, takes
    another snapshot (`:493`), and resolves each external target through SQL.
  - The window then snapshots again and copies and validates the whole page several
    times just to set toolbar states (`src/app/prototype_window.cpp:144-163`).
  - Every cursor move repaints the whole viewport (`page_editor.cpp:253`), and
    `paintEvent` walks all blocks to find dividers (`:281`).
  - Each visible card runs a SQL query and file checks on every paint (`:74`).

  Track changed blocks from `contentsChange`, and derive toolbar state from the
  current block's neighbours. Cache card paths and labels per page, and repaint only
  the previous and current active rectangles.
- **SUG-26 — Each autosave rewrites far more than the edit** (High, M08.3).
  - `save_page_in_transaction` reloads the stored page, then deletes and reinserts
    every block, preparing a new statement per row (`src/storage/records.cpp:27-39`,
    `:181-208`). It also decodes the page's unrestored fragments.
  - `refresh_search` then reindexes the page and every page that refers to it
    (`src/storage/search.cpp:93-102`), even when no title or heading used by a
    default label changed. A hub page that every journal entry references therefore
    reindexes the whole diary at each 500 ms pause.
  - `flush()` then reloads and decodes every page in the database for the table, and
    loads full target pages just for outgoing-link titles
    (`src/app/workspace_window.cpp:330`, `:433`).

  Diff blocks by ID and write only what changed, and reuse prepared statements.
  Reindex referrers only when a label they display changes, and refresh only the
  saved row.
- **SUG-27 — Structural edits replace the entire text document** (Medium, M08.3).
  Move, indent, outdent, kind changes, dividers and drops call `write_page`, which
  removes and reinserts all text (`page_editor.cpp:325-387`). The cost grows with
  page size: the checkpoints above record up to 84 ms for an adjacent move on 1,000
  short blocks. Each command also records a full-document deletion and insertion in
  QTextDocument's unbounded undo history, so memory keeps growing during long
  sessions on long pages. Apply local cursor operations to the affected range, and
  measure undo memory over long sessions.
- **SUG-28 — Pickers and completion load every page** (Medium to High, M08.3). Wiki
  completion and the Link… picker build candidates from every block of every page
  (`src/app/workspace_window.cpp:558-573`). `enter_wiki_link` rebuilds them for each
  typed `]]`. The reference-filter controls, Move block to page and Restore… also
  call `pages()` for each database (`:95`, `:956`, `:538`), and each call decodes
  whole documents. Add storage queries that return lightweight page, heading and
  block rows, use them incrementally, and stop building full block trees for pickers.
- **SUG-29 — No index on `links(source_page)`** (Medium, Confirmed, M07.7 follow-up).
  Both reference-filter directions filter `links` by `source_page`
  (`src/storage/link_query.cpp:9-10`). The table, however, is indexed only by its
  `(source_block, run_index)` key and `links_target`, so every candidate page scans
  the whole link table. Add a `links(source_page)` index, both through a schema
  migration and in `rebuild_links`, and confirm the query plans with `EXPLAIN QUERY
  PLAN`.
- **SUG-30 — The search index stores most text several times** (Medium, M08.3). Every
  block row repeats the page title and all property text
  (`src/storage/search.cpp:69`). The whole-page row stores the body twice, once
  indexed and once as the unindexed excerpt (`:83`). Index size and write volume
  therefore grow with blocks × (title + properties). A title match also returns one
  row per block before de-duplication. Keep the title and properties only on the page
  row, and use an external-content or contentless table for bodies. Build excerpts
  from the canonical blocks when displaying results.
- **SUG-31 — Attachment I/O holds locks and repeats hashing** (Medium, M08.3). Import
  copies the whole source file on the UI thread while holding the exclusive file lock
  and a `BEGIN IMMEDIATE` transaction (`src/storage/attachments.cpp:50-79`). Backup
  reads every attachment three times (copy, audit and manifest), and restore reads it
  four times (`src/storage/backup.cpp:55-81`). Stage and hash the file before opening
  the transaction, and reuse the digests computed while copying. Run large copies off
  the UI thread, with progress and cancel.
- **SUG-32 — Startup runs full integrity scans under a write lock** (Low to Medium,
  M08.3). Every open runs `PRAGMA quick_check` and `PRAGMA foreign_key_check` inside
  `BEGIN IMMEDIATE` (`src/storage/workspace.cpp:80-202`). Both scale with database
  size, so launch time grows with the diary and other connections wait in the
  meantime. Run the full checks after migrations, on request or periodically, and
  measure open time with large fixtures.
- **SUG-33 — Benchmark the integrated application with representative data** (High,
  process, M08.3). The benchmark drives a bare `PageEditor` with 1,000 one-sentence
  paragraphs (`src/prototype/main.cpp:25-70`). It therefore leaves out the window
  refresh, autosave, SQLite, search indexing, labels and cards that SUG-25 and SUG-26
  describe. Add fixtures such as:
  - a 10,000-block page;
  - a 5,000-entry Journal;
  - a hub page with 1,000 backlinks;
  - a page with 200 images and many default labels.

  Measure keystroke latency, save time, open time and memory in `WorkspaceWindow`.
  Record thresholds before using any of them as a gate.

### Architecture, maintainability and verification

- **SUG-34 — Split `WorkspaceWindow` into testable application services** (Medium).
  The 989-line class builds widgets and also owns autosave, table and view editing,
  navigation history, wiki completion, unresolved-target workflows, attachments,
  search and interchange, calling `Workspace` directly. AGENTS.md asks for separate
  application operations, yet `PageSession` is the only Qt-free application layer.
  Extract navigation history, link-target lookup, view editing and interchange
  coordination into Qt-independent services with unit tests, leaving the window as an
  adapter.
- **SUG-35 — Module boundaries leak** (Low to Medium).
  `include/nebula/storage/workspace.hpp:95-98` declares the Markdown and JSON
  interchange operations as `Workspace` members, but they are defined in
  `nebula_interchange`. `nebula_storage` therefore advertises symbols it does not
  provide. Interchange also includes storage-private headers
  (`src/interchange/internal.hpp:2`, `src/interchange/markdown.cpp:3`). Give storage
  a narrow API (transactions, snapshots, canonical record access), and implement
  interchange as free functions over it.
- **SUG-36 — Centralize schema support and shared helpers** (Low, before the next
  schema change). The supported versions are listed as string comparisons in two
  places that must change together (`src/storage/workspace.cpp:94`,
  `src/storage/backup.cpp:22`). The UTF-8 conversion helpers, `execute`/`scalar`, and
  view validation (`save_view` versus JSON `validate_records`) are duplicated. Use
  one supported-range constant, a list of migration steps with a test for each step,
  and shared validators.
- **SUG-37 — Typed storage errors** (Medium, with SUG-13). Storage throws
  `std::runtime_error` and `std::invalid_argument`, and the desktop shows their
  English text directly. It cannot tell a stale revision from a busy database, a full
  disk or invalid input, so it cannot offer the matching recovery (reload, retry or
  correct the input), and the messages cannot be translated. Report error categories
  from storage, and choose messages and actions in the desktop layer.
- **SUG-38 — Decide on a formatting standard** (Low, decision needed). Many functions
  put several statements on one line, up to about 400 characters, in
  `workspace_window.cpp`, `records.cpp` and `search.cpp`. This hides control flow
  during review, creates noisy diffs and limits line-level debugging. AGENTS.md asks
  new code to match its surroundings, so adopting a `.clang-format` column limit
  needs an explicit decision. If adopted, apply it file by file in commits that
  change only formatting.
- **SUG-39 — Define the identity grammar** (Low, M08.5). Storage creates 32-character
  lowercase hexadecimal IDs (`src/storage/sql.hpp:64`), while the editor creates
  hyphenated UUIDs (`src/editor/page_editor.cpp:52`). JSON and clipboard input accept
  any non-empty string without NUL. Document the accepted syntax and length, and
  validate IDs wherever data enters the workspace.
- **SUG-40 — Strengthen automated verification** (Medium, M08.4). All container
  builds use Release (`scripts/dev.sh:88`). There is no sanitizer build,
  warnings-as-errors, coverage report, fuzzing or CI configuration, so every check
  depends on someone running it by hand. Add:
  - ASan/UBSan Debug builds;
  - fuzz targets for `parse_wiki_link`, the block/view/document codec, JSON and
    Markdown import, HTML paste and the backup manifest;
  - `-Werror` for project targets;
  - a Linux CI workflow that runs `test gcc`, `test clang` and `test core`.

  Use the CI workflow to stress-test BUG-014.
- **SUG-41 — Check required SQLite features up front** (Low, M09.1). CMake checks
  only the SQLite version (`CMakeLists.txt:30`), but the schema also needs FTS5 at
  run time. With a distribution SQLite that lacks FTS5, the problem appears only when
  the version 7→8 migration fails. Add configure-time and startup checks with a clear
  message.
- **SUG-42 — Make data operations in the wrapper reproducible** (Low, M09). The
  headless backup, restore, import and export commands first rebuild from the working
  tree (`scripts/dev.sh:111-112`). User data is therefore processed by whatever
  uncommitted code is checked out, and the command fails if that code does not
  compile. The image also uses a moving `debian:trixie-slim` tag with unpinned
  packages (`Dockerfile.dev:1`). Warn on, or refuse, a dirty tree for data commands.
  For release builds, pin the base image digest and package snapshot (already an open
  decision).

### Scope decisions needed

- **SUG-43 — Record decisions on missing management operations.** Several management
  operations are missing:
  - saved views cannot be deleted;
  - property definitions cannot be renamed or deleted;
  - trash cannot be emptied (SUG-05);
  - files and images cannot be dropped into the editor;
  - UI strings are not translatable (`tr()` is unused).

  None of these is a defect, and none is authorized work. Record whether each belongs
  to the first release or later, so that none is added implicitly.

## Work log

| Date (Asia/Tokyo) | Milestone | Change and evidence | Follow-up |
| --- | --- | --- | --- |
| 2026-10-29 | M00 | Recorded the accepted discussion in this roadmap and added project management instructions. Repository inspection found no application files or commits. Document review and structural checks passed: 11 milestone definitions/table entries, 61 unique task IDs, matching exit criteria, accurate completion states, balanced code fences, and clean whitespace. Application tests are not applicable to this documentation-only change. | Begin M01 when implementation is requested; record prototype evidence before finalizing the editor design. |
| 2026-10-29 | M00, M01–M03, M05, M07–M08 | Reviewed Zim's repository and official linking, backlinks, page-navigation, and search documentation. Added cross-referencing scope, data/index responsibilities, lifecycle rules, tasks, acceptance criteria, and management guidance. Document review, whitespace checks, and structural validation passed: 11 milestones, 67 unique tasks, and all 61 earlier task IDs/statuses preserved. Application tests are not applicable; implementation remains unstarted. | Begin M01 when implementation is requested, including validation of typed inline references in the editor prototype. |
| 2026-10-29 | M01, M02 | Began execution with a C++17/Qt prototype, a Qt-independent document/tree module, 10 core and 27 interaction/comparison cases plus a launch check, two editor approaches, and measurement commands. The user selected MIT and required Docker-based dependency isolation; added the development image/wrapper and management guidance. Fixed BUG-001 through BUG-003 with regression checks. Detailed compiler, native/offscreen, and measurement evidence is in the feasibility report. | Finish M01.4 with real OS IME and screen-reader validation, then continue the production module/command foundation in M02. Storage and database milestones remain not started. |
| 2026-10-29 | M01–M03 | Added the startup folder picker with the requested default, separate host selection/data mounts, SQLite identity bootstrap, reusable desktop window, explicit comparison mode, toolbar/focus feedback, keypad handling, and a thin active-block outline/shadow. Recorded grip dots for M05.4 and moved README progress content here. Added 18 desktop cases plus launcher integration checks alongside the existing 10 core and 27 editor cases. Verification results are recorded below. | Finish M01.4 manual input/accessibility checks and M02's production operations; implement note persistence/schema and autosave in M03 before treating the app as a usable diary. |

Verification for the workspace/editor follow-up uses the existing Debian 13 image
`sha256:d96a2a1a0a4e223d4df47f2088b052961673eb5cc2f3a0e74fe4a0a0893a6f6e`,
Qt 6.8.2, SQLite 3.46.1, GCC 14.2.0 and Clang 19.1.7 on the RHEL 9.8 x86_64 host.
No host development packages were installed.

| Check | Outcome |
| --- | --- |
| `./scripts/dev.sh test gcc` | Passed all five CTest entries: core, launcher, editor, desktop, and prototype smoke. Includes the BUG-006 regression. |
| `./scripts/dev.sh test clang` | Passed the same five entries; new modules compile with warnings enabled. |
| `./scripts/dev.sh test core` | Passed core and launcher checks with the GUI disabled; no Qt or SQLite discovery/linking. |
| `./scripts/dev.sh test-native` | Passed 27 editor and 18 desktop cases on Xwayland, excluding Qt's setup/cleanup counts. Tests wait for window exposure and activation. |
| Real Docker workspace launch/reopen | Passed with a temporary host folder containing Unicode, spaces, a comma, and quotes; created the database and attachments directory and reopened it on a later launch. |
| Outline preview | Inspected the final native window screenshot; the thin outline encloses the heading with a subtle shadow and no background fill. |
| Shell and documentation checks | Bash syntax, 69 unique task IDs, balanced fences, local Markdown links, and `git diff --check` passed. |

Initial failures were corrected rather than omitted: an outline-test fixture used
the wrong block ID, the mock launcher could not execute on `/tmp`'s container
mount, native test focus needed activation waits (G16/G17), and the outline height
and unrelated-database edge case became BUG-005/BUG-006. BUG-004's exact original
session remains unconfirmed. Real OS IME and screen-reader checks remain
unperformed; no production note persistence or autosave is claimed.

Future entries should record the steps changed, checks actually performed and
their outcomes, relevant commits, encountered bugs, and the next concrete action.
Do not record tests as passed when they were unavailable or not run.

### Persistent workspace execution checkpoint — 2026-10-29–30

- Added `nebula_core` database/property/view values, `nebula_storage` records and
  JSON codec, `nebula_application` page session, `WorkspaceWindow`, and a headless
  `nebula_workspace` backup/restore tool. Qt Core handles JSON internally; the
  document core and all public application/storage values use standard C++ types.
- GCC: all six CTest entries passed, including 13 core cases, 15 storage/recovery
  cases, the existing 27 editor cases, and 27 desktop cases. Killed-process tests verify both
  rollback before commit and durability after commit. Actual view creation,
  database rename/trash/restore, table property edits, autosave/reopen, undo after
  save, failed-save close prompts, and cross-page navigation have focused tests.
- Clang and core-only passed; native Xwayland passed the full editor and desktop
  suites. The selected journal date (including a DST-transition calendar day)
  survives reopen as a date-only value; leap-year/date validation is also tested.
  A real Docker backup command succeeded without display access; the native
  workspace screenshot was inspected. Clang exposed
  a captured structured binding that required C++20, so it was rewritten for the
  agreed C++17 baseline. A nested JSON property-version validation check was added
  during final review; its focused rejection test and the final Clang suite passed.
  Final native tests also passed all 27 desktop cases. A real Docker restore into
  a new temporary workspace succeeded; source and backup remained intact.
- Next: implement
  grip-dot mouse reordering with subtree/identity/undo tests. M03 still needs
  independent deleted-block recovery and unresolved reference persistence. M05
  still needs dividers, rich clipboard, wiki entry, full link lifecycle/navigation,
  input/accessibility checks, and the remaining editor polish. M06 attachment
  ownership/import/manifest recovery, M07 search/interchange, M08 reliability,
  and Linux packaging remain unfinished. Do not infer completion from this
  checkpoint or restore the old in-memory-only README warning for workspace mode.

### Grip-dot mouse reordering checkpoint — 2026-10-30

- Added the requested six grip dots inside the active block's thin outline and
  shadow, an open/closed hand cursor, a visible drop marker, horizontal nesting,
  Escape cancellation, and edge autoscrolling. Dragging moves the whole subtree
  and makes one undoable structural edit; ordinary text selection stays native.
- Core relocation validates the prospective tree before changing the original,
  preserving IDs and rejecting descendant/cycle and invalid sibling destinations.
  Two new core cases and four editor mouse cases pass with GCC and Clang. The
  full-window drag/save/undo regression also passes. Clang passed all six CTest
  entries; the Qt-free core passed its 15 cases plus launcher checks. Xwayland
  passed all 31 editor and 28 desktop cases. The native grip preview was inspected.
- Remaining M05 work still includes dividers, rich clipboard, reference entry and
  lifecycle/navigation, undo typing coalescing, and production accessibility/IME
  verification. Completing mouse movement does not complete the editor milestone.

### Dependency and validation schedule correction — 2026-10-30

At the user's direction, actual OS IME/screen-reader acceptance belongs to M08.2.
Tasks were moved into their destination lists and renumbered in execution order,
with no transfer notices left behind.
M01/M02 satisfy their automated/Linux scope; the later checks remain unperformed.
The M03 list also follows implementation order from schema/workspace setup through
record operations, migrations, autosave, references, recovery, and verification.

### Deleted-block recovery and unresolved references checkpoint — 2026-10-30

- Completed M03 with schema version 4, transactional deleted-subtree archives,
  restoration after restart/backup, parent/page/database prerequisites, and
  preservation of edits after partial undo. Recovery records the deletion operation
  and original positions so independent sibling restores keep their order.
- Unresolved links retain intended names and database scope in canonical JSON;
  they render distinctly and never bind to another page by matching its title.
  The picker inserts unresolved links without creating pages; their activation
  explains how to choose a target. Index rebuilds include only resolved edges.
- GCC and Clang passed all six CTest entries: 16 core, 23 storage, 32 editor, and
  30 desktop cases, plus launcher and smoke checks. The migration fixtures preserve
  workspace identity, saved notes, version 3 recovery fragments, and rich content.
  Xwayland passed 32 editor and 30 desktop cases, including the actual unresolved
  picker and block restore dialog. BUG-007 has failing-before/passing-after tests
  for reverse sibling restoration and a partially recovered sibling.
- The roadmap now has 69 unique tasks in sequential milestone lists, valid local
  links, balanced fences, and clean whitespace. Scheduling history is confined to
  the work log; the README remains setup/usage documentation.
- Next: finish M05 block types, rich clipboard and typing undo grouping, followed
  by the wiki/reference navigation workflow. M06 managed attachments, M07 search/
  interchange, M08 reliability/accessibility, and platform releases remain open.
  Real OS IME and screen-reader checks have not been performed.

### Divider, clipboard, and undo checkpoint — 2026-10-30

- Completed M05.1–M05.5 and automated production checks in M05.7. Added divider
  insertion with a following paragraph, text-safe editing, and schema version 5.
  Migration rebuilds the block/link tables together; existing trees, links, and
  deleted fragments survive. Enter in an empty list exits without an extra block.
- Adjacent printable typing joins one undo group until a one-second pause,
  navigation, focus change, formatting/structure, clipboard, or composition event.
  Autosave retains those groups; page navigation/restart still clears history.
- Added a shared `nebula_format` module and versioned block clipboard. Tests cover
  hierarchy, Unicode clipping, bold/italic, cards, resolved/unresolved references,
  fresh IDs and internal-target remapping, insertion before existing suffixes and
  children, divider paste, and malformed-input rejection. Supported external HTML
  is parsed without resource loading; limitations are documented explicitly.
- GCC and Clang passed all six CTest entries: 17 core, 24 storage, 42 editor, and
  33 desktop cases, plus launcher and smoke checks. The Qt/SQLite-free core passed.
  Native Xwayland passed 42 editor and 33 desktop cases. BUG-008/BUG-009 have
  failing-before/passing-after regressions.
- The current 1,000-block benchmark measured 7 ms load, 4.18 ms typing p95, 18 ms
  adjacent move, and 34,280 KiB peak RSS on the recorded Docker/host environment.
  This single regression measurement meets the initial M01 investigation budgets;
  it is not a large-database or end-user performance guarantee.
- Next: M05.6 and M05.8–M05.10 reference lifecycle, wiki entry/completion, outgoing
  navigation/history, default/custom labels, and explicit unresolved target actions.
  Cut/paste uses copied identities; cross-page movement must preserve originals
  transactionally. Managed attachment import/retention/manifest recovery (M06),
  search/interchange (M07), release validation (M08), and packaging remain open.

### Session handoff — 2026-10-30

Work stops here at the user's request to log off for the night. The recovery and
roadmap cleanup checkpoint is `eb26eaf`; divider/clipboard/undo work is the next
checkpoint on `main`, accompanied by this handoff. No new feature work should
begin as part of this shutdown.

- **Milestones:** M00–M04 are Complete. M05 remains In progress with M05.6 and
  M05.8–M05.10 open. M06 is In progress: the journal template/date workflow works,
  but managed attachments are unfinished. M07–M09 are Not started.
- **Current implementation:** C++17/Qt 6 Widgets/SQLite, MIT; modules are core,
  format, storage, application/session, editor, desktop, and CLI. The JSON codec
  now lives in `include/nebula/format/codec.hpp` and `src/format/codec.cpp`.
  Core builds without Qt/SQLite. Workspace schema is version 5 with transactional
  upgrades from versions 1–4; payload/clipboard JSON remains version 1.
- **Data behavior:** normal launch asks for a workspace, defaulting to
  `~/Document/nebula/`. Notes/properties/views persist, autosave waits 500 ms and
  flushes before navigation/close, and failed saves retain pending edits. Undo is
  per page, survives autosave, and ends on page navigation/restart. Trash includes
  databases, pages, and independently recoverable deleted block fragments.
- **Development environment:** use the existing Docker CLI/image and
  `scripts/dev.sh`; no host Qt installation or Podman is needed. GCC/Clang/core
  and native checks are recorded above. GUI launch needs the existing X11/Xwayland
  socket and Xauthority. Build outputs and temporary previews remain ignored.
- **Known limits:** card blocks do not own files. Backup copies the attachment
  directory but has no managed-file manifest/retention verification yet. Cut/paste
  creates copied identities; default labels, cross-page block transfer, wiki
  completion, outgoing links/history, and explicit unresolved create/select actions
  remain incomplete. Real OS IME/screen-reader checks are unperformed;
  installed IBus/Orca do not establish acceptance.
- **Resume point:** read `AGENTS.md` and this roadmap, inspect `git status` and the
  latest commits, then continue M05.6/M05.8–M05.10 with focused tests. Start with
  stable-ID reference lifecycle and transactional block transfer before layering
  on wiki completion and navigation history. Preserve the local-only scope,
  isolated toolchain, clean ordered roadmap lists, and trailer-free Git messages.

### Stable reference lifecycle and workspace moves checkpoint — 2026-10-30

- Completed M05.6. References resolve directly by permanent page/block ID and
  current block owner, distinguishing trashed pages/databases and unavailable
  targets. Following flushes pending edits first; duplicate or reused titles never
  redirect references. Default/custom display-label updates remain M05.10.
- Added Workspace menu commands for moving a page between databases and moving
  the active block subtree to another page. Page moves preserve stored typed
  values by matching property name/type and adding missing definitions; conflicts
  roll back the membership, values, revision, and any new definitions. Views retain
  their settings over the same page identities.
- Block transfers preserve IDs, descendants, rich content, and internal/external
  targets while changing both trees, both revisions, and outgoing edges in one
  transaction. An emptied source gets a fresh paragraph. No deleted fragments
  are created for transferred blocks. References and aggregated backlinks follow
  the current owner across reopen, index rebuild, and backup/restore.
- Workspace moves flush edits and reset the open page's text undo. The latest
  block transfer has separate window-lifetime undo/redo across navigation; both
  pages must remain active with matching revisions. It refuses later changes
  instead of overwriting them and preserves unrelated pages' text undo. This
  boundary is documented in the README and workspace format; it does not add
  persistent workspace history. Workspace schema remains version 5.
- Fixed BUG-010 (move-dialog focus/activation) and BUG-011 (false autosaves from
  shortcut modifiers and navigation), with failing-before/passing-after checks.
  Initial GCC runs exposed those failures, including a timeout when the unexpected
  stale-revision dialog blocked the shortcut test. The final tests also check
  failed-save move prevention, cancel, keyboard shortcuts, focus, exact moved-block
  navigation, and refusal to undo over later edits.
- **Verification:** `./scripts/dev.sh test gcc` and `test clang` passed all six
  CTest entries: 19 core, 30 storage, 43 editor, and 38 desktop cases, plus launcher
  and smoke checks. `test core` passed core/launcher without Qt or SQLite.
  `test-native` passed all 43 editor and 38 desktop cases on Xwayland (counts
  exclude setup/cleanup). Storage cases inject failures during transfer/undo and
  property remapping and verify rollback, revisions, links, reopen, and backup
  restore into a fresh workspace. Checks used the existing Debian 13 image
  `sha256:d96a2a1a0a4e223d4df47f2088b052961673eb5cc2f3a0e74fe4a0a0893a6f6e`,
  Qt 6.8.2, SQLite 3.46.1, GCC 14.2.0, and Clang 19.1.7 on the RHEL 9.8 x86_64
  host. No host dependencies were installed. Documentation/task/link/whitespace
  checks passed. Real OS IME and screen-reader checks remain unperformed;
  no new performance claim is made by this change.
- **Next:** M05.8 wiki entry/completion and picker polish, M05.9 outgoing links/
  source context and back/forward position history, and M05.10 default/custom
  labels and explicit unresolved create/select-target actions. M05 stays In
  progress; M06 managed attachments and later milestones remain unfinished.

### Wiki entry and completion checkpoint — 2026-10-30

- Committed and pushed the verified M05.6 checkpoint as `2f162c3` on `main` using
  the configured author and a single-line message without trailers.
- Completed M05.8 with a Qt-independent wiki parser, `[[Page]]`, heading/block
  targets, current-page anchors, custom labels, escaping, Unicode, and invalid
  input preservation. Typed/IME-committed expressions resolve unique matches by
  ID; duplicate names open explicit selection. Unknown page names remain unresolved
  without creating pages; missing precise targets remain text. Conversion undo
  restores the original expression, and redo retains the chosen identities.
- Added completion using Qt's documented custom-completer pattern and a searchable
  target picker. Suggestions/picker rows include database/page/type/ID context;
  same-database duplicate titles are distinguishable by ID. Completion lists up to
  100 matches; the full picker remains available. Existing pending headings on
  the current page participate in target selection. No dependency was added.
- [LINKS.md](docs/LINKS.md) defines the input/escaping and completion contract.
  Pasted wiki syntax remains text. Inserted labels remain fixed pending M05.10;
  this task does not complete default-label or unresolved create/select behavior.
- **Verification:** GCC and Clang passed all six CTest entries: 21 core, 30
  storage, 45 editor, and 41 desktop cases plus launcher/smoke. Core-only passed
  without Qt/SQLite. Xwayland passed all 45 editor and 41 desktop cases, including
  actual typed entry, keyboard popup selection, duplicate-target choice/cancel,
  escaped names, persistence/backlinks, composition commit, and undo/redo. The
  environment is unchanged from the preceding checkpoint. A newly unused helper
  produced a compiler warning during the initial build; it was removed and final
  GCC/Clang builds are clean. Whitespace, ordered-task, Markdown-link and fence
  checks passed. Actual OS IME and reader acceptance remain unperformed.
- **Next:** execute M05.9 outgoing/source-context navigation, keyboard following,
  and back/forward position history, followed by M05.10 and the managed-attachment
  milestones. M05 remains In progress.

### Link context and navigation history checkpoint — 2026-10-30

- Pushed M05.8 commit `6f8c148` to `origin/main` with no commit trailers.
- Completed M05.9. Incoming links include source excerpts and exact target block
  IDs; outgoing links include source context and target state, including explicit
  unresolved names. Resolved outgoing edges use the derived index; unresolved
  entries come from the current page's canonical runs and never enter that index.
  Query/rebuild/trash/transfer checks verify their order and ownership.
- Ctrl+Enter follows the reference at the editor cursor. Incoming/outgoing lists
  activate with Enter. Workspace reference routing replaces the sample handler so
  source positions are captured before same-page navigation changes the cursor.
- Added window-lifetime Back/Forward (Alt+Left/Right) with up to 200 prior stable
  page/block positions, selection offsets and vertical scroll. Navigation across
  database table selection retains the departing position. New destinations clear
  forward history. Missing blocks fall back visibly to their original page;
  trashed destinations and failed saves preserve entries and pending edits for
  retry. Workspace moves do not add artificial history entries.
- **Verification:** GCC and Clang passed all six CTest entries (21 core, 31
  storage, 47 editor, 45 desktop cases plus launcher/smoke). Native Xwayland passed
  all 47 editor and 45 desktop cases. Tests exercise actual shortcut and list
  keyboard routing, same-page jumps, cross-database selection/scroll restoration,
  forward-branch clearing, missing/clamped positions, and failed navigation saves.
  The environment remains the recorded Debian 13/Qt 6.8.2 image. The first build
  caught a mixed-type `auto` declaration in the new history method; splitting the
  declarations fixed compilation. No core implementation changed, so the separate
  core-only configuration was not rerun. Documentation/link/status/whitespace
  checks passed. OS IME/reader acceptance remain unperformed.
- **Next:** M05.10 explicit unresolved create/select actions and default/custom
  labels through rename and undo/redo; then M06 managed attachments. M05 remains
  In progress until that behavior and its exit criteria have evidence.

### Default labels and explicit unresolved actions checkpoint — 2026-10-30

- Pushed the M05.9 checkpoint `185d465` to `origin/main` without trailers.
- Completed M05.10 and M05's deliverables/exit criteria. Unresolved activation
  offers explicit existing-target selection or new-page creation in a chosen
  database. Selection replaces only the activated occurrence and preserves rich
  custom labels. New page/source binding/revision/index writes commit together;
  injected failures and stale source revisions leave no orphan page or partial
  binding. Undo restores intended name/scope while retaining the explicitly
  created page; redo reuses its original ID. Name reuse alone never binds a link.
- Workspace schema 6 enables block JSON version 2 with explicit `custom_label`.
  Versions 1–5 migrate transactionally; old payloads/fragments remain readable and
  their fixed labels become custom. Document/clipboard/property/view envelopes
  remain version 1. Old application versions reject the newer workspace marker
  instead of silently dropping label behavior. Backup/restore accepts version 6.
- Default labels derive target titles/text by ID on open/edit, including incoming/
  outgoing excerpts. Current-page heading/title changes and their default labels
  undo together; direct label edits become custom and undo restores mode. The
  editor's temporary Qt title metadata participates only in native undo and is
  excluded from canonical JSON; `pages.title` remains authoritative. Default
  references inside block titles use `[link]` to bound recursive labels.
- Fixed BUG-012: Ctrl+Enter at a just-inserted link's end now inspects the previous
  document character after insertion clears the typing format. Both the focused
  keyboard case and the full-window unresolved-create case have regression checks.
- **Verification:** GCC and Clang passed all six CTest entries: 22 core, 34
  storage, 51 editor and 49 desktop cases plus launcher/smoke. Core-only passed
  without Qt/SQLite. Native Xwayland passed all 51 editor and 49 desktop cases.
  Coverage includes legacy payload preservation, unknown/malformed new payload
  rejection, default-label clipboard remapping, rename/undo, exact unresolved
  occurrence selection, target database choice, transactional failure rollback,
  restart/backup restore, and prior editor/window regressions. The recorded
  Docker/toolchain environment is unchanged; no host dependencies were installed.
- The 1,000-block measurement was 14 ms load, 14.92 ms typing p95, 84 ms adjacent
  move and 33,724 KiB peak RSS. This single run, made while compiler/native checks
  were also active, remains within the M01 investigation budgets; it is not a
  statistical or release performance claim. Ordered tasks, statuses, local links,
  balanced fences and `git diff --check` passed. Actual OS IME/screen-reader checks
  remain M08 work; none is claimed here.
- **Next:** execute M06.1–M06.3 managed attachment import/ownership, interruption
  and cleanup rules, and database-plus-file manifest backup/restore. M06 remains
  In progress; M07–M09 remain Not started. Current schema/payload contracts are in
  [WORKSPACE_FORMAT.md](docs/WORKSPACE_FORMAT.md), and interactive behavior is in
  [LINKS.md](docs/LINKS.md).

### Managed attachments and verified file recovery checkpoint — 2026-10-30

- Pushed M05 completion commit `6ab3ab7` to `origin/main` with no trailers.
- Completed M06.1–M06.3 and the milestone's exit workflow. New image/file imports
  copy regular source bytes into managed storage, preserve basename/size/SHA-256,
  and atomically commit metadata/page/index changes. File and directory flushes
  precede the SQLite commit; failures retain the old page and clean uncommitted
  files where possible. Source modification/removal does not change managed bytes.
  Symlinks/special files are rejected, including before image-reader access.
- Added a separate `nebula_files` adapter using existing Qt Core hash/JSON and
  POSIX file APIs. Image cards preview managed bytes through existing Qt Gui
  readers with bounded decode/cache sizes. File IDs survive clipboard, movement,
  deletion/undo and reopen. Local open/copy and availability/audit controls are
  keyboard-accessible. The Docker launcher adds only an explicitly selected
  read-only `/imports` mount through `--import-root`; source folders stay unchanged.
- Schema 7 adds the attachment registry; block payload 3 carries block/inline
  managed IDs while reading payloads 1–2. Versions 1–6 migrate with preserved notes,
  legacy label-only cards and recovery fragments. No new external library was
  added.
- Shared backup locks and exclusive import/cleanup locks protect immutable files.
  Backups copy a consistent SQLite snapshot and all retained attachment bytes,
  validate registered metadata/hashes, and durably write manifest version 1.
  Restore verifies paths, exact file list, sizes, hashes and workspace identity
  before making a new workspace; the backup is unchanged. Missing/corrupt bytes
  reject backup and remove partial output. Legacy pre-7 backups remain supported.
- Fixed BUG-013 with a failing-before/passing-after cleanup/undo regression.
  Registered imports are retained even if current references disappear inside a
  surviving block. Cleanup deletes only selected unregistered leftovers after
  fresh reference/lock checks; live/trash/recovery records and retained backup
  bytes remain protected. No permanent registered-file purge is claimed.
- **Verification:** GCC/Clang passed all six CTest entries: 23 core, 42 storage,
  52 editor and 54 desktop cases plus launcher/smoke. Core-only passed without
  Qt/SQLite. Native Xwayland passed all 52 editor and 54 desktop cases, including
  actual dialog interaction, image pixels from copied bytes after original removal,
  unavailable-image rendering, export copies, undo/reopen, audit cleanup and a
  restored dated journal with both image/file bytes. Tests cover import rollback,
  stale revisions, FIFO/symlink refusal, orphan fixtures, recovery/trash retention,
  active file locks, modified/missing files and malformed/tampered manifests.
- A real `dev.sh run --workspace … --import-root … --smoke-test` succeeded with
  Unicode, spaces, commas and quotes in host folders. Real headless Docker backup
  and fresh restore succeeded with a manifest and unchanged source fixture. The
  image/toolchain is unchanged from M05; no host packages were installed. Bash
  syntax, ordered tasks, local links, fences and `git diff --check` passed.
  Interrupted-import handling uses orphan fixtures and transactional failures;
  process-kill tests at every file/rename/commit boundary remain M08 work. External
  desktop-handler opening, actual OS IME/reader checks remain unperformed.
  Imports/audits are synchronous; large-file responsiveness is not claimed.
- **Next:** M07.1–M07.2 indexed search/tokenization and navigation, M07.3–M07.6
  Markdown/lossless interchange and validation, and M07.7 reference filters in
  search/views. M00–M06 are Complete; M07–M09 remain Not started at this checkpoint.

### Indexed local search checkpoint — 2026-10-30

- Pushed attachment milestone commit `161e640` to `origin/main`. Completed
  M07.1–M07.2; M07 is In progress. Schema 8 adds an FTS5 index with block and
  whole-page rows for titles, content, managed-file labels and typed property
  text. Edits, target-label changes, block transfers and index updates commit
  together; canonical joins exclude trashed pages/databases immediately.
- Added Ctrl+Shift+F workspace/database search, explicit result selection,
  matching-block navigation and existing position history. Index repair rebuilds
  from canonical records before saving pending edits, so a missing index cannot
  prevent repair. Restore rebuilds derived search data without changing note IDs
  or revisions. Unsupported/malformed migration input rolls back unchanged.
- Defined literal AND terms and optional trailing prefix *, NFKC/case folding,
  Latin accent folding and character segmentation for CJK substring matching.
  Representative Latin/fullwidth, Japanese, Chinese, Korean and Arabic fixtures
  pass. No stemming, transliteration or emoji-only query support is claimed.
  Search limits and behavior are in [SEARCH.md](docs/SEARCH.md). Existing SQLite
  must include FTS5; no external dependency or baseline increase was added.
- **Verification:** GCC and Clang passed all six CTest entries, including 48 storage,
  56 desktop, 52 editor and 23 core cases plus launcher/smoke. The new checks cover
  typed properties, cross-block terms, duplicate-title database scope, edits,
  trash/restore, literal escaping, Unicode/NUL preservation, default labels after
  rename/transfer/removal, failed-save rollback, v7 migration/rollback and backup
  restore with missing index rows. Native Xwayland passed 52 editor and 56 desktop
  cases, including the search shortcut, Enter selection, exact block, Back history,
  cancel, scoped results and repair with pending edits. The initial shortcut test
  needed to wait for modal-to-window activation before sending Back; both
  offscreen and native checks then passed. Actual OS IME/reader checks
  remain unperformed. Build environment is unchanged from M06. All 69 unique
  task IDs, local documentation links/fences and `git diff --check` passed.
- **Next:** M07.3 Markdown subset, M07.4–M07.6 lossless portable interchange and
  validation, then M07.7 reference filters. M08–M09 remain Not started.

### Markdown conversion checkpoint — 2026-10-30

- Pushed search commit `a679af4` to `origin/main`. Completed M07.3 with a
  separate `nebula_interchange` target using existing Qt Core/SQLite/file adapters.
  The documented subset supports paragraph/heading/list/to-do/divider blocks,
  asterisk emphasis, soft breaks, empty blocks, explicit page/block links and
  copied managed image/file assets. Export uses safe stable-ID filenames/anchors;
  import allocates new page/block/file IDs before resolving forward references.
  Duplicate titles remain distinct, missing targets remain unresolved, and
  pre-existing pages are never overwritten.
- Added GUI folder controls with conversion reports and headless Docker
  `export-md`/`import-md` commands. Exports also write CONVERSION.txt. Markdown
  drops properties/views/timestamps/trash/recovery/identities, flattens nesting,
  fixes default labels and turns out-of-set/unresolved destinations into label
  text with warnings. Remote/unsupported links become reported text without
  fetching. See [MARKDOWN.md](docs/MARKDOWN.md) for the exact application subset;
  general CommonMark compatibility is not claimed.
- Imports save new pages, links, managed-file metadata and search indexes in one
  transaction. Malformed UTF-8/NUL text, duplicate anchors, traversal/symlink/
  missing attachments and SQL failures preserve existing data and clean copied
  uncommitted files. Export refuses existing destinations and managed attachment
  locations; missing managed bytes remove partial output.
- **Verification:** GCC/Clang passed all seven CTest entries, including six
  interchange, 48 storage, 57 desktop, 52 editor and 23 core cases plus launcher/
  smoke. Tests cover rich text/soft breaks, duplicate titles, precise block
  remapping, forward/missing links, non-ASCII unsafe block IDs, images/inline assets,
  actual file bytes, explicit loss reports, remote resource refusal, invalid input,
  transaction rollback and partial-export cleanup. Native Xwayland passed all 52
  editor and 57 desktop cases including the import/export dialogs, visible report,
  pending-edit flush and cancel. The dialog test fills the visible filename entry
  because changing a preselected filename through selectFile alone did not update
  the shown save entry. Real Docker GUI workspace creation followed by headless
  Markdown import/export passed with Unicode/comma/quote folder names and unchanged
  original attachment bytes at `/tmp/nebula-m07.BQJ8dY`. All 69 task IDs,
  prose documentation links/fences, Bash syntax and `git diff --check` passed.
- **Next:** implement lossless JSON and complete M07.4–M07.6 validation/round trips,
  then M07.7 reference filters. M07 remains In progress; M08–M09 remain Not started.
  The Linux environment is unchanged; OS IME/reader remain unverified.

### Lossless interchange checkpoint — 2026-10-30

- Pushed Markdown commit `a262563` to `origin/main`. Completed M07.4–M07.6.
  Version 1 JSON exports structured canonical records and a verified attachment
  file manifest. It preserves all nine canonical tables, explicit order, IDs,
  rich text, typed/unset properties, views, timestamps/revisions, trash, recovery,
  reference label/unresolved states and retained/legacy file bytes. Decimal int64
  strings protect large revisions from other JSON readers' number limits.
- JSON import requires a fresh directory, rejects existing destinations and
  unsupported/unknown/duplicate fields, validates trees, relationships, property
  ownership/types, view settings, recovery records, timestamps and file paths/
  sizes/hashes, then rebuilds links and search from canonical content. Original
  exports remain unchanged. Failure removes partial new output. No merge or
  overwrite mode is implied. Markdown imports instead remap new identities into
  an existing database; both strategies are documented in
  [INTERCHANGE.md](docs/INTERCHANGE.md).
- Added GUI JSON export and import into a separately owned workspace/window,
  plus headless Docker commands with read-only source mounts. Imported windows
  keep their workspace alive through close/save/destruction. Editing the imported
  window does not change original notes. Export manifests are limited to 64 MiB;
  filesystem permissions/empty directories are outside the document model.
- **Verification:** GCC and Clang passed all seven CTest entries: 10 interchange,
  48 storage, 58 desktop, 52 editor and 23 core cases plus launcher/smoke. Full-model
  export/import/re-export JSON objects compare equal, including all table rows,
  unknown/unresolved targets, duplicate titles, rich/typed/NUL content, large
  revisions, restored/unrestored recovery, trashed databases/pages, views,
  images/inline assets and unused registered/legacy bytes. Tests reject malformed
  versions, unknown fields, numeric revision coercion/overflow, invalid flags,
  duplicate IDs/escaped duplicate keys, invalid trees/order, mistyped values,
  unknown view columns, recovery mismatch, invalid timestamps and missing/
  modified/unsafe/unlisted files without changing existing notes.
- Native Xwayland passed all 52 editor and 58 desktop cases, including the actual
  JSON dialogs and editing/saving/closing a fresh imported window after the import
  function returns. Real Docker JSON export/import through Unicode/comma/quote
  paths preserved every canonical row and file byte in the M07 disposable fixture.
  Clang identified a C++20 structured-binding capture extension; the code now uses
  ordinary C++17 references. OS IME/reader and crash-kill boundaries remain
  separately required. GitHub repository access is available.
  All 69 task IDs, prose links/fences, Bash syntax and `git diff --check` passed.
- **Next:** M07.7 structured incoming/outgoing reference filters in search and
  saved views. M07 remains In progress; M08–M09 remain Not started.

### Pasted-link preview scope addition — 2026-10-30

- Added M07.8 at the user's request: attempt a removable text-only preview on
  pasted links, without pictures or thumbnails. The user explicitly selected
  “Fetch text on paste only” after clarification of the prior no-runtime-network
  rule. This authorizes one-off page text retrieval for the paste workflow;
  ordinary notes, storage and existing local references remain usable offline.
  Implementation and verification follow the current M07.7 checks.

### Reference query checkpoint and execution stop — 2026-10-30

- Pushed lossless interchange commit `841f851` to `origin/main`. Completed
  M07.7 with stable-ID incoming/outgoing reference conditions in search and saved
  views. Both include references to blocks on their current owning pages, work
  without FTS terms, and combine with text/database/property filters. Duplicate
  titles, unresolved names, renames, moves and trash retain explicit identities.
  Up to eight AND-combined conditions are supported, including explicit missing
  selected IDs. The UI preserves multiple conditions and cancelled edits.
- Schema 9 marks view payload 2 while preserving legacy payload 1. JSON version 1
  now exports schema 9 and also imports schema 8 with legacy view settings. Mixed
  unsupported schema/view versions are rejected. Core property-only apply_view
  refuses reference conditions requiring workspace context; Workspace::query_view
  evaluates references and typed properties over the original pages.
- Search's repair action reconstructs both reference and FTS indexes before
  saving pending edits, including when either derived table is missing. This
  leaves canonical note IDs, timestamps and revisions untouched by repair itself.
  Contract details are in [SEARCH.md](docs/SEARCH.md),
  [WORKSPACE_FORMAT.md](docs/WORKSPACE_FORMAT.md) and
  [INTERCHANGE.md](docs/INTERCHANGE.md).
- **Verification:** GCC/Clang passed all seven CTest entries, including 51 storage,
  11 interchange, 60 desktop, 52 editor and 23 core cases plus launcher/smoke.
  Core-only passed without Qt/SQLite. Focused checks cover independent structured
  queries with a missing FTS table, both relation directions, block transfer
  ownership, duplicate titles, combined conditions, trash, index repair, typed
  saved views/reopen, v8 migration, invalid condition rejection, lossless filter
  round trips and old JSON schema compatibility. Native checks initially failed
  two assertions; the retained search shortcut failure is tracked as BUG-014.
  Two captured retries passed all 52 editor and 60 desktop cases, the final one
  with explicit activation/focus preconditions. No product fix is inferred from
  those passing retries; further stress checks remain M08 work. The development
  environment is unchanged. All 70 task IDs are unique and ordered; status,
  documentation link/fence and `git diff --check` checks passed.
- **User-requested stop:** finish this checkpoint, commit/push and wait for the
  user to resume after usage resets. M07.1–M07.7 are complete; M07 remains
  In progress because the newly requested M07.8 is not implemented. Its scope
  explicitly permits text retrieval on paste only, with removable previews and no
  images/thumbnails. AGENTS.md records that narrow exception. No preview network
  code/dependency was introduced in this checkpoint. M08–M09 remain Not started.
  Resume with M07.8, then reliability/accessibility/performance and release work;
  actual OS IME and screen-reader verification remain unperformed.

### Architecture, security and quality review — 2026-10-30

- At the user's request, reviewed all 63 tracked files at `761dd84` for
  vulnerabilities, defects and quality issues: source, tests, CMake, Docker tooling
  and documentation. The findings and proposals are recorded as SUG-01–SUG-43 in
  [Suggestions](#suggestions). No application code, task list or milestone status
  changed.
- **Checks:** the review was static only, reading code paths and existing tests. No
  build, test suite, benchmark or reproduction was run for it. *Confirmed* findings
  are therefore traced in code, not shown by a failing test, and *Plausible* findings
  still need reproduction. Documentation checks: `git diff --check` and local link
  review.
- **Next:** the user decides which suggestions to accept. Accepted items move into
  milestone task lists, and reproduced defects get BUG entries with regression tests.
  M07 remains In progress, with M07.8 next.

### Concurrent integration and trash-safety scope — 2026-10-30

- The user resumed roadmap execution and requested professional reference-list
  wording plus distinct, safer trash controls for blocks, pages and databases.
  Added M08.7. This independent safety/UI task is being executed ahead of the
  remaining preview work (M07.8), as explicitly prioritized by the user; the rest
  of M08 still depends on the complete M07 workflow.
- Another agent is implementing the Suggestions section. Its existing edits are
  preserved, including the storage/schema hardening and editor refactor. This
  task owns the app trash controls, dedicated UI tests and focused usage/work-log
  updates; it does not take over the Suggestions implementation.
- Captured an isolated source snapshot before changes at
  `/tmp/nebula-manual-audit-v148xw8p`. Its GCC build succeeded; seven of eight
  CTest entries passed, including storage, new storage hardening, interchange and
  desktop. The editor entry failed
  `block_kind_conversion_preserves_content_and_references`: after undo, the
  cursor identified heading instead of reference (tests/editor_tests.cpp:587).
  The live editor source changed during this check, so this is evidence about
  the captured snapshot, not a claim that the other agent's current work still
  has that failure. The full output is retained in the adjacent ignored /tmp log.
- Completed M08.7: separate red icons beside the database selector and page
  title, and in the block toolbar; scope-specific plain-text confirmations,
  default Cancel and an initially unchecked database acknowledgement. Page and
  database trash stop on save failure. Failed block trash restores the editor;
  successful block/subtree trash preserves a single undo group and saved recovery
  records. These controls never call permanent purge. Renamed visible/accessibility
  reference labels to **Referenced by:** and **References**. Usage and recovery
  details are in [TRASH.md](docs/TRASH.md).
- **Verification:** an isolated HEAD-plus-feature snapshot passed all eight
  CTest entries with GCC 14.2.0 and Clang 19.1.7, without the Suggestions changes.
  Native Xwayland passed 52 editor, 60 desktop and eight trash-safety cases.
  New checks click the real icon buttons and exercise Enter/Escape cancellation,
  disabled database confirmation without acknowledgement, duplicate names,
  pending-save failures at page/database scope, failed block-save rollback,
  nested subtree undo/redo, valid replacement of the last subtree, attachment
  retention, unavailable actions and restoring all three scopes after closing and
  reopening the workspace. Retained attachment bytes survive removal of the source.
- The combined snapshot passed core, launcher, storage, storage hardening,
  interchange, editor and smoke entries; after fixing BUG-015 its desktop and
  trash-safety entries also passed. This verifies captured integration points,
  rather than every subsequent edit by the concurrent agent. The initial copied
  GCC build cache needed a clean rebuild after switching back to HEAD APIs;
  that clean standalone build passed. No host dependencies were installed.
- All 71 milestone task IDs are unique. Local documentation links, Markdown
  fences and `git diff --check` passed. Actual OS IME and screen-reader
  checks remain unperformed. M07/M08 remain In progress; next is M07.8, followed
  by the remaining reliability, accessibility, performance and release work.

### Paste-preview adapter checkpoint — 2026-10-30

- Pushed M08.7 commit `963cdfb` to `origin/main`, with a subject-only commit
  message and the configured author. Other uncommitted Suggestions work remains
  preserved. At the user's direction, further shared-window edits wait until the
  Suggestions agent finishes its window changes.
- Began M07.8 with an independent `nebula_previews` adapter. It performs a
  cancellable asynchronous GET with a five-second total deadline, a 512 KiB
  aggregate byte limit and at most three same-origin redirects. It sends no
  cookies, credentials or Referer, retains no cookies/cache, uses no system proxy,
  rejects credential-bearing URLs and authentication challenges, and keeps normal
  HTTPS certificate/hostname checks. Only HTML/XHTML/plain-text responses yield
  bounded plain title/excerpt text; resource loading is disabled and controls,
  format characters and image placeholders are removed.
- Recorded the network decision in [PREVIEW_DESIGN.md](docs/PREVIEW_DESIGN.md):
  explicitly pasted private/loopback origins are permitted for local/intranet
  pages; redirects cannot change host, scheme or port. No image/subresource
  retrieval, background refresh or other networking is introduced. Qt Network
  adds no host installation or SDK baseline increase. Dependency, license and
  Linux TLS deployment requirements are in
  [DEPENDENCIES.md](docs/DEPENDENCIES.md).
- **Verification:** isolated HEAD-plus-adapter GCC/Clang builds passed all nine
  CTest entries. The final nine preview cases also passed focused GCC/Clang and
  ASan/UBSan Debug runs with warnings-as-errors. Fixtures cover URL rejection,
  HTML metadata/body/Unicode/plain encodings, malformed/truncated input, missing
  metadata delimiters, no image/frame requests, cookie suppression, same-origin
  redirect success/limits, cross-origin refusal, declared/streamed excess bytes,
  timeout, cancellation/destruction, HTTP/authentication errors and HTTPS trust/
  hostname validation. All requests used loopback servers inside network-disabled
  containers. The TLS backend reported OpenSSL 3.5.7 with Qt 6.8.2. Leak detection
  was not run (`detect_leaks=0`); no memory or undefined-behavior failure was
  reported by the enabled sanitizers.
- Core-only checks passed both CTest entries without Qt/SQLite. Documentation
  links/fences, the 71 unique milestone task IDs and `git diff --check` passed.
  The preview target also built with warnings-as-errors and passed all nine
  preview cases against a fresh capture of the concurrent Suggestions changes;
  that focused check does not certify the other agent's full application changes.
- **Remaining M07.8 work:** wire explicit paste and cancellation into the editor,
  preserve the original URL on every failure, insert removable canonical text
  blocks, reject stale asynchronous results, enable the narrow GUI runtime network
  capability, and verify full-window removal, save/reopen, clipboard and undo/redo.
  The application does not yet offer pasted-link previews. M07.8 stays unchecked,
  and M07/M08 remain In progress. Shared-window integration awaits the concurrent
  agent as requested; OS IME and screen-reader checks remain open.

### Suggestions implementation checkpoint — 2026-10-30

- At the user's request, implemented or decided SUG-01–SUG-43; outcomes are in
  [Implementation status](#implementation-status--2026-10-30). Workspace schema 10
  adds derived structures only (reverse-link index, recovery membership, maintenance
  timestamp, page/block search rows). Versions 1–9 migrate transactionally and the
  lossless JSON contract stays at schema 9. New modules: `nebula::interchange` free
  functions over `WorkspaceInternals`, Qt-free application services (navigation
  history, property text, link targets), a termination-signal adapter, storage error
  categories and a shared schema range.
- User-visible additions: Empty trash permanently…, Delete view, Rename/Delete
  property, Keep my version as a new page, Check workspace integrity…, Paste plain
  text (Ctrl+Shift+V), import progress/cancel, read-only opened copies, folder checks
  in the chooser, and private file modes. README, TRASH.md and the format,
  attachment, search, interchange, Markdown and clipboard documents describe them.
- **Concurrent work in the same tree (G27):** another agent committed `963cdfb`
  (M08.7 trash controls) and `382aad4` (M07.8 preview adapter) while this work was
  uncommitted. BUG-015, recorded by that agent, was introduced by this work's
  Restore-dialog edit and fixed by them in the shared tree. It explains the restore
  failure that hung this session's desktop runs until their fix landed; later
  isolated and full runs passed. Their files and roadmap sections were left as
  written. The only edit to their committed text is the Empty trash paragraph in
  TRASH.md, which had anticipated this operation. SUG-01 is addressed by their M07.8
  design.
- **Checks on the final combined tree:** `./scripts/dev.sh test gcc`, `test clang`
  and `test asan` (Debug, AddressSanitizer + UBSan, leak detection off) each passed
  all 13 CTest entries with warnings as errors: core, launcher, previews, storage,
  storage_hardening (22 cases), interchange, editor, desktop, trash_safety,
  review_desktop (11), fuzz_smoke (five targets), app (3) and smoke. `test core`
  passed 2/2 without Qt/SQLite. `test-native` on Xwayland passed 58 editor, 60
  desktop, 11 review-desktop and 8 trash-safety cases (setup/cleanup excluded).
  `stress 10` (and an earlier `stress 15`) repeated the interaction suites without
  failure. Launcher tests cover staging, refused existing destinations and the path
  that warns outside a Git checkout. Environment: the existing image
  `sha256:d96a2a1a…6f6e`, Qt 6.8.2, SQLite 3.46.1, GCC 14.2.0, Clang 19.1.7, RHEL 9.8
  host. The image was not rebuilt and no host packages were installed.
- **Failures corrected during implementation:** BUG-016 (undo caret after ranged
  rewrite); trigger-based fault injection replaced (G24); QCollator numeric order
  (G25); a stale-save path reported a validation error before the conflict (now
  conflict first); a new JSON test expected the overflow refusal one save early and
  was corrected. The product defects were caught by failing tests before their fixes.
- **Integrated benchmark** (`benchmark workspace`, offscreen, same host/image; the
  original code was built from `761dd84` in a temporary worktree with the same
  fixtures, adapted only for missing APIs). After values are the range over four
  runs, the first two measured before the search-pass optimization:

| Measurement | `761dd84` | After |
| --- | --- | --- |
| Typing p95, 10,000-block page | 104.3 ms | 4.4–8.2 ms |
| Save after typing, 10,000-block page | 11,270 ms | 76–131 ms |
| Adjacent move, 10,000-block page | 298 ms | 31–50 ms |
| Open 10,000-block page | 755 ms | 369–603 ms |
| Save after editing a page with 1,200 referrers | 9,113 ms | 0–1 ms |
| Save after renaming that page | 8,608 ms | 163–292 ms |
| Typing p95, page with 200 images and default labels | 10.1 ms | 0.9–1.0 ms |
| Import 200 images | 11.7 s | 1.1–1.8 s |
| Switch to 5,000-entry Journal table | 177 ms | 77–137 ms |
| Search "diary entry" | 36 ms | 15–32 ms (82 ms before skipping the cross-block pass) |
| Reopen workspace | 124 ms | 0–1 ms (weekly check not due) |
| Peak RSS | 90,292 KiB | 74,556–77,692 KiB |

  The editor-only `benchmark single` measured 6.8 ms typing p95 and 5 ms move.
  Proposed M08.3 investigation budgets for these fixtures (not yet gates): 16 ms
  typing p95, 250 ms save after body edits, 1 s for a hub rename, 1 s page open, 300
  ms table switch and 128 MiB peak RSS.
- **Not verified:** the CI workflow has not run on a GitHub runner yet; libFuzzer
  and clang-format are absent from the current image, so only the deterministic
  fuzz driver ran; real OS IME, screen-reader and GUI/CLI lock contention checks
  remain unperformed.
- **Pending work:** the remaining follow-ups for SUG-22, SUG-28, SUG-34, SUG-36,
  SUG-38 and SUG-40 are scheduled as M08.8. M07.8 editor integration of link
  previews (owned by the concurrent preview work), the other M08 tasks and the
  M09 release work remain open. Drag-and-drop and UI translation are deferred
  beyond the first release. M07 and M08 stay In progress.
- At the user's request this checkpoint is committed with a subject-only message
  and the configured author, without co-author or other trailers. The other
  agent's uncommitted `.gitignore` change was left unstaged. The push result is
  recorded in the next checkpoint.

### Suggestions integration verification — 2026-10-30

- The user confirmed that the concurrent Suggestions agent finished. Captured
  its complete working tree at `/tmp/nebula-suggestions-final-fcjp3lg7` before
  continuing M07.8 window integration. Preserved the schema/editor refactor,
  recovery fix, documentation, test tooling and development-only CI workflow.
- **Checks:** the earlier captured integration passed all 13 CTest entries after
  a clean GCC build with warnings-as-errors. Compared the final capture with
  that source: code differences were the final search optimization and storage
  hardening tests, plus tooling and documentation updates. The final capture's
  GCC build and all 13 CTest entries passed as well. This is direct verification
  of the captured source; the other agent's Clang/ASan/native/stress/benchmark
  results remain separately recorded in its checkpoint above.
- M07.8's independent controller preparation passed 12 behavior cases with GCC,
  Clang and native Xwayland (setup/cleanup excluded). The tests attach the
  controller explicitly; automatic paste hookup and launcher networking still
  need integration before M07.8 can be marked complete. Actual OS IME and reader
  acceptance remain M08 work. The host has IBus and Orca 40.3 executables, but
  IBus currently reports no connected bus/engine; their availability alone does
  not establish input-method or screen-reader verification.

### Pasted-link preview integration — 2026-10-30

- Completed M07.8 and M07. Explicit whole-URL pastes start one bounded request;
  normal, plain-text and canonical plain URL paste share the workflow. Internal
  references and ordinary text containing a URL never fetch. External HTML next
  to a whole URL is discarded, including images. The original URL is immediately
  editable and remains on failure or cancellation.
- The application-owned controller inserts successful title/excerpt text as an
  ordinary sibling paragraph after the pasted block's subtree. It preserves
  selection/scroll, defers through composition and modal operations, cancels
  obsolete results, and provides a keyboard-accessible Cancel preview button.
  Preview insertion is a separate undo step; trash removes just the preview.
  Persistence, clipboard, recovery, backup and JSON retain canonical text; reopen
  and undo/redo perform no network requests. Save failures retain pending edits.
- Only the normal Docker workspace GUI now uses bridge networking. The chooser,
  build/test, demo/smoke and data commands remain network-disabled. The fetcher
  keeps its existing size/deadline/redirect/TLS restrictions. README and
  [PREVIEW_DESIGN.md](docs/PREVIEW_DESIGN.md) describe the shipped behavior.
- **Checks:** final GCC and Clang suites each passed all 14 CTest entries with
  warnings-as-errors. ASan/UBSan initially found BUG-018; after its fix all 14
  entries passed (leak detection remains disabled). Core-only passed 2/2. Native
  Xwayland passed 58 editor, 60 desktop, 11 review-desktop, 18 pasted-preview and
  eight trash-safety cases, excluding setup/cleanup. The earlier native failure
  remains BUG-019. URL extraction fixtures use container loopback only.
- Rebuilt the isolated development image for the pending M08 tooling work:
  `sha256:d80b4106d71df36528c29f34b28803f6bf74f8f47f7938fce3d1c66180a66e5d`;
  Qt 6.8.2, GCC 14.2.0, Clang 19.1.7, SQLite 3.46.1. No host dependencies were
  installed. The existing real GitHub runner passed commit `30978de`:
  [checks run](https://github.com/oppenheimmer/nebula/actions/runs/41022337351).
  That run does not certify these later unpushed changes. OS IME/screen-reader
  acceptance remains unverified. M08 continues;
  M09 remains Not started.

### Reliability and application-service continuation — 2026-10-30

- Resumed the uncommitted M08 preparation without discarding it. Split the full
  M08.8 follow-up requirements into M08.8–M08.13 so each has separate completion
  evidence. Added process-kill checkpoints before/after page and trash commits,
  during attachment copy, after file promotion and after attachment commit. Each
  case reopens, audits, rebuilds derived indexes, backs up/restores and exports/
  imports while preserving note IDs, reference targets, dates and retained bytes.
- M08.8 now allocates Link… picker items in batches of 100 pages and blocks.
  Search covers every cached target and current unsaved text; expansion loads
  children on demand. Continuations support Enter/double-click. Persistent model
  indexes cancel queued continuations when a search replaces the tree. Widget
  tests cover paging, late Unicode matches, empty queries and queued rebuilds;
  full-window tests cover insertion, autosave, undo/redo and backlinks.
- M08.9 extracts typed view-edit parsing/validation/persistence and Markdown/JSON
  coordination into Qt-free application services. Window code retains dialogs
  and display updates. Focused service tests cover invalid typed filters without
  mutating a saved view, settings preservation, all four interchange routes,
  destination refusal and independent ownership of imported workspaces.
- M08.10 shares length-aware UTF-8 conversion/validation across desktop, storage,
  format, interchange and attachment manifests, retaining error types and embedded
  NUL behavior. Added invalid/truncated/overlong UTF-8, Japanese and emoji checks.
  GCC 14's `maybe-uninitialized` diagnostic on inlined temporary optional members
  required named preview runs; warnings remain enabled and fatal.
- Initial GCC, Clang and ASan/UBSan suites each passed all 15 CTest groups before
  the final service and large-fixture changes. Five coverage-guided parser targets
  completed 21 seconds each with ASan/UBSan: wiki 1,931,000, codec 137,022,
  manifest 132,996, JSON 89,578 and Markdown 2,529 executions, without a reported
  finding. Leak detection remains disabled. Logs/corpus stay in ignored builds.
- The larger concurrent-maintenance fixture initially failed with SQLite code 13,
  `database or disk is full`: its data exceeded the launcher's 256 MiB `/tmp`
  tmpfs. Added the SQLite error to backup failures and placed that fixture in the
  ignored build directory via `NEBULA_TEST_TMPDIR`; production backup locking was
  not changed. An initial 64 MiB body fixture also correctly exceeded the JSON
  manifest limit once metadata was included; the final supported fixture contains
  2,001 pages with 32 MiB of body text.
- Added the first-use guide and linked it from README, covering workspace choice,
  save confirmation, keyboard controls, previews, managed files and recovery.
  Actual OS IME/reader acceptance remains unperformed: the host IBus command
  reports no connected engine; Orca availability does not establish a reader
  check.
- Final compiler/sanitizer/native, benchmark, formatting, documentation and CI
  results will be recorded below before closing this checkpoint. M08 remains
  In progress; M09 retains its dependencies and is not claimed complete.

### M08 verified reliability, services and performance — 2026-10-30

- Completed M08.1, M08.3–M08.6, M08.8–M08.10 and M08.13. M08.2 remains open
  for actual OS IME/screen-reader acceptance and investigation of the intermittent
  native activation check (BUG-019). M08.11 awaits its formatting-only commit;
  M08.12 awaits verification of this source on the real runner. M08 stays
  In progress; the Linux release remains unfinished.
- **Final checks:** GCC and Clang Release and GCC Debug ASan/UBSan each passed
  all 15 CTest groups with warnings-as-errors; core-only passed 2/2 without Qt or
  SQLite. Leak detection remains off. Native Xwayland passed 58 editor, 60 desktop,
  13 review-desktop, 18 preview-controller, nine reliability and eight trash-safety
  cases (setup/cleanup excluded). The seven process-kill boundaries and restored/
  imported attachment bytes pass. Existing storage/migration/failure, reference
  lifecycle, clipboard remapping and interchange regressions pass in these runs.
- **Concurrent maintenance:** the real window edited a 69,492,736-byte workspace
  while a separate process completed four headless backup/export cycles. Eight
  typing events produced six persisted autosaves before the final flush, with no
  visible save failure. Backup restore and lossless JSON import each retained all
  2,001 pages. The 256-page backup steps needed no locking change for this fixture;
  this is measured evidence, not a universal scheduling guarantee.
- **Performance:** selected the documented M01 budgets and proposed integrated
  budgets before measuring; [PERFORMANCE.md](docs/PERFORMANCE.md) defines fixtures,
  scope and commands. Three sequential runs on the otherwise idle reference
  machine (RHEL 9.8, Intel i5-1345U, Debian 13 container, GCC 14.2.0 Release,
  Qt 6.8.2/offscreen) all met them. Image remains `sha256:d80b4106…a66e5d`.

| Measurement | Range across all three runs | Budget |
| --- | --- | --- |
| 1,000-block load | 7–8 ms | 500 ms |
| 1,000-block typing p95 | 2.18–2.24 ms | 50 ms |
| 1,000-block adjacent move | 3 ms | 100 ms |
| 1,000-block peak RSS | 41,480–42,040 KiB | 128 MiB |
| 10,000-block open | 354–357 ms | 1,000 ms |
| 10,000-block typing p95 | 4.16–4.24 ms | 16 ms |
| 10,000-block body save | 70 ms | 250 ms |
| Hub typing p95 / body save | 0.35–0.40 ms / 0 ms at clock resolution | 16 / 250 ms |
| 200-image page typing p95 / save | 0.67–0.72 ms / 39 ms | 16 / 250 ms |
| Hub rename (1,200 referrers) | 161–165 ms | 1,000 ms |
| 5,000-entry table switch | 78–79 ms | 300 ms |
| Integrated peak RSS | 84,300–86,172 KiB | 128 MiB |

  Diagnostic results: 10,000-block move 25 ms; 200-image import 1,026–1,033 ms;
  search 15 ms; hub/image opens 40/27 ms. No outlier was discarded. These are
  reference-machine checks, not portable CI timing assertions or IME measurements.
- Final documentation checks cover local links outside code examples, balanced
  fences, 78 unique task IDs, scope/status consistency and `git diff --check`.
  No host dependencies were installed. No power-loss or actual OS IME/reader
  result is claimed. Checkpoint commit/push and subsequent
  formatting/CI outcomes are recorded in the next entry.

### Formatting and Linux CI — 2026-10-30

- Pushed reliability/services checkpoint `06cf7aa` and formatting-only commit
  `d6aac43` to `origin/main`, using the configured author and subject-only messages.
  The formatter changed 65 C++ files with no manual semantic edits. Final formatted
  GCC, Clang and ASan/UBSan suites each passed all 15 CTest groups. `format --check`
  passed. A further six-second fuzz run per parser passed: wiki 565,083, codec
  42,481, manifest 37,223, JSON 28,033 and Markdown 799 executions. No sanitizer
  finding was reported; leak detection remains off. M08.11 is complete.
- Completed M08.12: the real Linux runner passed `06cf7aac16662ca717b8c31e08b10b3644adb44b`
  ([run 41029359108](https://github.com/oppenheimmer/nebula/actions/runs/41029359108)),
  covering GCC, Clang, core-only, ASan/UBSan and ten interaction-suite repetitions.
  The formatting revision has a separate run in progress. The workflow now adds
  formatting and coverage-guided parser checks; the earlier successful run does
  not certify these newly added steps.
### Linux validation and package preparation — 2026-10-30

- Real Linux CI passed `ab7e1c48a0e600676086d0b66cb32ff2544b1f40`, including
  the new formatter and five coverage-guided parser steps, all three compiler/
  sanitizer suites, core-only and ten interaction repetitions:
  [run 41030214143](https://github.com/oppenheimmer/nebula/actions/runs/41030214143).
  The formatting-only revision also passed its own Linux run.
- Began M09 packaging preparation ahead of M08 acceptance as independent build/
  installation infrastructure; a published Linux release remains dependent on
  M08.2. Selected a Debian 13 amd64 `.deb` development preview (`0.1.0~dev1`),
  native executable/menu entry, system Qt/SQLite dependencies and separate MIT
  notice. No third-party libraries or image plugins are bundled. CPack derives
  ELF dependencies; platform plugins, fonts, cursor support and TLS trust are
  explicit. Added a dependency/image-plugin update review to the release checklist.
- Pinned the official Debian manifest and signed Debian/security snapshot to
  2026-10-30. Final development image is
  `sha256:6d842b08646e8578f18463f5650e12f20fd70c486cf6c878baa5fa06247f9a85`.
  GCC/Clang remain 14.2.0/19.1.7; Qt 6.8.2 and SQLite 3.46.1. The first CPack run
  identified missing `file(1)`; added this development-only tool inside the image.
- A new package fixture/checker verifies a user-owned dated page, stable block
  target/backlink, Unicode and managed bytes after deleting their original. The
  clean runtime has no compiler/CMake. Package install, reinstall, removal and
  reinstall preserve that workspace; offline launch/new workspace, full-window
  editing/search, Markdown/JSON, backup/restore and all nine preview/TLS cases
  passed. The runtime TLS backend was OpenSSL 3.5.7. Build/image preparation can
  fetch dependencies; fixture checks use no network. Actual checks and final
  revision will be recorded after resolving BUG-021 and rerunning this package.
- The first pinned-image GCC and Clang suites passed 15/15. ASan/UBSan exposed
  BUG-021, so M08.13 was reopened and completion/release claims remain withheld.
  Snapshot changes and their regressions are being validated. Package/version,
  build, installation/data-location and recovery details are in
  [INSTALL.md](docs/INSTALL.md). No host dependency or package was installed.

### Export-contention fix verification — 2026-10-30

- Fixed BUG-021 and completed M08.13 again. The live database is locked only
  during bounded snapshot-copy steps; long JSON/Markdown validation and file work
  use a private query-only disk copy. Temporary files use private modes and are
  removed before success. Export rejects an active source write transaction.
  Integrity, foreign-key, canonical-reference and attachment checks remain intact.
- Final GCC and Clang suites passed all 15 CTest groups, including 23 storage-
  hardening cases. ASan/UBSan passed all 15 as well; the expanded reliability group
  took 110.46 seconds. Its CTest timeout is now 180 seconds and the maintenance
  worker wait 60 seconds, based on measured sanitizer overhead and added Markdown
  coverage; production save/network timeouts are unchanged. Leak detection is off.
- Core-only passed 2/2. Native Xwayland passed 58 editor, 60 desktop, 13 review-
  desktop, 18 preview-controller, nine reliability and eight trash-safety cases,
  excluding setup/cleanup. A complete concurrent backup/JSON/Markdown cycle over
  the 69,492,736-byte database produced ten typing events and nine autosaves with
  no save failure; restored/imported documents and managed bytes stayed valid.
- The earlier false read-only FTS integrity regression (BUG-020) and existing
  migration/failure/interchange cases continue to pass. Adding the internal storage
  header exposed a test-only `sql` namespace/helper collision; renamed that helper
  before the final compiler runs. No warning was disabled.
- Remaining M08 work is M08.2: actual OS IME/reader acceptance and investigation of
  the intermittent native activation check, not replaced by these automated
  composition/focus checks. Final package validation
  follows; no released Linux package is claimed yet.

### Debian development package verified — 2026-10-30

- Completed M09.1–M09.3 as preparation; M09.4 and a published Linux release
  remain open pending M08.2 acceptance and resolution of release issues. The
  package version is explicitly `0.1.0~dev1`, not a completed first release.
- `./scripts/package-check.sh` generated the amd64 `.deb`, installed it with
  `dpkg` in a fresh pinned Debian 13 runtime image with no compiler/CMake, and
  exercised it offline. It verified program/menu/notice files, exact executable
  bytes, new/opened workspaces, 60 full-window editing/search cases, nine HTTP/TLS
  preview cases, Markdown/JSON, backup/restore and retained managed attachment
  bytes/IDs/date values. TLS used OpenSSL 3.5.7 with normal trust/hostname checks.
- A separate network-disabled image step created an unprivileged user's workspace,
  reinstalled the package, purged it, and reinstalled it again. All three checks
  preserved that workspace, references and attachment bytes. The existing schema
  migration suite separately validates versions 1–9. No earlier public package
  exists, so this is package replacement/removal plus format-migration evidence.
- Package artifact: `build-container-gcc/packages/nebula-0.1.0~dev1-x86_64.deb`;
  SHA-256 `66ed6503304d9600d484ade028b7ed80dae5da45ae12ef256b694a65c85f1a5e`.
  It stays out of Git. Installed binaries dynamically use Debian's Qt/SQLite;
  there are no copied third-party libraries/image plugins. `file(1)`'s installed
  copyright records BSD-2-Clause-alike; it is a build-only packaging dependency.
- Actual dependency metadata includes glibc ≥2.38, libstdc++ ≥14 and Qt Core ≥6.8.2;
  the selected verified target is Debian 13 amd64, not every system satisfying
  those individual constraints. Native Xwayland launch of the installed preview
  also succeeded. No runtime data folder was mounted from the user and no host
  package/dependency was installed.
- README, first-use, dependency, install/recovery and interchange documents now
  distinguish preview installation from release support. Interrupted export folders
  can retain a private full-workspace scratch database, including data omitted by
  Markdown; this is documented, and successful exports remove it. Package builds
  use explicit Release/non-sanitized/non-fuzz flags. Byte-for-byte package
  reproducibility is not yet claimed.
- The final pinned-image compiler/sanitizer/native results and BUG-021 regression
  evidence are recorded immediately above. Shell syntax, formatter, scope/status,
  local Markdown links/fences, 78 unique task IDs and `git diff --check` passed.
  The host has Python GI/pyatspi and an accessibility bus, but IBus reports no
  address/engine. Merely discovering those facilities does not complete actual
  OS IME/Orca acceptance; M08.2 stays open.

### Package-documentation closure — 2026-10-30

- Fixed BUG-022 after checking installed documentation navigation. Package tests
  now require the README, original LICENSE, project instructions, roadmap and
  install guide on disk. Added an app-specific dpkg path include to the slim
  **verification image** so it retains those docs. The complete offline package
  check passes after this change, including all 60 desktop and nine preview cases.
- Current local preview artifact (replacing the earlier checkpoint's build):
  `build-container-gcc/packages/nebula-0.1.0~dev1-x86_64.deb`, SHA-256
  `313c29e387ebd46b9df0dc9aef1d9f666009306add00a0b034d306418d4f98a2`.
  The binary implementation is `01f9535` plus the locally verified package-doc
  fix; subsequent work-log bookkeeping is not embedded retrospectively. Package
  output stays ignored. A pinned recipe is verified; bit-identical builds are
  not claimed.
- Final source documentation links/fences, 78 unique milestone IDs, shell syntax,
  formatter and `git diff --check` pass. Existing commit/push authorization was
  used with configured authorship and no trailers. M08.2 and M09.4
  remain open; no milestone or public release is marked complete without its
  remaining acceptance evidence.
- Real Linux CI passed implementation `01f9535d45a42bd3ef31f75d1b10725362f075ad`:
  [run 41033879972](https://github.com/oppenheimmer/nebula/actions/runs/41033879972).
  This includes GCC, Clang, core-only, ASan/UBSan, format, five coverage-guided
  parsers, Debian install/offline-runtime checks and ten interaction repetitions.
  The later package-documentation inclusion fix was checked locally with the full
  package script; this earlier CI run is not claimed to certify that later edit.

### Post-milestone audit scheduled — 2026-10-30

- At the user's request, added a code audit after completion of all release work.
  It requires a thorough audit followed by code simplification and organization
  using the official Google C++ Style Guide, with separate mechanical/semantic
  commits and Linux regression/performance evidence. M10 is Not started;
  this scheduling change does not start a broad refactor during M08–M09.
- Read the official guide's goals and organization/include guidance to reference
  the requested standard. Checked roadmap dependencies, task ordering, scope and
  status. Prose links/fences across 17 Markdown files, 83 unique ordered task IDs
  and `git diff --check` passed. Application tests are not required for this
  documentation-only scheduling change.

### Linux OS input/accessibility and nested Backspace closure — 2026-10-30

- Resumed the uncommitted M08.2 implementation after reading README/roadmap and
  inspecting status. Preserved its optional desktop-image work. The audit
  milestone was separately committed/pushed as
  `a6b4e45`, using configured authorship and no attribution trailers.
- Completed M08.2 and M08 on the recorded Linux profile. The optional image
  builds from the pinned recipe as
  `sha256:fbf5a48c71385692c68fe72f5b06c74203a13c504640f6d389d00e223432a4ff`.
  Actual components: Debian 13 amd64, Qt 6.8.2/GCC 14.2.0, Xvfb 21.1.16,
  Openbox 3.6.1, IBus 1.5.32, ibus-anthy 1.5.17/Anthy 0.4, Orca 48.1,
  AT-SPI 2.56.2, Speech Dispatcher 0.12.0 and PulseAudio 17.0. Runtime checks use
  network=none, a private display/bus/null audio sink, a read-only source/root
  filesystem and temporary test-user state. No host dependencies were installed.
- Real XTest keys traverse IBus: preedit updates, candidate selection and exact
  candidate commit, cancellation, Enter/split, boundary selection/merge,
  clipboard and commit/structural undo/redo pass while IDs persist. Preedit-only
  flush leaves the saved revision unchanged. BUG-024 required an editor-owned
  canonical-change signal: Qt's native text-change signal also describes visual
  composition. Autosave now ignores preedit and mode/function/Escape keys without
  suppressing native display/cursor/selection notifications. Empty replacement
  deletion remains a real grouped edit with a regression.
- F8/context-menu Block details exposes type, nesting, checked state, references
  and card labels as selectable read-only text. It preserves note text, the
  editor's selection, undo and saved revision. Real Orca speech and AT-SPI checks
  pass for headings, nested bullets, tasks, reference text/type, image/file labels,
  editor selection and focus restoration after every inspection. Fixed BUG-023
  with keyboard-selection flags, a reusable dialog and focus restoration.
- The private native profile passed 58 editor and 61 desktop cases, excluding
  setup/cleanup and the two separately exercised real-IME/reader cases. Search,
  unresolved-target choice and outgoing/backlink keyboard navigation each passed
  20 private-desktop repetitions. After explicit user approval for host display
  access, the same three workflows each passed another 20 repetitions on RHEL
  Xwayland. BUG-014/BUG-019 close as validation issues; their original timing
  causes remain unconfirmed, with no picker/product-defect fix inferred.
- The user's subsequent Backspace report reproduced BUG-025: native visual
  unindent left the application parent ID unchanged, then normalization restored
  nesting. Backspace at offset zero with no selection now invokes the existing
  subtree outdent. Empty/nonempty paragraph, heading, bullet and checked-task
  fixtures cover two levels, preserved IDs/formatting/children and separate
  undo/redo steps. The real workspace test verifies save, backlinks, no recovery
  fragments, undo/redo and reopening. Final targeted Xwayland editor/window
  checks pass, including ordinary root merging and the IME/dialog regressions.
- GCC, Clang and ASan/UBSan verified all 15 CTest groups across full runs and
  focused reruns. The new Backspace test initially sent its second key after redo
  without establishing the caret precondition; it now checks consecutive outdents
  before undo/redo. Only that failed editor group was rebuilt/repeated on all
  three configurations, and each passed. Other groups had passed the final full
  runs. Leak detection remains off. Earlier pre-fix keyboard-selection/preedit/
  mode-key/Backspace failures are retained in ignored build logs.
- Harness corrections: bounded subprocess waits, fresh and line-buffered real
  Orca logs, and separate comparisons for editor normalization versus original
  persisted card representation. These prevent false failures without changing
  reader speech or requiring a no-op save. The new CI desktop job reproduces this
  isolated profile; its real-runner result follows the push, not an assumption
  from local success.
- Rebuilt the Debian `0.1.0~dev1` preview with the final application changes.
  The complete isolated package check passed installation/reinstallation/removal
  preservation, 62 window behavior cases (the two real-OS cases are exercised in
  the separate desktop profile), nine preview/TLS cases, editing/search,
  Markdown/JSON, managed bytes and backup/restore. Artifact:
  `build-container-gcc/packages/nebula-0.1.0~dev1-x86_64.deb`, SHA-256
  `bc9fdc65ff28a6cecbeda20ddd49d4b4f0e5a93edb9e58a955a64939d1c5b55c`.
  It stays ignored and is not a published release. Later work-log bookkeeping
  is not embedded retrospectively in that package.
- Formatter, Bash/POSIX-shell/Python syntax, prose links/fences across 17 Markdown
  files, 83 unique ordered milestone task IDs, 25 unique bug IDs, status/scope
  consistency and `git diff --check` pass.
- Limits: speech was verified from the real reader/dispatcher through a private
  null sink, without subjective audio-quality acceptance. The native editor is a
  text field; block semantics use F8. Other IMEs/compositors
  remain outside this Linux profile. The final Backspace guard was verified after
  that full OS profile with focused native regressions. Existing performance
  budgets remain recorded; no new performance claim is made. M09.4
  remains open; the audit waits for all preceding milestones.

### Implementation checkpoint verified on real runners — 2026-10-30

- Committed/pushed implementation `894ecb04975a019d9362e63269788495f304371e`
  with a subject-only message and configured authorship. Both jobs passed in
  [Linux/desktop run 41058351228](https://github.com/oppenheimmer/nebula/actions/runs/41058351228).
  GCC, Clang and ASan/UBSan each passed 15/15 CTest groups; core-only passed 2/2.
  Formatting, five coverage-guided parsers, installed-package checks and ten
  interaction-suite repetitions passed. Leak detection remains off.
- The new real-OS desktop job passed on that final source, including BUG-025:
  59 editor/62 desktop behavior cases, 20 focus repetitions and separately run
  real IBus/Anthy and Orca speech/selection/focus/persistence checks. This supplies
  real-runner evidence for the new optional image and scripts, beyond local
  acceptance. The reader test observed actual generated speech with its log
  flushed live.
- M09.4 remains open. The post-release audit remains Not started until all
  preceding milestones finish. This follow-up changed roadmap evidence only;
  application tests were not repeated for its prose. Local
  documentation/status/whitespace checks passed.

### Linux-only scope and space cleanup — 2026-10-30

- At the user's direction, Linux is now the only target platform (M00.1/M09).
  Removed the withdrawn follow-on release milestone and its requirements,
  dependency/platform notes, native VM workflow/helper and obsolete work-log
  material from the current tree. Historical commits remain intact. Third-party
  license names/notices remain accurate. README, project instructions and usage,
  dependency, attachment, interchange and feasibility documents agree.
- Renumbered the existing post-release audit to M10.1–M10.5, directly after M09,
  retaining the full audit, Google C++ style, simplification and verification
  requirements with Linux baselines. M09.4 remains open; M10 is Not started and
  still requires every preceding milestone to finish. This scope change does not
  mark withdrawn deliverables complete or begin the audit.
- Per the user's report, the unmerged packaging-preparation branch/worktree,
  guest disks and local guest-tool image had already been deleted. That work was
  never committed to main or pushed; no package was built or verified. Only one
  guest's base/build disk pair had been prepared. Final Git inventory confirms
  no retired-platform branch/worktree remains; the pre-existing detached Kilo
  checkout is preserved.
- Integrated the matching cleanup edits appearing during this session after the
  user's explicit instruction to continue and integrate them. Removed the unused
  three-origin transfer launcher command, its tests and bundle/transfer-only
  fixture code. Kept the package fixture's richer notes, properties, reference,
  trash and managed-file create/verify checks and all production interchange.
  A final matching CMake edit links that fixture only to storage, removing its
  unused direct interchange/Qt Core dependencies. The local package check below
  preceded this final target-link change; fresh CI must verify that final graph.
- **Verification before reclaiming builds:** Bash syntax and launcher regressions
  passed; container `format --check` passed. The full isolated Debian package
  check rebuilt the retained fixture with GCC 14.2/Qt 6.8.2 and passed install,
  reinstall/purge data preservation, launch/edit/search, managed bytes,
  Markdown/JSON export/import and fresh backup/restore. Full-window tests passed
  62 behavior cases (64 including setup/cleanup); the two actual OS input/reader
  cases are intentionally skipped by this package profile. Preview/TLS passed
  nine behavior cases (11 including setup/cleanup), using OpenSSL 3.5.7.
  Real OS input/reader acceptance was not repeated for this tooling removal.
- **Local space recovery:** the main checkout fell from 1,352,404,992 bytes
  (about 1.26 GiB) to about 9.1 MiB including Git history; source/docs outside Git
  occupy about 1.6 MiB. Removed ignored build output, sanitizer/fuzz/transfer
  fixtures, speech logs, preview packages and index caches. Also removed 12
  confirmed generated Nebula build folders in old temporary snapshots
  (248,311,808 bytes, about 237 MiB), preserving their sources and all workspaces,
  attachments and backups. No user note data or unrelated project cache was
  deleted.
- Removed every remaining project Docker image, including the retained Linux
  development image, and cleared the builder cache again after the package check
  recreated it. Final `docker system df`, image inventory and `buildx du` report
  zero images, containers, volumes and build-cache bytes. No host dependency
  installation or global environment change was made. Before the next Docker
  build/run, use `./scripts/dev.sh build-image`; optional OS acceptance additionally
  needs `./scripts/dev.sh build-desktop-image`.
- **Remote space recovery:** verified the retired workflows were idle, then
  deleted all seven repository VM/tooling caches (5,840,811,690 bytes, 5.44 GiB)
  and six synthetic transfer artifacts (56,297 bytes). Fresh GitHub API/CLI
  inventories report zero caches and zero artifacts. Historical workflow logs
  and Git commits were retained; other repositories were untouched.
- Documentation checks pass: 17 Markdown documents, valid local links/fences,
  Linux-only scope, 76 unique ordered task IDs, 25 preserved bug entries,
  34 gotchas, accurate release/audit dependencies and `git diff --check`.
  Scope scanning retains only third-party license identifiers for the withdrawn
  targets.

## Technical references discussed

- [Zim Desktop Wiki repository](https://github.com/zim-desktop-wiki/zim-desktop-wiki)
- [Zim page links and heading/object anchors](https://zim-wiki.org/manual/Help/Links.html)
- [Zim backlinks pane](https://zim-wiki.org/manual/Plugins/BackLinks_Pane.html)
- [Zim page navigation and creation](https://zim-wiki.org/manual/Help/Pages.html)
- [Zim incoming/outgoing link search](https://zim-wiki.org/manual/Help/Searching.html)
- [Qt rich-text processing](https://doc.qt.io/qt-6/richtext.html)
- [Qt rich-text document structure](https://doc.qt.io/qt-6/richtext-structure.html)
- [Qt model/view programming](https://doc.qt.io/qt-6/model-view-programming.html)
- [Qt interface guidance](https://doc.qt.io/qt-6/qt-intro.html)
- [Qt supported platforms](https://doc.qt.io/qt-6/supported-platforms.html)
- [Qt licensing](https://doc.qt.io/qt-6/licensing.html)
- [GTK 4 documentation](https://docs.gtk.org/gtk4/)
- [gtkmm](https://gtkmm.gnome.org/)
- [wxWidgets](https://github.com/wxWidgets/wxWidgets)
- [Appropriate uses for SQLite](https://sqlite.org/whentouse.html)
- [SQLite online backup API](https://sqlite.org/backup.html)
- [SQLite FTS5](https://sqlite.org/fts5.html)
- [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)

These references informed the discussion. Confirm exact supported dependency
versions and platform requirements when implementing the affected milestone.
