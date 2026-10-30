# Lossless JSON interchange, version 1

Import / Export → Export lossless JSON folder… writes a new export folder.
Import lossless JSON into new workspace… asks for that source folder and a new
destination folder, validates the export, then opens the imported workspace in a
separate window. The current workspace keeps its own notes. Pending edits save
before either operation; a failed save keeps them visible.

Headless Docker equivalents:

```sh
./scripts/dev.sh export-json /path/to/workspace /path/to/new-export
./scripts/dev.sh import-json /path/to/export /path/to/new-workspace
```

The wrapper mounts the source read-only and only a new private staging folder
writable, then renames the finished export or workspace into place. It refuses
uncommitted source changes unless `NEBULA_ALLOW_DIRTY=1` is set. No network is used.
Open older workspaces normally to migrate them before exporting. A new subfolder
within the workspace is writable in the GUI container; a separately imported
workspace remains independent and needs its own backup. The original workspace's
backups do not include export folders or nested workspaces.

## Preservation and collisions

This format preserves the supported document/database model: workspace, database,
page, block, property, view, recovery-group and attachment IDs; full block trees
and order; rich runs, card types and checked state; typed property values including
unset/false/zero; saved view settings; timestamps/revisions; page/database trash;
deleted fragments including already-restored recovery records; and managed files.
References preserve kind, stable target IDs, custom/default label mode, rich
display text, unresolved names and database scope. Missing page/block targets
remain unavailable or unresolved. They are never redirected by title.

Import requires a fresh destination. Any existing folder/file is refused, even if
empty. The imported workspace keeps the export's `workspace_id`, which names a
workspace lineage rather than one copy. No merge, replacement, title matching or
overwrite mode exists, so imported IDs remain unchanged. Markdown import supplies the
separate operation for adding new pages with remapped identities to an existing
database.

All registered attachment files are retained, including those needed by undo or
recovery and those with no current object reference. Regular legacy/unregistered
attachment files are also included. File bytes, paths and registry hashes/sizes
are verified. Missing/corrupt required bytes fail export/import visibly; they do
not become a successful partial export. File ownership, permissions, timestamps
and empty directories are filesystem details outside the document model.

Derived links and full-text indexes are excluded. Import rebuilds both from
canonical records. An export can therefore recover from missing/stale derived
rows without treating those rows as document content. The SQLite backup format
is a separate recovery option; JSON does not contain a database binary.

## Envelope and canonical records

The folder contains UTF-8 workspace.json and attachments/. The root has exactly:

| Field | Representation |
| --- | --- |
| format | String `nebula-workspace`. |
| version | JSON number 1. |
| schema | JSON number 9, identifying the current canonical record contract. Earlier schema 8 exports are also accepted. Workspace schema 10 adds only derived structures, so it still exports contract 9. |
| tables | Object with exactly the nine arrays below. |
| files | Array of `path`, `size`, `sha256` objects. |

Each table row is an object with exactly the listed column names. Strings remain
Unicode strings; JSON payload columns become structured objects rather than
encoded strings. Flags are JSON booleans. **All SQL integer columns**, including
revisions, positions and kind enums, use canonical decimal strings to preserve
the complete signed 64-bit range in other JSON readers. Property numbers remain
the model's finite double precision JSON numbers. SQL null parent IDs become
JSON null. Array order preserves record insertion order, including the current
property-definition presentation order; block position columns remain authoritative.

| Array | Columns |
| --- | --- |
| workspace_meta | id, workspace_id |
| databases | id, name, trashed |
| pages | id, database_id, title, revision, trashed, created_at, modified_at |
| blocks | id, page_id, parent_id, position, kind, payload |
| property_definitions | id, database_id, name, kind |
| property_values | page_id, property_id, value_json |
| saved_views | id, database_id, name, settings |
| deleted_blocks | id, page_id, parent_id, anchor_id, fragment, restored, removed_at, operation_id, original_position |
| attachments | id, name, size, sha256, image |

Block, property, view and fragment payload versions follow
[WORKSPACE_FORMAT.md](WORKSPACE_FORMAT.md). Supported older block payloads retain
their version/fields semantically; JSON whitespace/key order is not significant.
Unknown payload fields/versions are refused instead of discarded.
Schema 8 exports accept view payload 1 only. Schema 9 supports view payloads 1–2,
including structured reference filters. Importing schema 8 creates a current
workspace without rewriting legacy view settings or changing model identities.
Dates remain date-only properties. UTC creation/modification/removal timestamps
remain distinct strings, with no timezone conversion during interchange.

Each file path is a canonical relative path below attachments/, using forward
slashes. Size is a nonnegative decimal int64 string and sha256 is a lowercase
SHA-256 hex string. No absolute, dot/traversal, backslash, duplicate or symlink
path is accepted. The manifest file set must exactly match the actual regular
files. File IDs and display names are separate; original source paths are absent.

## Validation and failure behavior

Unsupported format/schema versions, duplicate JSON keys (including escaped equivalent
keys), unknown/missing fields, wrong types, out-of-range integers, invalid UTF-8,
duplicate identities, identities outside the documented grammar (1–128 bytes of UTF-8
without control characters), page revisions of 2^62 or more, foreign-key failures,
malformed trees/order, mistyped/misowned properties, invalid view settings, malformed
recovery fragments and invalid UTC timestamps are rejected. Identity fields cannot be
empty or contain NUL; root/absent recovery anchors retain their defined empty/null
forms. Reference target absence is allowed and preserved. Text NUL characters are
preserved in supported string fields. The manifest is limited to 64 MiB; a larger
export fails explicitly and removes its partial destination.

Export copies a consistent database snapshot in bounded SQLite backup steps, then
validates and serializes that private copy while retaining a shared managed-file
lock. It does not hold a live read transaction through serialization/file writes.
The temporary full workspace database is private, sits in the new export folder, and is removed
before success. An interrupted export may leave that incomplete folder; remove it
and retry rather than opening it as a workspace. Import
creates a new workspace, validates and writes canonical records in a transaction,
copies/checks files, then rebuilds indexes. File/directory flushes precede a
successful result. A reported failure removes the newly created partial folder
without overwriting existing data. An externally killed process can leave a new
partial folder; use a different fresh destination when retrying. Original inputs
remain unchanged. Process-kill boundary verification is part of M08.

Linux semantic round-trip fixtures cover all canonical tables, rich/typed values,
large revisions beyond JavaScript's exact integer range, trash/recovery, duplicate
titles, reference states, images/inline assets and retained/legacy bytes. Actual
transfer between Linux, FreeBSD amd64 and OpenBSD amd64 remains a separate
M10 verification requirement for each system.
