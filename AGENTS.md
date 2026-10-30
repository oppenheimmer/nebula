# Project instructions

## Purpose and scope

Nebula is an always-local, single-user desktop note-taking and diary application.
Database entries open as pages containing flowing, movable blocks. Linux is the
initial release target; macOS follows. Zim Desktop Wiki is an additional behavioral
reference for document links and backlinks. Read `ROADMAP.md` before planning or
changing the project, and keep work aligned with the current user request.

The roadmap describes intended work; it is not blanket authorization to implement
all milestones during a documentation or discussion task. User instructions take
precedence over this file. Continue authorized work without repeatedly requesting
confirmation for routine, reversible implementation decisions.

Do not introduce synchronization, hosted services, collaboration, accounts,
telemetry, or runtime network dependencies. A spatial canvas is outside the agreed
scope. Do not expand first-release scope implicitly.

The user authorized M07.8 on 2026-10-30 as a narrow exception: fetching text from
a web page when its link is pasted, for a removable text-only preview. Do not
import images/thumbnails or refresh previews on opening notes. Other note
operations must continue to work offline; no other runtime networking is implied.

Development dependencies must remain isolated to this project's development
environment. Use the existing Docker CLI with `Dockerfile.dev` and
`scripts/dev.sh`; never install Qt or other project dependencies system-wide or
invoke a host package manager for them. An explicitly selected project-local SDK
is a fallback, not a global PATH/library configuration. Do not install Podman as a
prerequisite. Keep SDKs, container build output, and tool caches out of Git.

## Architecture and dependencies

- Use the agreed C++17, Qt 6 Widgets, SQLite, and CMake direction. The editor
  architecture remains subject to the M01 prototype; document evidence for any
  proposed change to the selected toolkit or language baseline.
- Keep the document core independent of Qt GUI types and testable without a
  window. Put platform integration behind narrow adapters.
- Separate document rules, application operations, storage, editor/view adapters,
  attachments, and import/export. Internal modules/build targets are sufficient;
  do not add a runtime plugin framework without a concrete requirement.
- Use application-owned stable identifiers and a documented, versioned persistent
  format. Do not make toolkit serialization the authoritative document format.
- Preserve typed properties, valid block trees, explicit sibling order, and the
  distinction between visual wrapping and paragraph boundaries.
- Keep view settings separate from page contents. Database views operate on the
  same underlying pages rather than creating copies.
- Keep cross-referencing local to the workspace, including links across databases.
  Resolve page, heading, and block references by stable IDs; titles and custom
  display labels must not become persistent identities. Disambiguate duplicate
  titles and preserve unresolved or unavailable targets explicitly.
- Derive backlinks from canonical inline references through a rebuildable link
  index. Preserve reference behavior across renames, moves, trash/restore, undo,
  and import/export identity remapping. Indexing must not create pages or silently
  redirect references by name.
- Maintain conservative dependency and compiler baselines. Record the reason for
  new dependencies, their licenses, and their Linux/macOS implications.
- Nebula's own source uses MIT, as selected by the user. Keep third-party licensing
  separate and record platform decisions in the roadmap before distribution.

## Data reliability

- Treat notes and attachments as user-owned data. Use transactions for related
  storage changes and report save failures instead of claiming success.
- Document autosave durability and pending-edit behavior; a visible saved state
  requires successful persistence.
- Add versioned migrations for schema changes. Verify existing-data preservation
  and safe handling of unsupported formats.
- Import attachments into managed storage and coordinate references, trash,
  cleanup, and backup retention. Never assume the original source path is stable.
- Back up the database and its referenced attachment files consistently. Verify
  restore into a fresh workspace, including attachment availability.
- Keep diary entry dates distinct from creation/modification timestamps.
- Preserve full model information in the lossless interchange format; document
  limitations of Markdown conversions instead of silently dropping information.

## Milestone and issue management

- Identify the relevant milestone and task IDs before implementation. Respect
  dependencies; record a reason if work changes their order or scope.
- Update affected checkboxes, milestone status, and the work log with the change.
  Use Not started, In progress, Blocked, or Complete; avoid invented percentages.
- When rescheduling tasks, move their full requirements into the destination
  milestone and renumber the affected lists in execution order. Remove old-location
  pointers; record the scheduling decision once in the work log.
- Keep progress updates, completed-check results, encountered bugs, and remaining
  milestone work in `ROADMAP.md`. Keep `README.md` focused on the project,
  setup, usage, and material data-handling limitations; do not add progress snippets.
- Mark a milestone complete only when its deliverables and exit criteria have
  evidence. Distinguish prototypes, production behavior, and planned work.
- Keep unresolved decisions visible. Record significant choices and their reasons
  in the roadmap; link a separate design document if a decision needs more detail.
- Record reproducible defects with a stable BUG identifier, environment, expected
  and actual behavior, diagnosis, resolution, and verification. Preserve resolved
  entries. Anticipated problems belong in the risk register, not the bug log.
- Add learned constraints to the gotcha log with evidence and an affected milestone.
- Work-log entries must describe checks actually run and their outcomes. Explicitly
  record unavailable checks and environmental limitations.
- Use the user's timezone for work-log dates; the initial project timezone is
  Asia/Tokyo.

## Validation

- Add focused automated tests alongside every new feature. Cover its observable
  behavior and preserve regression checks for defects found during implementation.
- Validate changes proportionately. For documentation, review scope, consistency,
  local links, status accuracy, and `git diff --check`; application tests are not
  required for prose-only changes.
- For implementation, run the relevant build and focused checks before reporting
  completion. Add meaningful tests for document invariants, persistence/migrations,
  recovery, or demonstrated regressions rather than tests that mirror code.
- Exercise editor behavior directly: selection across blocks, split/merge,
  navigation, clipboard, undo/redo, Unicode, input-method composition, and keyboard
  access. Screenshots alone are insufficient evidence.
- Exercise the full application window as well as isolated editor widgets; verify
  focus and shortcut routing. Identify the active block with a thin outline and
  subtle shadow. When mouse reordering is implemented, provide grip dots as the
  block's drag target and retain keyboard equivalents.
- Use representative fixtures and recorded environments for performance claims.
  Establish thresholds before treating a benchmark as a pass/fail release gate.
- Record Linux and macOS verification separately. A Linux build does not establish
  that macOS builds or behaves correctly.
- Once relevant checks pass, repeat or broaden them only when new changes,
  failures, or unresolved concerns justify it.

## Git and communication

- Inspect repository status before changing or committing files. Preserve unrelated
  user work and stage only files belonging to the requested change.
- Use the configured Git author identity. Do not change identity or signing
  configuration to attribute work to an agent.
- Use concise, descriptive commit messages. **Never add co-author trailers,
  agent-identifying trailers, or generated-by attribution to commit messages.**
- Commit and push when authorized by the user; existing authorization applies
  throughout the task. Use normal pushes to the intended remote/branch. Do not
  force-push or rewrite existing history without explicit authorization.
- If a push or check fails, report the actual outcome and preserve the completed
  work. Do not describe a local commit as successfully pushed without evidence.
- Report what changed, how it was checked, and any remaining limitation. Keep the
  roadmap accurate rather than using the final response as the only progress log.
