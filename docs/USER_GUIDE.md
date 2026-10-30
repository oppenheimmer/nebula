# First steps with Nebula

Nebula keeps one person's notes in a local workspace. The current development
workflow is Linux with Docker and X11/Xwayland. Linux is the only target platform.

## Open your first notebook

If the package preview is already installed, launch `nebula`. For a source
checkout, use the project folder with the existing Docker daemon running:

```sh
./scripts/dev.sh build-image
./scripts/dev.sh run
```

The first command downloads development dependencies into the container. Choose
a workspace folder when the app opens; the suggested location is
`~/Document/nebula/`. Reuse that folder next time. **Check:** the window shows your
chosen location. `--demo` is a disposable sample and does not save notes.

Select **Journal**, then **Journal entry**, and choose the day you are writing
about. Other databases can hold ordinary pages. Select a page's table row to
write; double-click table cells to edit its title or properties. Entry date is
separate from the time you created the page. Views filter or sort the same pages.

## Write and find things

| Task | Action |
| --- | --- |
| New paragraph / line within a paragraph | Enter / Shift+Enter |
| Bold / italic | Ctrl+B / Ctrl+I |
| Undo / redo | Ctrl+Z / Ctrl+Shift+Z |
| Paste without formatting | Ctrl+Shift+V |
| Move a block and its children | Drag the grip dots, or Alt+Up/Down |
| Nest / unnest a block | Tab / Shift+Tab |
| Save immediately | Ctrl+S |
| Search notes | Ctrl+Shift+F; select a result to open it |
| Link to another page or heading | **Link…**, or type `[[Page title]]` |
| Follow a reference / go back | Ctrl+Enter or Ctrl+click / Alt+Left |

The active block has a thin outline. Wrapping onto another screen line does not
create another block. Undo survives autosave on the open page; it clears when
switching pages and does not survive restarting the app.

If page titles repeat, choose the intended database/target in **Link…**. An
unresolved link needs an explicit target choice or page creation; merely typing a
name does not create a page. **Referenced by:** and **References** show connections.

Pasting a complete web URL attempts a text-only preview once. **Cancel preview**
stops the attempt; trash its separate paragraph to keep just the URL. No images
are fetched, and opening saved notes does not refresh previews. A failed fetch
leaves the URL intact. Ordinary writing, search and saved notes work offline.

## Add files and protect your notes

Use **Attachments → Import image… / Import file…**. With Docker, launch with
`./scripts/dev.sh run --import-root /path/to/source-folder`; those files appear
under `/imports` in the file chooser. Nebula copies imported files into the
workspace. Moving the original afterwards does not break the attachment.

**Check after editing:** wait for **Saved locally**. A pending or failed save does
not mean the edit is on disk. Confirm a first note by closing and reopening the
same workspace. To make a backup, save and close the window, then run:

```sh
./scripts/dev.sh backup /path/to/workspace /path/to/new-backup
./scripts/dev.sh restore /path/to/new-backup /path/to/new-workspace
```

Choose new destination folders outside the source. Restore creates another
workspace; open it and check a note and attachment before relying on it. Keep the
database and `attachments/` together. Do not copy just a live database as a backup.
**Import / Export → Export lossless JSON folder…** also preserves the supported
model and retained files; Markdown is a limited reading/editing interchange and
omits properties, saved views and trash history.

## If the result differs from what you expected

| What you see | Next step and check |
| --- | --- |
| Empty notes after reopening | Check the displayed workspace path and selected database/view. Open the original folder; do not assume a new empty workspace lost your notes. |
| **Save failed** or edits still pending | Keep the window open, check disk space and write access, then Ctrl+S. Continue only after **Saved locally**. For a revision conflict, use **Workspace → Keep my version as a new page**. |
| A block, page or database was removed | Use Undo while still on the page, or **Restore…**. Red trash icons have different scopes; read the confirmation. **Empty trash permanently…** cannot be undone. |
| Search or backlinks seem stale | Use **Search → Rebuild search and reference indexes**, then repeat the search. A saved view may also be filtering the page out. |
| An attachment is unavailable | Use **Attachments → Check managed files…**. Restore a verified backup into a fresh workspace if the managed bytes are missing; deleting the original import source is normally harmless. |
| Workspace reports it is already open | Close the other Nebula process and retry. Do not remove lock files to bypass a running session. |
| GUI does not launch | Check Docker access and the X11/Xwayland requirements in [README](../README.md#run); do not install Qt system-wide or change display access permissions broadly. |
| Import/restore refuses its destination | Select a new folder; these operations deliberately refuse to overwrite existing data. A backup/export folder must be restored/imported before opening as a workspace. |

If a problem persists, keep the original workspace, the exact error and steps to
reproduce it. Check [README](../README.md) for detailed commands and limitations;
[TRASH](TRASH.md), [ATTACHMENTS](ATTACHMENTS.md) and
[WORKSPACE_FORMAT](WORKSPACE_FORMAT.md) explain recovery and data handling.
