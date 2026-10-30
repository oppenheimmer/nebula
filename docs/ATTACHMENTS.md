# Managed files and backup

Attachments → Import image… / Import file… copies a regular source file into the
workspace, inserts a card after the active block's subtree, and adds an empty
paragraph for continued writing. Import saves the file metadata and page together.
Changing or deleting the original source does not change the imported bytes.
Cancel creates no attachment. Symlinks and special files are rejected.

Image cards preview local bytes through the installed Qt image readers. Previews
decode on a worker thread, never while painting, with a 128 MiB reader allocation
limit, a 40-million-source-pixel limit and a bounded 32 MiB preview cache;
unsupported or oversized previews remain stored with an unavailable-preview label.
Card file paths are re-resolved at most once per second, so a missing managed file
shows as unavailable shortly after it disappears. Select the card and use Open
current attachment or Save a copy…. Opening copies the managed bytes, after checking
their checksum, into a private temporary folder under the original file name, makes
that copy read-only (0400) and hands it to the desktop handler; the managed file
itself is never given to another program, and the copies are removed when the window
closes. Saving a copy verifies its checksum and uses an atomic output file. External
desktop opening depends on the installed local file handler. Development containers
may have no desktop handler; Save a copy… remains available.

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

Import writes a temporary `.import-<id>` file, flushes its bytes, publishes its final
name without overwriting an existing identity, and flushes the directory before
committing SQLite metadata/page/index changes. The copy and its SHA-256 run on a
worker thread with a progress dialog and Cancel while the exclusive file lock is
held; the database transaction starts only after the copy completes, so large files
do not block other database writers. Save failure rolls back those records and
removes new files where possible. A process interruption can leave an unregistered
staging/final file, which is recoverable through the audit. Notes do not become saved
before their file and database commits succeed.

Attachments → Check managed files… verifies availability and checksums and lists
unregistered leftovers. Cleanup rechecks under an exclusive file/database lock and
deletes only an explicitly selected unregistered file with no references. Registered
imports remain retained, including when only in-memory undo may need them; they are
included in backups even if currently unreferenced. Live notes, trash and every
retained block-recovery fragment are inspected. Registered imports are released only
by Restore… → Empty trash permanently…, after trash, recovery records and editor undo
that could still use them are gone. No automatic file deletion or source-path cleanup
occurs.

`.nebula.lock` coordinates imports/cleanup with shared backup reads. A busy operation
reports that it must be retried. Backups copy an SQLite snapshot and all
managed/legacy regular attachment files while holding the shared lock, hashing each
file once as it is copied, then check registered files against those digests. Files
are read component by component without following links. Missing, changed, unsafe or
unavailable referenced data fails the backup and removes its partial output.

`backup-manifest.json` version 1 contains `workspace_id` and an ordered `files`
array. Every file entry has a UTF-8 relative `path`, decimal-string byte `size`, and
lowercase SHA-256. The list covers `nebula.sqlite3` and all regular files under
`attachments/`. Directory entries and the coordination lock carry no user content and
are not hashed. Restore rejects path traversal, symlinks, duplicates, extra or
missing files, checksum mismatches and unsupported manifests before creating the
destination, and checks each copied file against the verified digest. The manifest
detects accidental corruption or incomplete copies; it is not tamper evidence,
because whoever can edit a backup can also regenerate its manifest. Manifests above
64 MiB are rejected explicitly. Schema 7 backups require a manifest; legacy backups
remain supported and are migrated in the new workspace. The original backup is
unchanged. The restored live workspace removes the snapshot manifest because
subsequent edits change its database.

File durability and locking use a narrow POSIX adapter (native BSD preflight recorded in the roadmap; desktop/release validation pending)
for regular files, flushes, same-directory hard links and shared/exclusive locks.
Linux verification is recorded in the roadmap. FreeBSD amd64 and OpenBSD amd64
behavior each require M10 verification. The current import/audit operations are
synchronous; large-file responsiveness remains part of M08 performance validation.
