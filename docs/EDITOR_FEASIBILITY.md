# M01 editor feasibility report

Date: 2026-10-29 (Asia/Tokyo)

## Decision and status

Use a **page-wide QTextDocument/QTextEdit adapter** as the provisional editor
direction. It supplies continuous selection and a shared native undo history while
allowing the prototype to map text blocks, references, lists, and painted objects
to application-owned values.

This report records the initial feasibility evidence and measurements. On
2026-10-30 the user moved actual OS input-method and screen-reader acceptance from
M01.4 to M08.2. Those checks remain unperformed and are Linux release gates; they
are not implied by automated composition/accessibility tests. Current milestone
status and subsequent verification are tracked in [ROADMAP.md](../ROADMAP.md).

## Runnable comparison

`nebula_prototype --demo --compare-editors` exposes two tabs with the same sample:

- **Page-wide document:** a single native editor with stable block metadata,
  hierarchy operations, inline runs/references, and object representations.
- **Separate blocks:** one native QTextEdit per block. This is a comparison
  baseline, not a second finished editor. It demonstrates native selection and
  undo boundaries before adding a custom coordination layer.

Both approaches receive the same 1,000-paragraph fixture for measurement. Selecting
through multiple paragraphs works in the first approach. Selecting all in one
widget of the second approach selects only that block; undo in one widget does
not reverse edits in another. Focused typing is faster in the separate-widget
baseline. A custom selection/history controller could change that tradeoff, but
would add substantial implementation work.

## Representation and operation ownership

- `include/nebula/core/document.hpp` defines prototype page/block/run/reference/
  asset values using standard C++ types, independent of Qt.
- The ordered block vector is tree preorder; `parent_id` defines hierarchy. Core
  validation rejects duplicate/empty identities, missing or inactive parents,
  paragraph text with hard newline boundaries, and invalid reference/asset values.
- The adapter stores ID, parent, kind, and prototype asset metadata in QTextBlock
  format properties. Inline formatting, reference target IDs, and painted object
  metadata live in character formats. Snapshots translate these to core values.
- Paragraph splits receive a new UUID. Existing children follow the trailing half
  of the split. Merges preserve the surviving block and undo restores the removed
  identities. Normalization repairs relationships after native structural deletion.
- Core movement operates on complete sibling subtrees; indent/outdent preserve
  preorder and descendants. The prototype rebuilds the native document inside one
  undo group for these operations, preserving semantic values and IDs.
- QTextDocument owns this prototype's undo history. Formatting, reference/card
  insertion, split/merge, and structural operations are grouped coherently. History
  is cleared on fixture load and does not survive closing the process.
- Painted image/attachment cards share the document's selection and focus. They
  are layout/interaction representations; no files are imported or managed yet.
- Ctrl+click emits a typed reference target and can navigate to another block in
  the sample page. Workspace-wide resolution, wiki completion, backlinks, and
  unresolved-target workflows remain production milestone work.

These are investigation types and adapters. They do not define the final SQLite
schema or full rich-text serialization format. M02 still needs the production
application operation/storage interfaces and a deliberate undo/coalescing design.

## Test coverage

The core suite has **10 named cases** covering valid/invalid documents, hierarchy,
sibling boundaries, subtree movement, indent/outdent, stable references, Unicode
fixture integrity, and the long-page fixture.

The Qt suite has **27 feature/comparison cases**, plus its initialization and
cleanup hooks:

| Behavior | Evidence |
| --- | --- |
| Model loading and identity | Snapshot validation retains IDs, parents, UTF-8 content, references, and asset metadata. |
| Visual wrapping | Narrowing the view increases layout lines without adding blocks or changing IDs. |
| Enter/Shift+Enter | Split identity and undo/redo checks; soft breaks stay inside one block. |
| Parent splits | BUG-001 regression keeps descendants under the trailing split and restores the tree on undo. |
| Lists and to-dos | Enter continues the list, clears the new checkbox, and preserves undo; marker state also has a focused undo check. |
| Merging | Backspace and Delete retain the survivor's ID; undo restores removed IDs. |
| Clipboard | Keyboard copy/cut across blocks and multiline paste, including CRLF/CR normalization and fresh IDs. |
| Selection/formatting | Keyboard selection and bold/italic span block boundaries and undo correctly. |
| Block kinds | Heading/bullet/paragraph conversion preserves text, reference targets, identities, and list membership. |
| Tree editing | Alt+arrow subtree moves, Tab/Shift+Tab nesting, and surviving-child repair after deletion. |
| References | IDs survive target text edits; typing after a reference does not inherit its target; mouse activation emits the target. |
| Object cards | Selection, deletion/undo, image/file type, and text around an object survive document reordering. |
| Composition | Synthetic preedit stays outside committed content; Japanese/emoji commit and undo preserve IDs and text. |
| Font coverage | The container baseline renders Japanese, Arabic, and emoji; this check is explicitly skipped outside that configured baseline. |
| Accessibility | A native accessible text interface, editor role, and name are exposed. |
| Comparison behavior | Separate widgets exhibit local selection and local undo. |
| Long documents | Editing and undo on the 1,000-block fixture preserve valid structure and identities. |

CTest additionally launches and closes the full prototype. Build/test instructions
are in [README.md](../README.md); compiler/toolkit baselines are in
[DEPENDENCIES.md](DEPENDENCIES.md).

Recorded environments:

| Environment | Checks and result |
| --- | --- |
| Debian 13 Docker image; GCC 14.2; Qt 6.8.2; Release | Core, editor, and launch suites passed with the offscreen backend. |
| Same image; Clang 19.1.7; Qt 6.8.2; Release | Same suites passed with the offscreen backend. |
| Same image; GCC core-only configuration | Standalone core build/tests passed with GUI discovery disabled. |
| Docker GUI through host Xwayland | Native interaction suite, automatic GUI close, and screenshot capture passed; the screenshot was visually inspected. |
| Initial RHEL 9.8 investigation; isolated Qt 6.8.3 SDK | Earlier suites built/ran with GCC 11.5 and Clang 21.1.8; superseded by the Docker workflow after the user's isolation requirement. |
| macOS | Not run; no macOS runner was available. |

The image tag is `nebula-dev:debian13`; the verified image ID is
`sha256:d96a2a1a0a4e223d4df47f2088b052961673eb5cc2f3a0e74fe4a0a0893a6f6e`.
Its base is Debian trixie-slim, with CMake 3.31.6 and Ninja. Qt and font packages
were installed inside the image; the host Qt development package is absent.
Temporary host SDK/download files were removed after the container workflow was
validated. The final development path is Docker-based.

## Measurements and initial budgets

Host: RHEL 9.8 x86_64, Intel Core i5-1345U (12 logical CPUs), about 15 GiB RAM.
The processes below ran in the Docker image using GCC Release, Qt 6.8.2, the
offscreen backend, and the same 1,000-paragraph fixture.

| Metric | Page-wide document | Separate blocks |
| --- | --- | --- |
| Initial load plus first event processing | 38 ms | 190 ms |
| Typing event p95, 30 inserted characters | 15.89 ms | 0.31 ms |
| One adjacent subtree move, page-wide implementation | 33 ms | Not implemented in baseline |
| Active native editors | 1 | 1,000 |
| Process peak RSS | 33,724 KiB | 67,348 KiB |

Commands:

```sh
./scripts/dev.sh benchmark single
./scripts/dev.sh benchmark separate
```

For this fixture and reference machine, use initial investigation budgets of
500 ms load, 50 ms typing p95, 100 ms adjacent move, and 128 MiB peak RSS for the
page-wide candidate. These budgets are now recorded for future comparison; they
are not portable CI performance assertions or end-user hardware guarantees.
The reported values are the final local run, not a statistical performance study.
Earlier runs measured 18 ms load and 6.85 ms typing p95 for the same page-wide
fixture; record repeated measurements before drawing broader latency conclusions.

Timing includes synchronous event delivery and event processing, not a real
compositor/input-method/screen-reader session. Peak RSS is read from Linux
`/proc/self/status` in each fresh process. Each measurement process creates the
candidate widgets and an empty inactive comparison container; the editor count
above describes the active candidate. The prototype snapshots/normalizes all
blocks on mutation and rewrites the document for tree changes. That cost is a
known optimization target before M08, despite meeting the initial budgets here.

## Defects found and remaining validation

- **BUG-001:** a parent split flattened its children. A failing test established
  the case; reparenting inside the same edit group fixed it. The regression passes.
- **BUG-002:** the first container screenshot had missing Japanese/emoji glyphs.
  A font coverage check failed on Japanese. Noto CJK/Color Emoji are now installed
  inside the image; the coverage check and updated native preview pass.
- **BUG-003:** undo after a type conversion moved the caret to the first block, so
  a following conversion could target the wrong block. The conversion/content/
  reference test reproduced it. Starting edit groups with the active editor cursor
  fixes the behavior; the regression also checks focus restoration.
- **Environment constraint:** the initial host build mixed a Linuxbrew linker/
  libraries with the system glibc and failed to link. Explicit system-toolchain
  selection fixed that investigation. The Docker environment avoids the mixture.

M08.2 manual desktop acceptance checklist:

- [ ] Use an actual OS IME: start and change a composition, select a candidate,
  commit/cancel it, and verify Enter, cursor movement, selection, and undo around
  block boundaries. Record the IME, desktop session, Qt version, and results.
- [ ] Use a screen reader with keyboard navigation and selection. Check headings,
  list/to-do state, references, and card labels, not just the editor's accessible
  name. Record findings and create reproducible bug entries where needed.

Production follow-ups already covered by the roadmap include application-level
undo and typing coalescing, lossless internal rich clipboard handling, identity-aware
drag/drop, semantic accessibility for embedded objects, persistent workspaces,
attachment import, full reference resolution, and macOS validation. Ordinary
prototype key events currently create separate undo groups; this is not the final
typing-coalescing behavior.
