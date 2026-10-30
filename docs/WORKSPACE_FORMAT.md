# Workspace format, version 8

A workspace contains `nebula.sqlite3`, `attachments/`, and `.nebula.lock`. The database has SQLite
application ID `0x4e454255` and `user_version = 8`. The application refuses unknown
versions. Version 1 contained only workspace identity; upgrading to version 2
adds the persistent record tables in one transaction and preserves that identity.
Version 3 adds deleted-block recovery. Version 4 records deletion operation IDs and
original positions to preserve sibling order when fragments are restored in a
different order. Version 5 adds divider blocks, rebuilding block/link tables in
one transaction while preserving their records. Version 6 enables explicit
default/custom reference labels with block payload version 2. Its marker upgrade
preserves version 1 payloads and deleted fragments; legacy labels remain custom.
Version 7 adds managed attachment metadata and block payload version 3; older
representation-only cards retain empty attachment IDs. Version 8 adds rebuildable
FTS5 page/block/property search. Versions 1–7 migrate
transactionally; existing version 3 fragments retain their content and original
parent/next-sibling anchors.
Failed migrations roll back. Managed attachment paths and database files cannot be
symlinks.

| Table | Canonical responsibility |
| --- | --- |
| `workspace_meta` | Single row with the permanent workspace identity. |
| `databases` | Permanent ID, name, and trash flag. |
| `pages` | Permanent ID, owning database, title, optimistic revision, trash flag, and UTC creation/modification timestamps. |
| `blocks` | Globally unique permanent ID, owning page, optional parent, preorder position, kind, and versioned JSON payload. |
| `property_definitions` | Permanent ID, owning database, name unique within that database, and declared type. |
| `property_values` | Page/property pair and typed JSON value. |
| `saved_views` | Permanent ID, database, name, and versioned presentation/query settings. |
| `links` | Rebuildable source page/block/run to target page/block edges, indexed by target identity. |
| `deleted_blocks` | Deleted subtree fragments, original parent/next sibling, deletion operation ID, original position, page, timestamp, and restored state; saved atomically with page edits. |
| `attachments` | Permanent file ID, original basename, byte count, SHA-256, and image/file presentation flag. Bytes live in the managed directory, not SQLite BLOBs. |
| `page_search` | Derived FTS5 rows for active/trashed pages; canonical joins exclude trash. Shadow tables are implementation details. See [SEARCH.md](SEARCH.md). |

Block kinds are paragraph (0), heading (1), bullet (2), to-do (3), image card (4),
attachment card (5), and divider (6). Dividers have no text runs. Positions are
unique within a page. The block vector is
tree preorder; roots have a null parent, and descendants are contiguous. A deferred
composite foreign key requires parents to belong to the same page. The core
additionally validates active ancestry, cycles, IDs, and paragraph boundaries.
Pages always have at least one block. Image/file cards with an attachment ID own
a reference to managed metadata/bytes. Legacy cards with empty IDs retain their
label-only representation.

Block payload JSON version 3 contains `version`, `runs`, `asset_label`,
`attachment_id`, and `checked`. Every run has UTF-8 `text`, `bold`, and `italic`. An optional `reference`
contains `kind` (page 0 or block 1) and either permanent `target`, or an
`unresolved_name` with optional database `scope`. Exactly one target form is valid.
Versions 2–3 references also require a boolean `custom_label`. True preserves explicit
label text; false derives a current label by stable ID and uses stored text as an
unavailable-target fallback. Readers accept version 1 payloads with all labels
treated as custom. Absent name/scope fields in older version 1 payloads mean empty strings. Unresolved
names are never bound by title during load, rendering, indexing, or navigation.
An optional `asset`
contains `image`, `label`, and (in version 3) `attachment_id`. Version 1–2 blocks
and inline assets have empty attachment IDs. Soft breaks use U+2028; hard paragraph boundaries are
separate blocks. QTextDocument/HTML serialization is never authoritative. Unknown
payload versions and malformed or mistyped fields fail explicitly.

Default labels refresh when a page opens and during editing. Current-page heading
and title changes update their default labels in the same native undo command.
The editor holds a temporary copy of the page title in its first block's Qt
format solely to undo title/label edits together; it is excluded from canonical
block JSON. The `pages.title` column remains authoritative. Editing default link
text makes that label custom; undo restores both text and mode. Default block
titles substitute `[link]` for nested default references, bounding recursive labels.
Incoming/outgoing excerpts also derive current labels by ID without changing targets.

Following an unresolved reference offers explicit existing-target selection or
new-page creation with a destination database. Creating a page and binding its
source reference uses one transaction, including revisions and derived edges;
failed or stale source writes leave no new page. Undo of the binding restores the
unresolved reference while retaining the explicitly created page. Creating another
page with the intended name alone never binds an unresolved reference.

Property JSON version 1 uses `version`, `type`, and (except unset) `value`:

| Type | Value |
| --- | --- |
| 0 | Unset, distinct from false, zero, or an empty string. |
| 1 | UTF-8 text. |
| 2 | Finite double precision number. |
| 3 | Gregorian `YYYY-MM-DD` date, independent of timestamps and timezone conversion. |
| 4 | Boolean. |
| 5 | Array of nonempty, unique UTF-8 tag strings. |

The declared property kind is text (0), number (1), date (2), checkbox (3), or
tags (4). Application validation enforces matching values and database ownership.
Numbers sort numerically, dates lexically in ISO order, and tags lexicographically.
Unset values sort before populated values in ascending order. Ties use page ID.
Text comparison is case-sensitive; locale-aware collation is not implied.

View JSON version 1 stores `visible` property IDs, AND-combined `filters`, a `sort`
property ID (empty means title), and `ascending`. A filter stores `property`,
`comparison` (equal 0, contains 1, less 2, greater 3, empty 4, not-empty 5), and a
typed property value. Contains means a case-sensitive substring for text or an
exact tag match. Empty means unset. Views never copy pages. The desktop tags field
uses comma-separated entry; labels containing commas need a future entry control.

Saving a page uses one `BEGIN IMMEDIATE` transaction for metadata, the complete
block tree, property values, and derived link edges. The revision must match the
loaded revision. A stale writer fails instead of overwriting another window's
changes, and a failed transaction leaves the pending editor/session values intact.
Foreign keys, uniqueness checks, core validation, and payload decoding supplement
SQLite's integrity check.

Moving a page between databases preserves its page/block IDs and block payloads.
One transaction remaps its stored property values to destination definitions with
the same name and type, creates missing definitions, updates membership/revision,
and commits. A same-name/different-type conflict rolls everything back, including
new definitions. Unset values and date-only values retain their types. Definitions
with no stored value on this page are not copied. Existing source/destination
views keep their settings; they query the page's new membership. Moving back uses
matching source definitions again. Newly created definitions are retained.

Cross-page block transfer commits both trees, both revisions, and their derived
outgoing edges in one transaction. The transferred root becomes a root at the end
of the destination; descendants, order, rich content, and all target IDs remain
unchanged. An emptied source gets a fresh empty paragraph. Transferred identities
are not deleted-block fragments. Incoming block references resolve through the
block's current owning page; page references continue to identify their original
page. Neither operation rewrites references by title or changes their display text.
Resolution distinguishes available targets, trashed pages/databases, unavailable
targets, and unresolved intended names without scanning every document.

The desktop flushes pending edits before workspace moves and clears the open
page's editing undo after a successful move. The latest block transfer has a
separate in-memory Workspace undo/redo operation. Reversing it atomically restores
both trees and updates their revisions/indexes, including removing/reinstating the
synthetic empty-source paragraph. It requires both pages to be active and both
revisions to match; subsequent changes, including saves from another window,
cause refusal instead of overwriting them. Navigation alone does not invalidate
this operation. A new transfer replaces it; it does not survive window close.
Undoing while viewing another page preserves that page's text undo history.

The desktop saves after 500 ms without a new edit, and flushes before page/database
navigation or a normal window close. Pending changes are visible. Saved locally
means the SQLite commit succeeded with `synchronous=FULL`; edits during the debounce interval are pending
and may be lost if the process is killed. Save failures retain the document and
block silent closing. Undo belongs to the page editor, survives autosave, and is
cleared on page navigation; it does not survive restart. Adjacent printable typing
joins one undo group while the caret is continuous and pauses remain under one
second. Navigation, focus changes, formatting, structural commands, clipboard
operations, and composition events end that group. Autosave does not end it.

Database/page trash preserves IDs and stored content. Links retain permanent
targets, including unavailable targets. Backlinks exclude trashed source pages
and databases and aggregate links to blocks on a target page. The derived index
can be rebuilt from canonical runs. Deleting saved block identities archives their
typed content and hierarchy in the same transaction. Restore inserts only missing
identities, preserving already restored/edited blocks. Fragments from one deletion
retain their relative sibling order even when restored in reverse order. The page, database, and an
external original parent must be restored first. Undo followed by save marks fully
returned fragments restored. Recovery persists across restart and backup; this is
deleted-block recovery, not version history for every text change inside a block.

`nebula_workspace backup SOURCE NEW_DESTINATION` uses SQLite's online backup API
with a read-only source connection and a shared attachment lock, copies immutable
attachment bytes, verifies registered hashes/availability, and writes a versioned
database/file manifest. Existing/nested destinations, symlinks, special files and
missing/changed managed bytes are rejected; failed output is removed. Restore
verifies the complete manifest before creating a fresh copy, then validates and
migrates that copy without modifying the backup. Legacy backups without a manifest
remain accepted only for pre-7 schemas. No overwrite-based restore exists.
The precise file format, import ordering, retained-file cleanup policy and
manifest rules are in [ATTACHMENTS.md](ATTACHMENTS.md).
