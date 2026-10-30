# Block clipboard format

Nebula publishes `application/x-nebula-blocks+json` alongside ordinary text and
HTML. Its payload is the version 1 document envelope used for recovery fragments:
`version`, source page `id`, and a preorder `blocks` array. Each block contains
`id`, `parent`, numeric `kind`, and a version 1 `payload` with rich-text runs.
Run and block fields are defined in [WORKSPACE_FORMAT.md](WORKSPACE_FORMAT.md).
Writers use block payload version 2 within the version 1 envelope; readers also
accept legacy version 1 blocks. Default/custom label modes survive copying and
internal target remapping. Older readers reject the new nested payload version.
This is application-owned JSON, not QTextDocument serialization.

Copy clips runs to the selection and removes parent relationships outside it.
Formatting, selected block types/hierarchy, checked state, card labels, resolved
references, and unresolved names/scope are preserved. Plain text uses asset labels
and `---` for dividers; soft breaks and paragraph separators become newlines.

Paste validates the complete fragment before replacing the selection. Unknown
versions or invalid trees are rejected visibly, leaving the document and undo
history unchanged. A paste is one undoable edit. New blocks receive fresh IDs;
the first pasted block can merge into the existing destination block and retain
that block's ID. References to selected blocks are remapped to their pasted
identities. Other reference targets and unresolved names remain unchanged.
Original children follow the trailing root, as when Enter splits a parent.

Cut/paste also creates pasted identities. It is not an identity-preserving move
between pages; references from elsewhere still identify the original blocks.
Use Workspace → Move block to page… for an identity-preserving subtree transfer.
Clipboard data does not transfer page properties, database definitions, views,
workspace identity, or managed files. Asset cards currently carry representations
and labels. A reference copied from another workspace can remain unavailable.

For external HTML, the supported subset is paragraphs, headings, lists, bold,
and italic. Other presentation and external hyperlink destinations are discarded
while their text remains. Images become `[image]`. A resource-free document
adapter prevents loading images, stylesheets, or other external resources. HTML
does not import files or create Nebula references by guessing titles or URLs.

Plain text normalizes CRLF/CR to LF and uses paragraph separators to create
blocks. HTML and Nebula formats take precedence when present; an invalid Nebula
payload is rejected rather than silently using its text fallback.
