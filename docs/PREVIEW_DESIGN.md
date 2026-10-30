# Paste-only text preview adapter

M07.8 permits a single text-preview attempt when the user pastes a complete HTTP
or HTTPS URL. The desktop editor connects explicit URL pastes to the bounded
fetcher and stores successful previews as ordinary text blocks.

## Fetch contract

`TextPreviewFetcher` performs asynchronous GET requests with a five-second total
deadline and a 512 KiB aggregate response limit, including redirect bodies. It
follows at most three redirects, all on the original scheme, hostname and port.
Cross-origin redirects, including HTTP-to-HTTPS upgrades, leave the preview
unavailable. The original pasted URL must remain in the note on every failure.

The fetcher sends no cookies, authentication credentials or Referer. It neither
keeps received cookies nor uses a persistent HTTP cache or system proxy. URLs
containing credentials are rejected. Authentication challenges fail instead of
opening a login prompt. HTTPS uses Qt's normal certificate and hostname checks;
the adapter never ignores TLS errors. Cancellation aborts the active reply.

Explicitly pasted private-network and loopback URLs are permitted. This supports
the user's own local/intranet pages. Only the pasted origin may receive the GET;
a redirect cannot change host, scheme or port. Consequently, pasting a URL sends
its path and query to that host even if no useful preview results. There are no
fetches from note opening, typing, link navigation, indexing, or previews already
saved in a note. No other runtime networking is authorized by this contract.

## Extracted text

Only `text/html`, `application/xhtml+xml` and `text/plain` responses are accepted.
The adapter decodes supported declared/BOM/HTML encodings, rejects malformed
encoded text, and returns a title of at most 160 UTF-16 units and an excerpt of
at most 400 units, plus an ellipsis when shortened. It avoids splitting surrogate
pairs. HTML uses the document title and description metadata when present, with
visible body text as a fallback. Extraction is best effort, not a browser render.

An isolated QTextDocument overrides resource loading. Scripts do not execute;
images, thumbnails, stylesheets, frames and local files are never imported or
retrieved. Output is plain text, with whitespace normalized and control/format
characters and image object placeholders removed. Unsupported content, empty
text, authentication, network/TLS failures, excess bytes, timeout and cancellation
return an error and no preview.

## Editor integration

Pasting a complete URL inserts the URL immediately and starts one attempt.
Normal paste, Paste plain text, and copied canonical plain URL text share this
behavior. Ordinary text containing a URL and internal references do not fetch.
External HTML accompanying a complete URL is ignored, including image markup.
The status bar offers a keyboard-accessible Cancel preview control while waiting.
Successful title/excerpt text becomes a separate paragraph after the source
block's subtree, with the same parent. The title is bold; the excerpt follows a
soft line break. No preview metadata, HTML, images or attachment files are stored.

Remove the preview with the normal block trash control while retaining the URL.
Preview insertion is a separate undo step; clipboard, save/reopen, deleted-block
recovery, backup and lossless interchange preserve its canonical text. Undo/redo
and reopening never repeat the request. A save failure retains pending edits and
reports the failure through the normal save state.

The controller verifies the originating page/block and a cursor bookmark of the
original URL before applying a result. Navigation, removal, undo, replacement,
newer pastes, hiding and destruction cancel stale work. Insertion preserves the
current selection and scroll position, waits for IME composition to end, and
waits for modal operations to close. These paths have controller and full-window
regression checks; synthetic composition checks do not establish OS IME acceptance.

The Docker launcher gives only the normal workspace GUI bridge networking.
Builds, tests, the chooser, demo/smoke runs, and data commands retain
`--network=none`. Test HTTP/TLS fixtures run on container loopback; tests do not
depend on an external website. Ordinary note operations remain available offline.
