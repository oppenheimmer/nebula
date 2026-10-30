# Paste-only text preview adapter

M07.8 permits a single text-preview attempt when the user pastes a complete HTTP
or HTTPS URL. This document records the adapter contract; editor insertion and
removal still need integration and end-to-end verification.

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

## Integration requirements

The editor must start this adapter only from an explicit URL paste, preserve the
URL immediately, and provide a cancel control while waiting. Insert successful
text as an ordinary removable block so canonical runs, existing trash recovery,
clipboard, storage and interchange keep the text without an HTML or image payload.
Removing the preview must leave the URL, and saved notes must open offline.

Before inserting an asynchronous result, verify the originating page/block and
pasted URL still exist. Navigation, removal, undo, replacement and closing the
window must not insert a stale result into another page or revive removed text.
Verify these paths, save/reopen and undo/redo in the full application before
marking M07.8 complete. The current Docker GUI launcher still disables networking;
its runtime policy must be updated during integration, while build/test/data
containers stay offline.
