# Nebula roadmap

Last updated: 2026-10-29 (Asia/Tokyo)

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
| Organization | User-defined databases containing pages; saved views share the underlying pages. |
| Editor | Flowing document with movable, nestable blocks; not a spatial canvas. |
| Cross-referencing | Links to pages, headings, and blocks across databases in one workspace; backlinks and link-aware navigation/search. |
| Language | C++17 baseline with conservative dependencies; C libraries where useful. |
| Application license | MIT, selected by the user; dependencies retain their own licenses. |
| Interface | Qt 6 Widgets is the preferred starting point, subject to the editor feasibility milestone. |
| Storage | SQLite for structured records, with small versioned JSON payloads for flexible block content and view settings. |
| Attachments | Prefer managed files beside the database; evaluate SQLite BLOBs if a single-file workspace becomes a requirement. |
| Build | CMake; keep compiler and platform requirements explicit. |
| Development isolation | Existing Docker CLI with a project development image. Qt and build dependencies must not be installed system-wide on the host. |
| Modularity | UI-independent document core, application operations, storage interfaces, and separate desktop adapters. |
| Portability | Linux first; introduce macOS build checks early and deliver its polished package later. |
| Data ownership | Documented schema, migration paths, backup/restore, and portable exports. |

The following remain implementation decisions rather than settled facts:

- Whether the editor uses one rich-text document per page or separate block
  components, including how selection and undo cross block boundaries.
- Release dependency pinning and third-party distribution notices. The current
  module/API baseline is recorded in [DEPENDENCIES.md](docs/DEPENDENCIES.md).
- Minimum Linux distribution/compiler baseline and macOS deployment target.
- Linux packaging format and the eventual macOS distribution method.
- Benchmark thresholds and supported search languages/tokenization behavior.
- Exact rich-text payload schema and supported Markdown import/export subset.
- Exact wiki-link escaping rules and interchange syntax for duplicate page titles,
  custom labels, heading/block targets, and unresolved references.

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

The macOS release follows the Linux release, with build checks introduced earlier.
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
now exist. Production workspace storage and database views remain unimplemented.

Update the relevant milestone and work log in the same change as implementation.
Record actual defects in the bug log, anticipated problems in the risk register,
and learned constraints in the gotcha log. Preserve the history of resolved bugs
and changed decisions. Avoid unsupported completion percentages.

| ID | Milestone | Dependencies | Status |
| --- | --- | --- | --- |
| M00 | Project definition and working conventions | None | Complete |
| M01 | Editor feasibility and architecture decisions | M00 | In progress |
| M02 | Modular application foundation | M01; prototype build/core scaffolding started early | In progress |
| M03 | Persistent workspace and recovery foundations | M02 | Not started |
| M04 | Database-to-page workflow | M03 | Not started |
| M05 | Complete first-release block editing | M01, M04 | Not started |
| M06 | Attachments and diary workflow | M03, M05 | Not started |
| M07 | Search and portable interchange | M04, M06 | Not started |
| M08 | Reliability, accessibility, and performance | M04–M07 | Not started |
| M09 | Linux release | M08 | Not started |
| M10 | macOS release | Early checks in M02; release after M09 | Not started |

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
- [ ] M01.4 Check Unicode, input-method composition, keyboard access, and basic
  accessibility with real toolkit behavior.
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

Progress evidence: `nebula_prototype` contains both editor approaches, backed by
core and Qt interaction tests. [EDITOR_FEASIBILITY.md](docs/EDITOR_FEASIBILITY.md)
records the provisional page-wide editor decision, measured fixture/budgets,
regressions, and remaining validation. M01.4 has automated Unicode, synthetic
composition, keyboard/mouse, font fallback, and accessible-text checks; real OS
input-method and screen-reader sessions remain outstanding, so M01 is not complete.
The user selected MIT. Development now uses a Docker image rather than host Qt
packages, as required in the subsequent environment clarification.

### M02 — Modular application foundation

Deliverables: a buildable desktop shell, core/application/storage module boundaries,
and a repeatable developer workflow.

- [x] M02.1 Establish CMake targets and the C++17 baseline; document dependencies
  and supported Linux compiler configurations.
- [ ] M02.2 Implement the initial UI-independent document types and invariants,
  including permanent IDs, typed references, hierarchy validation, and deterministic
  ordering.
- [ ] M02.3 Define application operations, storage interfaces, and ownership of
  undo/redo without duplicating editing rules in individual views.
- [ ] M02.4 Add workspace navigation, database/page placeholders, and platform
  adapters for paths, dialogs, menus, and shortcuts.
- [x] M02.5 Establish meaningful automated core tests and documented build/check
  commands; exercise both GCC and Clang where available.
- [ ] M02.6 Introduce an early macOS compile/smoke check when a runner or machine
  is available; record any infrastructure limitation rather than claiming a pass.

Exit criteria: a fresh checkout builds and launches on the chosen Linux baseline;
core tests run without a GUI. Platform assumptions and the status of the early
macOS check are documented.

Dependency exception: the CMake targets, initial core values/tree operations, and
test harness were needed to investigate M01, so that scaffolding was started
before M01's manual desktop validation is complete. This does not complete the
production core, application command/undo architecture, workspace shell, or macOS
checks. [README.md](README.md) documents the Docker build/test and GUI commands.

### M03 — Persistent workspace and recovery foundations

Deliverables: versioned SQLite storage, workspace lifecycle, persistence of core
records, and an initial backup/restore path.

- [ ] M03.1 Define and document tables, relationships, typed property storage,
  rich-text/reference payload versions, derived link edges, and schema versioning.
- [ ] M03.2 Implement create/open/close operations, storage adapters, transactions,
  and validation of persisted document relationships.
- [ ] M03.3 Implement autosave with visible persistence state and actionable write
  errors; define commit/durability behavior and the pending-edit window.
- [ ] M03.4 Implement versioned migrations and safe behavior for unsupported newer
  workspace formats; verify preservation of existing data.
- [ ] M03.5 Implement trash and restore semantics for pages/blocks, including how
  links and attachment references behave.
- [ ] M03.6 Implement database snapshot backup and restore; reserve the manifest
  and attachment inclusion mechanism completed in M06.
- [ ] M03.7 Verify interrupted operations, reopening, rollback, migration, and
  backup/restore using representative persisted fixtures.
- [ ] M03.8 Persist resolved and unresolved inline references; maintain a rebuildable
  index of source/target page and block relationships. Verify consistency across
  commits, undo/redo, trash, and restoration without resolving targets by title.

Exit criteria: supported records survive closing/reopening; failed operations do
not leave partial logical edits; migrations and database backup/restore preserve
the tested data. Tests distinguish pending edits from confirmed saved data. Link
index rebuilds reproduce the references in persisted content.

### M04 — Database-to-page workflow

Deliverables: the first complete organization workflow, from a database table to
an editable page and back.

- [ ] M04.1 Create, rename, and remove logical databases and their pages using the
  established trash policy.
- [ ] M04.2 Add editable text, number, date, checkbox, and tags/select properties,
  with correct validation and type-aware ordering.
- [ ] M04.3 Build the table model/view interface, visible-column settings, filters,
  sorting, and persistence of saved views.
- [ ] M04.4 Open a row as its page, edit basic text through application operations,
  and keep page properties and database views consistent.
- [ ] M04.5 Verify that multiple saved views share data and retain their settings
  across restart; changing presentation must not duplicate content.

Exit criteria: create a Journal database, add properties and entries, edit a page,
filter/sort the table, save a view, restart, and recover the same data and view.

### M05 — Complete first-release block editing

Deliverables: the production block editor implementing the approach chosen in M01.

- [ ] M05.1 Support paragraphs, headings, lists, checkboxes, links, and dividers,
  including a practical way to insert blocks and change their types.
- [ ] M05.2 Implement Enter/Shift+Enter behavior, split/merge operations, inline
  formatting, and natural cursor movement across block boundaries.
- [ ] M05.3 Implement cross-block selection, copy/cut/paste, and defined handling
  of plain text and supported rich-text clipboard content.
- [ ] M05.4 Implement drag reordering, keyboard-accessible move operations, and
  basic nesting while preserving IDs and preventing invalid trees.
- [ ] M05.5 Integrate consistent undo/redo for text and structural edits with
  autosave; document undo boundaries and whether history survives restart.
- [ ] M05.6 Implement page/block link resolution using permanent IDs and predictable
  behavior when a target is in trash or unavailable. Preserve targets across page
  renames, database moves, heading edits, and block moves between pages.
- [ ] M05.7 Recheck Unicode, composition input, accessibility, and the long-document
  scenarios from M01 against the production editor.
- [ ] M05.8 Add wiki-style link entry, completion and a target picker for pages,
  headings, and blocks, with duplicate-title disambiguation and custom labels.
- [ ] M05.9 Add backlinks/outgoing-link navigation with source context and exact
  block destinations; provide keyboard access and back/forward position history.
- [ ] M05.10 Surface unresolved references and explicit create-or-select-target
  actions, including the destination database for a new page. Keep default and
  custom label behavior consistent through renames and undo/redo.

Exit criteria: the M01 interaction scenarios pass with persisted documents. Text
wrapping does not alter block structure; structural edits preserve identity and
can be undone coherently. A journal page can reference a page and heading in
another database, navigate to them and back, and expose the corresponding backlink.
Renaming or moving targets preserves references; ambiguous/unresolved references
require an explicit target choice. Missing first-release behavior is tracked
explicitly.

### M06 — Attachments and diary workflow

Deliverables: image/file blocks, reliable managed attachments, complete workspace
backup, and a useful daily journal workflow.

- [ ] M06.1 Implement managed file import, attachment metadata, image display, and
  file attachment blocks; removing the source file must not break imported data.
- [ ] M06.2 Handle interrupted imports, missing files, and orphan detection;
  cleanup must account for live documents, trash, and active backups.
- [ ] M06.3 Complete database-plus-attachment backup/restore with a consistent
  manifest and verification of referenced files.
- [ ] M06.4 Add a Journal template with Title, Entry date, Mood, and Tags, plus a
  convenient way to create an entry for a chosen day.
- [ ] M06.5 Keep date-only entry values distinct from timestamps and verify diary
  behavior around date/timezone changes.

Exit criteria: create a dated entry with an image and file, move/remove the original
source files, restart, and restore the workspace into a fresh location. The entry,
properties, blocks, and attachments remain usable.

### M07 — Search and portable interchange

Deliverables: searchable notes and documented export/import formats.

- [ ] M07.1 Index appropriate page text and properties using FTS5; keep the index
  consistent with edits and trash changes and provide a rebuild path.
- [ ] M07.2 Define and test search/tokenization behavior for supported languages;
  search results should navigate to the relevant page or block.
- [ ] M07.3 Implement a documented Markdown import/export subset with explicit
  handling of unsupported formatting, attachment references, internal link paths/
  anchors, and targets outside the exported set.
- [ ] M07.4 Implement versioned JSON export/import that preserves the supported
  model, including IDs, properties, blocks, views, internal reference targets/
  labels/unresolved states, and attachment references/files.
- [ ] M07.5 Define import validation and identity-collision behavior; malformed or
  partially supported input must not silently overwrite existing data. Remap
  internal references consistently if imported identities must change.
- [ ] M07.6 Verify semantic round trips through the lossless format and test the
  documented Markdown limitations with representative fixtures.
- [ ] M07.7 Add incoming/outgoing link filters to search and saved database views,
  including references to blocks on a selected page; verify structured link queries
  independently of full-text matches.

Exit criteria: edits and deletions appear correctly in search; rebuilding produces
equivalent results. A full export imported into a fresh workspace preserves the
supported model, reference relationships, and attachments, with equivalent backlinks
after rebuilding derived indexes. Markdown limitations are visible and documented.

### M08 — Reliability, accessibility, and performance

Deliverables: verified recovery behavior, interaction checks, benchmark results,
and a release candidate with known limitations documented.

- [ ] M08.1 Exercise crash/interruption scenarios, failed writes, migration errors,
  trash restoration, and backup restoration across the complete workflow.
- [ ] M08.2 Recheck keyboard navigation, focus, screen-reader semantics, clipboard,
  Unicode, and input-method composition in the integrated application.
- [ ] M08.3 Benchmark long pages and large databases against the thresholds agreed
  in M01; investigate bottlenecks using measured evidence.
- [ ] M08.4 Verify storage invariants and regression coverage for actual defects;
  check that attachments and derived indexes remain consistent.
- [ ] M08.5 Document the workspace format, recovery procedure, export limitations,
  supported environments, and unresolved release issues.
- [ ] M08.6 Verify references/backlinks through duplicate titles, renames, moves,
  heading changes, target trash/restore, reuse of deleted titles, undo/redo, backup
  restore, import identity remapping, and derived-index rebuilding.

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

- [ ] M10.1 Resolve issues found by early macOS checks and choose the supported OS
  and architecture baseline.
- [ ] M10.2 Adapt menus, Command-key shortcuts, file dialogs, application paths,
  and bundle resources without moving document rules into platform code.
- [ ] M10.3 Verify text/input-method behavior, accessibility, attachments, and the
  complete database/page workflow on macOS.
- [ ] M10.4 Verify backup/export transfer between Linux and macOS while preserving
  data; this is manual portability, not synchronization.
- [ ] M10.5 Package the application, document signing/distribution requirements for
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
| R06 | Linux assumptions may make the macOS port expensive. | Keep platform adapters narrow and start compile/smoke checks in M02. | Open |
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

## Work log

| Date (Asia/Tokyo) | Milestone | Change and evidence | Follow-up |
| --- | --- | --- | --- |
| 2026-10-29 | M00 | Recorded the accepted discussion in this roadmap and added project management instructions. Repository inspection found no application files or commits. Document review and structural checks passed: 11 milestone definitions/table entries, 61 unique task IDs, matching exit criteria, accurate completion states, balanced code fences, and clean whitespace. Application tests are not applicable to this documentation-only change. | Begin M01 when implementation is requested; record prototype evidence before finalizing the editor design. |
| 2026-10-29 | M00, M01–M03, M05, M07–M08 | Reviewed Zim's repository and official linking, backlinks, page-navigation, and search documentation. Added cross-referencing scope, data/index responsibilities, lifecycle rules, tasks, acceptance criteria, and management guidance. Document review, whitespace checks, and structural validation passed: 11 milestones, 67 unique tasks, and all 61 earlier task IDs/statuses preserved. Application tests are not applicable; implementation remains unstarted. | Begin M01 when implementation is requested, including validation of typed inline references in the editor prototype. |
| 2026-10-29 | M01, M02 | Began execution with a C++17/Qt prototype, a Qt-independent document/tree module, 10 core and 27 interaction/comparison cases plus a launch check, two editor approaches, and measurement commands. The user selected MIT and required Docker-based dependency isolation; added the development image/wrapper and management guidance. Fixed BUG-001 through BUG-003 with regression checks. Detailed compiler, native/offscreen, and measurement evidence is in the feasibility report. | Finish M01.4 with real OS IME and screen-reader validation, then continue the production module/command foundation in M02. Storage and database milestones remain not started. |

Future entries should record the steps changed, checks actually performed and
their outcomes, relevant commits, encountered bugs, and the next concrete action.
Do not record tests as passed when they were unavailable or not run.

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
