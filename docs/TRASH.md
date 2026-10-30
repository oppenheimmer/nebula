# Trash and recovery

Nebula gives each recoverable trash operation a small red trash-icon button.
Hover or keyboard-focus the button to identify its scope. The same commands are
available in the Workspace menu.

| Button location | Scope | Recovery |
| --- | --- | --- |
| Beside the database selector | The selected database, including its pages, properties and views. Pages already trashed keep that state. | Restore the database to make its active pages visible again. |
| Beside the page title | The open page and all of its blocks/properties. Other pages remain active. | Restore that page. |
| In the block toolbar | The active block and its nested blocks, as identified by the active outline. Other blocks remain in the page. | Undo while the page stays open, or Restore the saved block group. |

Each button opens a confirmation naming the target, its identity and the number
of affected pages or blocks. Cancel is the default; Escape cancels. Moving a whole
database also requires an initially unchecked acknowledgement checkbox before the
confirmation button becomes available.

Notes save before the confirmed move. If pending edits cannot save, page/database
trash does not proceed. A failed block-trash save restores the removed content in
the editor and reports the save failure; the existing saved document stays intact.
Trashing the last block/subtree leaves a new empty paragraph, so the page remains
valid. Successful block trash is a single undoable editor command. Undo and redo
preserve block identities and reference targets.

These three controls move content to recoverable Trash. They never run permanent
purge or remove imported attachment files. Pages and databases retain their
identities, and saved removed blocks retain the information needed for recovery.
Existing references can report a target as trashed or unavailable until restored;
they are not redirected to similarly named notes.

## Restore and permanent deletion

Use Restore to recover a database, page or saved block group after reopening.
Restore a database before its pages and a page before its blocks. Restore also
keeps existing edits to blocks that are already present. Undo history is limited
to the currently open page/window; Restore is the route after navigation/restart.

A separate Empty Trash operation, when available, permanently removes retained
content and unused files after its own explicit confirmation. It is distinct
from the red recoverable-trash controls. Permanent deletion cannot be undone;
existing backups and exports keep their independent copies. Keep a verified
backup before choosing permanent deletion.

The page's reference panels use **Referenced by:** for incoming references and
**References** for outgoing references. Their keyboard navigation and stable-ID
resolution are unchanged by those labels.
