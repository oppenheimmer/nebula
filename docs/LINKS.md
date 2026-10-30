# Local reference entry

All references identify a page or a globally unique block in the current workspace.
Page names, database names and visible labels are not persisted identities.

Type a wiki expression in the editor, or use Link… and its searchable target list:

| Input | Destination |
| --- | --- |
| `[[Project]]` | Page with the exact title Project. |
| `[[Project|reading notes]]` | That page, displayed as reading notes. |
| `[[Project#Meeting]]` | Heading whose full text is Meeting on that page. |
| `[[Project#^block-id]]` | Block with the exact ID block-id on that page. |
| `[[#Meeting]]` / `[[#^block-id]]` | Heading/block on the current page. |

Backslash escapes `\`, `[`, `]`, `#` and `|` in titles or labels; for example
`[[A\#B\|C|label\|text]]` identifies the page A#B|C and displays label|text.
Other escape sequences, extra separators, empty targets/labels, and expressions
crossing paragraph boundaries remain ordinary text. Surrounding spaces/tabs in
page/heading targets are trimmed. Labels retain their spaces. Target matching on
closing `]]` is exact and case-sensitive. Unicode is preserved.

Typing `[[` opens completion with database, page, target text/type, and ID context.
Suggestions use case-insensitive substring terms; completion displays at most
100 matching targets. Select a specific suggestion with the mouse or arrow keys
and Enter/Tab; Escape dismisses it. The full Link… picker remains searchable when
the target is outside those suggestions. It does not select a target implicitly.
The integration follows Qt's [custom-completer example](https://doc.qt.io/qt-6/qtwidgets-tools-customcompleter-example.html).

Closing a uniquely matching expression converts only that expression to a typed
reference. Duplicate page titles or duplicate heading text open the picker and
require an explicit choice. Cancel preserves the typed expression. Unknown page
titles become unresolved references with the intended name and current database
scope; no page is created. Missing heading/block expressions stay text so their
intended precise destination is not replaced with a page target. Malformed or
incomplete expressions stay text. Conversion is independently undoable, restoring
the original wiki expression. Undo/redo and autosave preserve the selected IDs.

Wiki recognition runs on typing and committed input-method text. Pasted wiki
syntax remains text; Nebula's semantic clipboard carries typed references instead.
Omitting `|label` creates a default label that follows the target title/text when
opened or edited. Explicit labels remain fixed. Editing default label text makes
it custom; undo/redo retains that choice. Same-page heading/title edits and their
default-label updates undo together. Legacy labels retain their original fixed
behavior. Default labels inside a block's own title use `[link]` when deriving
other reference labels, preventing recursive expansion.

Following an unresolved link opens explicit actions to choose an existing page,
heading or block, or create a named page in a selected database. Creating the page
and binding the source commit together. Cancel preserves the reference. Undoing
the binding keeps the explicitly created page and restores the intended name/scope;
redo restores the same target ID. Selecting an existing target changes only the
activated occurrence and preserves a custom label's rich formatting.

Ctrl+click or Ctrl+Enter at the editor cursor follows a reference. **Referenced by:**
shows the referring page and source excerpt, including the exact target block ID
where applicable. **References** includes each outgoing label, destination/state and source
excerpt; unresolved links remain visible. Both lists support keyboard selection
and Enter to activate. Incoming activation opens the referring block; outgoing
activation opens the exact target or explains its unresolved/unavailable state.

Navigate → Back/Forward (Alt+Left/Right) restores stable page/block positions,
cursor/selection offsets, and vertical scroll. History covers same-page block
jumps as well as page/database navigation, holds up to 200 prior positions, and
lasts for the window's lifetime. A new destination clears forward history. Later
text edits clamp saved offsets; if the recorded block is gone, navigation opens
its original page at the first block and reports that fallback. Unavailable or
trashed page destinations and failed saves leave history entries intact for retry.
History records view positions, not copies of page contents.

Resolution reports a trashed page or database and
keeps unavailable targets explicit. Restoring the original target reconnects its
ID; creating a page with the same name does not. Moving blocks between pages
uses Workspace → Move block to page… to preserve identities. See
[WORKSPACE_FORMAT.md](WORKSPACE_FORMAT.md) and
[CLIPBOARD_FORMAT.md](CLIPBOARD_FORMAT.md) for persistence and copying contracts.
