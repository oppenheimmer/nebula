# Nebula 0.1-alpha screenshots

Captured on 2026-11-02 from the application widgets at release source
`d4d794a4f6767b52a6b98acd9b8963d4485849ee` (tag `0.1-alpha`, package version
`0.1.0~alpha`). All notes, dates, references and attachment contents are fictional
example data created in a temporary workspace.

| File | View | Dimensions |
| --- | --- | --- |
| [01-block-editor.png](01-block-editor.png) | Block editing, nested lists, tasks, a managed file and backlinks | 1400 × 950 |
| [02-journal-views.png](02-journal-views.png) | Journal entries, typed properties and a saved date-sorted view | 1400 × 950 |
| [03-workspace-search.png](03-workspace-search.png) | Full-text search with a reference filter across databases | 950 × 700 |

The capture used the existing Debian 13 amd64 Docker build (GCC 14.2, Qt 6.8.2),
offscreen Qt and DejaVu Sans 10-point UI text. A temporary driver linked the
release's application libraries, selected pages/views through their existing
controls, and saved `QWidget::grab()` output as PNG. Window and table widths were
adjusted for readability. These are direct widget captures, without desktop
window decorations or image retouching; the application rendering was unchanged.

The fixture checked both backlinks, eight journal rows, two filtered search
results, navigation into the selected result, successful persistence and workspace
integrity. Captures were visually reviewed for legibility and content. No personal
workspace, host display or network access was used. Capture workspaces and build
helpers remain outside Git.
