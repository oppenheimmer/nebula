# Managed files and backup

Attachments → Import image… / Import file… copies a regular source file into the
workspace, inserts a card after the active block's subtree, and adds an empty
paragraph for continued writing. Import saves the file metadata and page together.
Changing or deleting the original source does not change the imported bytes.
Cancel creates no attachment. Symlinks and special files are rejected.

Image cards preview local bytes through the installed Qt image readers. Preview
decoding is limited to 40 million source pixels, with a bounded 32 MiB preview
cache; unsupported or oversized previews remain stored with an unavailable-preview
label. Missing managed files show an unavailable state. Select the card and use
Open current attachment or Save a copy…; saving a copy verifies its checksum and
uses an atomic output file. External desktop opening depends on the installed
local file handler. Development containers may have no desktop handler; Save a
copy… remains available.

The Docker launcher exposes only the chosen workspace by default. To import host
files, explicitly expose one existing folder read-only:

```sh
./scripts/dev.sh run --import-root "$HOME/Pictures"
```

The file chooser starts at `/imports`. This does not make the host home writable
or mount other folders. Export copies can be saved in the mounted workspace;
installed desktop builds use the user's normal filesystem access.

Each file has a permanent random ID, original basename, byte count, SHA-256 and
image/file presentation flag. Managed bytes live at `attachments/<id>` and are
immutable to the application. No original absolute path is persisted. Block and
inline card references retain IDs through nesting, movement, clipboard and undo.
Copying a card to another workspace retains an explicitly unavailable ID if that
workspace lacks the file; clipboard paste never fetches or guesses a file path.

Import writes a temporary `.import-<id>` file, flushes its bytes, publishes its
final name without overwriting an existing identity, and flushes the directory
before committing SQLite metadata/page/index changes. Save failure rolls back
those records and removes new files where possible. A process interruption can leave an
unregistered staging/final file, which is recoverable through the audit. Notes do
not become saved before their file and database commits succeed.

Attachments → Check managed files… verifies availability and checksums and lists
unregistered leftovers. Cleanup rechecks under an exclusive file/database lock
and deletes only an explicitly selected unregistered file with no references.
Registered imports remain retained, including when only in-memory undo may need
them; they are included in backups even if currently unreferenced. Live notes,
trash and every retained block-recovery fragment are inspected. Permanent purge
of registered imports is outside this cleanup policy. No automatic file deletion
or source-path cleanup occurs.

`.nebula.lock` coordinates imports/cleanup with shared backup reads. A busy
operation reports that it must be retried. Backups copy an SQLite snapshot and
all managed/legacy regular attachment files while holding the shared lock, then
verify registered files against their imported hashes. Missing, changed, unsafe
or unavailable referenced data fails the backup and removes its partial output.

`backup-manifest.json` version 1 contains `workspace_id` and an ordered `files`
array. Every file entry has a UTF-8 relative `path`, decimal-string byte `size`,
and lowercase SHA-256. The list covers `nebula.sqlite3` and all regular files under
`attachments/`. Directory entries and the coordination lock carry no user content
and are not hashed. Restore rejects path traversal, symlinks, duplicates, extra or
missing files, checksum mismatches and unsupported manifests before creating the
destination. Manifests above 64 MiB are rejected explicitly. Schema 7 backups
require a manifest; legacy backups remain supported and are migrated in the new
workspace. The original backup is unchanged. The restored live workspace removes
the snapshot manifest because subsequent edits change its database.

File durability and locking use a narrow Linux/macOS adapter for regular files,
flushes, same-directory hard links and shared/exclusive locks. Linux verification
is recorded in the roadmap. macOS behavior still requires M10 verification. The
current import/audit operations are synchronous; large-file responsiveness remains
part of M08 performance validation.
