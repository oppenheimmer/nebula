# Markdown subset

Import / Export → Export Markdown folder… writes all active pages into a new
folder. Import Markdown folder… creates new pages in the selected database.
Both operations save pending edits first and show a conversion report. Existing
pages are never overwritten. Export also writes CONVERSION.txt with its warnings.
Choose a new folder name for export, rather than an existing folder.

Docker command-line equivalents are:

```sh
./scripts/dev.sh export-md /path/to/workspace /path/to/new-markdown-folder
./scripts/dev.sh import-md /path/to/markdown /path/to/workspace DATABASE_ID
```

The source folder is mounted read-only. Import changes only the explicitly selected
workspace. The database ID appears in search/target selectors and in the workspace
database. In the GUI, import folders exposed through `--import-root` are available
at /imports; exports can be written into a new subfolder of the writable workspace.
Exported folders are not included in workspace backups.

## Supported representation

- Each top-level, UTF-8 .md file becomes a page. Its first `# Title` line supplies
  the title; otherwise the filename stem does. Duplicate titles remain separate.
- Blank lines delimit blocks. `## Heading`, `- bullet`, `- [ ] todo`,
  `- [x] done`, and `---` represent their corresponding block kinds.
  Consecutive list/heading/divider lines start separate blocks. Other consecutive
  lines form one paragraph with soft breaks.
- Asterisk bold/italic and backslash punctuation escapes are supported.
  `<br>` represents a soft break. Arbitrary HTML is literal text, without loading
  resources. Unbalanced asterisk emphasis applies through the paragraph end and
  produces a warning.
- Exported filenames use `id-PAGE_ID.md`. IDs with unsafe filename characters
  use a tilde plus UTF-8 hexadecimal spelling. Titles never supply filenames.
  Each block has a preceding `<a id="block-BLOCK_ID"></a>` marker, using the same
  safe spelling. Empty blocks use `<!-- nebula-empty -->`.
- Relative `[label](other.md)` links resolve to files in the imported set.
  `[label](other.md#block-ID)` resolves to an explicit block marker. Every imported
  page/block gets a fresh ID, allocated before links resolve, including forward
  references. Missing files/anchors become explicit unresolved references; they
  are never matched by title or created automatically.
- `![label](attachments/FILE)` imports an image and
  `[label](attachments/FILE)` imports a file. Export copies referenced managed
  bytes into that folder and verifies registered hashes. Import copies bytes into
  new managed IDs, preserving original inputs. A standalone asset becomes a card;
  an asset among text becomes an inline asset. Asset names are display labels.

This is an application subset, not a general CommonMark or another app's import
implementation. Unsupported heading levels, nesting, quotes, underscore emphasis,
code, tables and other constructs remain literal text with a warning. External
links, remote images and unsupported link destinations become label text with a
warning. Nothing is fetched. Local attachment paths are restricted to a single
filename below attachments/; traversal, encoded paths, symlinks, missing files,
invalid UTF-8/NUL input, duplicate block markers and files above 64 MiB fail
explicitly. Inputs are not recursively scanned; ignored entries are reported.

## Conversion limits

Markdown omits database membership, typed properties, saved views, timestamps,
revisions, trash and deleted-block recovery. Export flattens block nesting and
reports it. Default reference labels become fixed labels after import.
Unresolved, unavailable, trashed or out-of-set destinations export as visible label
text, with a warning. Legacy cards lacking managed bytes also become label text.
Embedded NUL text becomes a replacement character and is reported. This format
does not preserve the full Nebula model; use lossless JSON when available.

Title line breaks become spaces, and bold/italic presentation on inline assets is
omitted, with warnings. Managed image bytes are copied without a decoder pass;
an unsupported image remains retained even if its preview cannot be rendered.
Open an older workspace normally to upgrade it before using headless interchange.

Imports commit all new pages, blocks, references, attachment registry rows and
search updates in one transaction. Failure preserves existing data and removes
uncommitted copied files when possible; interrupted leftovers follow the managed
attachment audit/cleanup rules. Export refuses existing destinations and removes
partial output on a reported failure. No merge or overwrite import mode exists.
