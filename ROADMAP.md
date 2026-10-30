# Nebula roadmap

Last updated: 2026-10-30 (Asia/Tokyo)

## Project goal

Build a portable desktop note-taking and diary application inspired by Notion's
block editing and database organization and Zim Desktop Wiki's interconnected
notes. The primary interface is a database view: each entry is a page with editable
properties and a flowing document of movable blocks. Users should be able to write
naturally, organize entries through typed properties, cross-reference documents,
and keep their data usable for the long term.

The application is **always local and single-user**. Linux is the first release
target; macOS is a planned subsequent target. The architecture must be modular,
with a documented storage format and conservative C/C++ dependencies.

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
| Portability | Linux first; compile/smoke and packaging verification on macOS are required in M10. |
| Data ownership | Documented schema, migration paths, backup/restore, and portable exports. |

The following remain implementation decisions rather than settled facts:

- Remaining OS input-method/accessibility validation of the selected page-wide
  rich-text adapter, including whether those checks require architectural changes.
- Release dependency pinning and third-party distribution notices. The current
  module/API baseline is recorded in [DEPENDENCIES.md](docs/DEPENDENCIES.md).
- Minimum Linux distribution/compiler baseline and macOS deployment target.
- Linux packaging format and the eventual macOS distribution method.
- Benchmark thresholds; supported search behavior is defined in
  [SEARCH.md](docs/SEARCH.md).
- Cross-platform verification of the defined lossless interchange and Markdown
  subset; contracts are in [INTERCHANGE.md](docs/INTERCHANGE.md),
  [MARKDOWN.md](docs/MARKDOWN.md) and
  [WORKSPACE_FORMAT.md](docs/WORKSPACE_FORMAT.md).

### Options considered

| Option | Reason to consider it | Tradeoff and disposition |
| --- | --- | --- |
| C++ with Qt Widgets | Rich-text facilities, desktop controls, and model/view tables. | Preferred; custom block interactions still need a prototype. |
| C++ with Qt Quick/QML | Flexible layouts and animated interfaces. | Adds QML and another interface model; a fallback if the editor investigation warrants it. |
| C with GTK 4, or C++ with gtkmm | Strong Linux foundation; suitable if C or GNOME integration becomes the priority. | macOS integration and packaging need separate investigation. |
| C++ with wxWidgets | Established cross-platform native desktop controls. | Expected to require more custom work for this editor; keep as an alternative. |
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

The original version 1 bootstrap format was deliberately small: `nebula.sqlite3`, SQLite
`application_id = 0x4e454255` (`NEBU`), `user_version = 1`, and
`workspace_meta(id INTEGER PRIMARY KEY CHECK(id=1), workspace_id TEXT NOT NULL
UNIQUE)`. Its single row contains a generated 32-character hexadecimal identity.
Initialization occurs in a transaction; reopening preserves that identity.
Unrelated databases and unsupported versions are rejected without modifying their
contents. **Version 1 stores no pages or notes.** The current version 7 format
adds persistent databases, pages, block payloads, properties, views, derived link
edges, and deleted-block recovery. [WORKSPACE_FORMAT.md](docs/WORKSPACE_FORMAT.md) documents the
schema, JSON versions, ownership, autosave/undo boundaries, and backup contract.
The prototype notice applies only to explicit in-memory demo mode.

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

The macOS release follows the Linux release and begins with build and smoke checks.
Calendar and other database presentations are possible later extensions. Advanced
relations, formulas, and broader Notion feature parity are not first-release
commitments. A spatial canvas, synchronization, shared workspaces, and a hosted
service are outside the agreed product scope.

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
interchange are implemented. Pasted-link previews and release validation remain
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
| M07 | Search and portable interchange | M04, M06 | In progress |
| M08 | Reliability, accessibility, and performance | M04–M07 | In progress |
| M09 | Linux release | M08 | Not started |
| M10 | macOS release | M09 | Not started |

### M00 — Project definition and working conventions

Deliverables: this roadmap and repository management instructions in `AGENTS.md`.

- [x] M00.1 Record the project goal, local-only scope, and Linux/macOS direction.
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
- [ ] M07.8 When pasting a web link, try to extract a text-only preview that the
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

- [ ] M08.1 Exercise crash/interruption scenarios, failed writes, migration errors,
  trash restoration, and backup restoration across the complete workflow.
- [ ] M08.2 Recheck keyboard navigation, focus, screen-reader semantics, clipboard,
  Unicode, and input-method composition in the integrated application. Use an
  actual OS IME to check candidate selection, composition updates, commit/cancel,
  and Enter/cursor/selection/undo at block boundaries. Check screen-reader
  navigation/selection for headings, lists, to-do
  state, references, and asset labels. Record IME, reader, desktop, Qt, and results.
- [ ] M08.3 Benchmark long pages and large databases against the thresholds agreed
  in M01; investigate bottlenecks using measured evidence.
- [ ] M08.4 Verify storage invariants and regression coverage for actual defects;
  check that attachments and derived indexes remain consistent.
- [ ] M08.5 Document the workspace format, recovery procedure, export limitations,
  supported environments, and unresolved release issues.
- [ ] M08.6 Verify references/backlinks through duplicate titles, renames, moves,
  heading changes, target trash/restore, reuse of deleted titles, undo/redo, backup
  restore, import identity remapping, and derived-index rebuilding.
- [x] M08.7 Clarify recoverable trash at block/subtree, page and whole-database
  scope. Give each scope its own small red trash-icon button with an accessible
  name and clear confirmation; require an unchecked acknowledgement checkbox
  before confirming database trash. Default to Cancel, preserve pending edits on
  failure, retain attachments, and verify cancel/Escape, restore, block undo/redo
  and restart behavior. These buttons must never invoke permanent purge. Rename
  the visible reference lists to “Referenced by:” and “References”.

Exit criteria: required checks have recorded outcomes; supported saved data
survives the documented recovery scenarios; no unresolved release-blocking defect
remains. Performance meets the agreed thresholds on recorded hardware/fixtures.

### M09 — Linux release

Deliverables: an installable Linux package, release notes, and user/developer
instructions matching the shipped behavior.

- [ ] M09.1 Select and document a packaging approach, minimum supported environment,
  dependency versions, and required license notices.
- [ ] M09.2 Validate installation, launch, workspace creation, editing, search,
  export, backup, and restore in a clean supported environment without network.
- [ ] M09.3 Verify upgrade behavior and provide build, installation, data-location,
  backup, and recovery instructions.
- [ ] M09.4 Record release verification, known limitations, and the release version.

Exit criteria: a reproducible package installs and runs the agreed offline workflow
on the documented Linux target. Release documentation and the roadmap reflect the
actual delivered features and remaining issues.

### M10 — macOS release

Deliverables: a macOS application bundle using the same core and workspace format,
with platform integration and distribution instructions.

- [ ] M10.1 Compile and smoke-test the core, storage, and desktop targets on
  a real macOS runner or machine, using a project/runner-scoped SDK. Run applicable
  tests and record OS/architecture, compiler, Qt, and actual outcomes.
- [ ] M10.2 Resolve issues found by M10.1 and choose the supported OS
  and architecture baseline.
- [ ] M10.3 Adapt menus, Command-key shortcuts, file dialogs, application paths,
  and bundle resources without moving document rules into platform code.
- [ ] M10.4 Verify text/input-method behavior, accessibility, attachments, and the
  complete database/page workflow on macOS.
- [ ] M10.5 Verify backup/export transfer between Linux and macOS while preserving
  data; this is manual portability, not synchronization.
- [ ] M10.6 Package the application, document signing/distribution requirements for
  the selected channel, and record release checks and limitations.

Exit criteria: the macOS package completes the agreed offline workflow and passes
cross-platform data-transfer checks. Its supported environments and distribution
procedure are documented.

## Risk register

These are anticipated risks, not encountered bugs.

| ID | Risk | Resolution or investigation | Status |
| --- | --- | --- | --- |
| R01 | Native editor composition may make selection, focus, or undo inconsistent across blocks. | Compare approaches and validate interactions in M01 before committing to the production editor. | Open |
| R02 | Toolkit representation may lose application block identity or formatting. | Own the persistent schema and verify model/editor and export round trips. | Open |
| R03 | Attachment files and database references may diverge after interruption or backup. | Define import ordering, reconciliation, retention, and a consistent backup manifest. | Open |
| R04 | Search tokenization may be unsuitable for some writing systems. | Define supported behavior and test representative language fixtures in M07. | Open |
| R05 | Large documents or databases may expose rendering/query bottlenecks. | Establish workloads and thresholds in M01; measure again in M08. | Open |
| R06 | Linux assumptions may make the macOS port expensive. | Keep platform adapters narrow and begin the macOS milestone with compile/smoke checks. | Open |
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

- **Milestone/state:** M07/M08; Investigating. One native suite run reported
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
  Stress verification of the intermittent failure remains pending.

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
  BUG-013 removes an object while keeping its block ID, so no deleted-block
  fragment exists. Retain registered imports independently of current reference
  counts; clean only unregistered leftovers until a deliberate purge policy can
  account for undo, recovery and retained backups.

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
session remains unconfirmed. Real OS IME, screen-reader, and macOS checks remain
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
  and Linux/macOS packaging remain unfinished. Do not infer completion from this
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

At the user's direction, actual OS IME/screen-reader acceptance belongs to M08.2
and macOS compile/smoke checks begin M10. Tasks were moved into their destination
lists and renumbered in execution order, with no transfer notices left behind.
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
  Real OS IME, screen-reader, and macOS checks have not been performed.

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
  but managed attachments are unfinished. M07–M10 are Not started.
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
  remain incomplete. Real OS IME/screen-reader and macOS checks are unperformed;
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
  checks passed. Real OS IME, screen-reader, and macOS checks remain unperformed;
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
  checks passed. Actual OS IME, reader, and macOS acceptance remain unperformed.
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
  checks passed. OS IME/reader and macOS acceptance remain unperformed.
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
  remain M08 work, and macOS checks remain M10; none is claimed here.
- **Next:** execute M06.1–M06.3 managed attachment import/ownership, interruption
  and cleanup rules, and database-plus-file manifest backup/restore. M06 remains
  In progress; M07–M10 remain Not started. Current schema/payload contracts are in
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
  Linux/macOS file APIs. Image cards preview managed bytes through existing Qt Gui
  readers with bounded decode/cache sizes. File IDs survive clipboard, movement,
  deletion/undo and reopen. Local open/copy and availability/audit controls are
  keyboard-accessible. The Docker launcher adds only an explicitly selected
  read-only `/imports` mount through `--import-root`; source folders stay unchanged.
- Schema 7 adds the attachment registry; block payload 3 carries block/inline
  managed IDs while reading payloads 1–2. Versions 1–6 migrate with preserved notes,
  legacy label-only cards and recovery fragments. No new external library was
  added. macOS compilation/filesystem behavior remains M10 work.
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
  desktop-handler opening, actual OS IME/reader and macOS checks remain unperformed.
  Imports/audits are synchronous; large-file responsiveness is not claimed.
- **Next:** M07.1–M07.2 indexed search/tokenization and navigation, M07.3–M07.6
  Markdown/lossless interchange and validation, and M07.7 reference filters in
  search/views. M00–M06 are Complete; M07–M10 remain Not started at this checkpoint.

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
  offscreen and native checks then passed. Actual OS IME/reader and macOS checks
  remain unperformed. Build environment is unchanged from M06. All 69 unique
  task IDs, local documentation links/fences and `git diff --check` passed.
- **Next:** M07.3 Markdown subset, M07.4–M07.6 lossless portable interchange and
  validation, then M07.7 reference filters. M08–M10 remain Not started.

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
  then M07.7 reference filters. M07 remains In progress; M08–M10 remain Not started.
  The Linux environment is unchanged; OS IME/reader and macOS remain unverified.

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
  ordinary C++17 references. OS IME/reader, crash-kill boundaries and actual macOS
  transfer remain separately required. GitHub access is available for a real
  macOS runner when its milestone is reached; no macOS verification is claimed.
  All 69 task IDs, prose links/fences, Bash syntax and `git diff --check` passed.
- **Next:** M07.7 structured incoming/outgoing reference filters in search and
  saved views. M07 remains In progress; M08–M10 remain Not started.

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
  code/dependency was introduced in this checkpoint. M08–M10 remain Not started.
  Resume with M07.8, then reliability/accessibility/performance and release work;
  actual OS IME, screen-reader and macOS verification remain unperformed.

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
  fences and `git diff --check` passed. Actual OS IME, screen-reader and macOS
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
  Linux/macOS TLS deployment implications are in
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
  agent as requested; OS IME, screen-reader and macOS checks remain open.

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

These references informed the discussion. Confirm exact supported dependency
versions and platform requirements when implementing the affected milestone.
