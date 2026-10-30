# Local search

Use Search → Find in workspace… (Ctrl+Shift+F). Pending edits save before the
dialog opens; a failed save keeps the editor open. Search all active databases or
select one by its stable ID. Results show page/database context, one result per
page, up to 100 pages. Select a result and press Enter or Open to navigate to its
matching block. Queries spanning multiple blocks can open the page instead.
Back/Forward retains the previous editor selection and scroll position.

Indexed text includes titles, block text, card/inline-asset labels, and populated
property names and values. Number, date, checkbox and tag values retain their
canonical types; only their search representation becomes text. Date properties
remain calendar dates. Default reference labels derive from current targets by ID.
Renaming a target updates referring pages in the same save transaction. Unresolved
references remain unresolved; searching never creates or binds pages. Trash and
trashed databases are excluded. Deleted fragments are not search results.

## Query and language behavior

Whitespace-separated terms are AND-combined. Each term is literal text; quote
marks, parentheses, OR, and SQL syntax do not introduce query operators. A trailing
asterisk requests a token prefix, for example `walk*`. A bare `walk` matches a
whole token, not `walking`. Punctuation inside a term separates tokens that must
occur together in order. Empty/punctuation-only queries return no results.
Queries allow at most 32 terms and 4096 UTF-8 bytes; invalid UTF-8 is rejected.
The desktop entry limits input to 1024 UTF-16 code units.

Search copies use Unicode NFKC normalization and case folding. SQLite FTS5
`unicode61 remove_diacritics 2` supplies word boundaries and Latin accent folding:
`CAFE` finds `Café`, and ASCII finds fullwidth Latin text. Japanese kana, Chinese
Han, Korean Hangul/Jamo, and Bopomofo are separated into character tokens so a
contiguous query such as `本語` matches inside `日本語`. Spaces between query
groups permit matches anywhere in a page. Latin, Japanese, simplified Chinese,
Korean and Arabic fixtures are automated; this is character/word matching, without
stemming, reading conversion, transliteration, fuzzy search or a language dictionary.
Emoji-only queries are not supported. Embedded NUL characters become spaces only
in derived search text; canonical note text is preserved.

## Index ownership and repair

Workspace schema 8 adds `page_search`, an FTS5 virtual table. SQLite must be built
with FTS5 enabled; missing support rejects the transactional migration while
preserving the previous database. No external tokenizer/library is required.
The core document model remains independent of Qt and SQLite. The storage adapter
uses existing Qt Core Unicode operations.

Each page has per-block rows plus a whole-page row for terms across blocks.
Titles/properties accompany both. Results use FTS5 BM25 relevance, then title and
block ID; ranking is not a persistent identity or a guaranteed ordering across
index implementations. Canonical page/database joins determine availability.
Related note, reference and search changes commit together. Failed index writes
fail the save and retain pending edits.

Search → Rebuild search index drops and reconstructs only the derived table from
canonical notes/properties/references, then saves any pending edits. Page IDs,
revisions and note text are unchanged by the rebuild itself. Backup restore also
reconstructs the index. This repairs missing tables/rows but cannot repair damaged
canonical content; those failures are reported. Imports, queries and rebuilds run
locally without network access.

Tokenizer, phrase escaping, prefix and ranking behavior follow the
[SQLite FTS5 documentation](https://sqlite.org/fts5.html).
