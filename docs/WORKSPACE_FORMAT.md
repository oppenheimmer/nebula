# Workspace format, version 4

A workspace contains `nebula.sqlite3` and `attachments/`. The database has SQLite
application ID `0x4e454255` and `user_version = 4`. The application refuses unknown
versions. Version 1 contained only workspace identity; upgrading to version 2
adds the persistent record tables in one transaction and preserves that identity.
Version 3 adds deleted-block recovery. Version 4 records deletion operation IDs and
original positions to preserve sibling order when fragments are restored in a
different order. Versions 1–3 migrate transactionally; existing version 3 fragments
retain their content and original parent/next-sibling anchors.
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

Block kinds are paragraph (0), heading (1), bullet (2), to-do (3), image card (4),
and attachment card (5). Positions are unique within a page. The block vector is
tree preorder; roots have a null parent, and descendants are contiguous. A deferred
composite foreign key requires parents to belong to the same page. The core
additionally validates active ancestry, cycles, IDs, and paragraph boundaries.
Pages always have at least one block. Image/file cards currently contain labels
and representations; they do not establish managed attachment ownership.

Block payload JSON version 1 contains `version`, `runs`, `asset_label`, and
`checked`. Every run has UTF-8 `text`, `bold`, and `italic`. An optional `reference`
contains `kind` (page 0 or block 1) and either permanent `target`, or an
`unresolved_name` with optional database `scope`. Exactly one target form is valid.
Absent name/scope fields in older version 1 payloads mean empty strings. Unresolved
names are never bound by title during load, rendering, indexing, or navigation.
An optional `asset`
contains `image` and `label`. Soft breaks use U+2028; hard paragraph boundaries are
separate blocks. QTextDocument/HTML serialization is never authoritative. Unknown
payload versions and malformed or mistyped fields fail explicitly.

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

The desktop saves after 500 ms without a new edit, and flushes before page/database
navigation or a normal window close. Pending changes are visible. Saved locally
means the SQLite commit succeeded with `synchronous=FULL`; edits during the debounce interval are pending
and may be lost if the process is killed. Save failures retain the document and
block silent closing. Undo belongs to the page editor, survives autosave, and is
cleared on page navigation; it does not survive restart.

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
with a read-only source connection, then copies the attachment directory. It
rejects existing destinations, destinations within the source, symlinks, and
special attachment files; failed output is removed. `restore` makes a fresh copy
and validates/migrates that copy without modifying the backup. No overwrite-based
restore exists. A manifest, managed-asset consistency, and retention/reconciliation
rules remain necessary before claiming complete attachment recovery. Do not modify
attachment files externally during backup.
