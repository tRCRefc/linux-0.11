# Repository Collaboration Instructions

These instructions apply to the whole repository and must be followed in every
new development session.

## Required Context

Before changing source, assembly, build configuration, or tests:

1. Read `notes/README.md` and the README for the current development stage.
2. Read `NEXT_STEP.md` for planned work, but do not treat plans as implemented.
3. Inspect `git status`, recent commits, and the relevant source history.
4. Preserve unrelated user files, local notes, and generated output.

Read-only questions and status inspections do not require a note update unless
they produce new durable review findings or validation evidence.

## Required Note Maintenance

Updating `notes/` is part of completing development work. The directory is
temporarily tracked as part of the repository migration snapshot described in
`MIGRATION.md`; after that documented cleanup is committed, it returns to being
local-only.

- After a source or configuration capability changes, update the corresponding
  stage note before ending the session.
- Organize the update around the commit or pending atomic change: behavior,
  system effect, validation evidence, and remaining limitations.
- Distinguish implemented, verified, and planned behavior. Compilation alone
  must not be reported as runtime verification.
- If work is not committed, label it as an uncommitted work in progress. After
  a commit is created, replace that marker with the exact commit hash and title.
- Update `notes/README.md` when the current baseline, stage range, or stage list
  changes.
- Start a new stage file when work begins a new system responsibility such as
  exec, file-backed paging, PID 1, or interrupt-driven scheduling. Do not grow
  one catch-all note indefinitely.
- Record useful temporary QEMU probe results, but do not retain probe code in
  the formal source path unless a separate test target is intentionally added.
- Keep `NEXT_STEP.md` at the repository root as the development plan. It is not
  part of the learning-note archive.
- While the migration snapshot is tracked, include relevant note updates in the
  staged change. After the cleanup in `MIGRATION.md`, keep `notes/` local and do
  not stage it again unless the user explicitly changes the policy.

Use `notes/session-checklist.md` as the concrete start and completion checklist.

## Completion Gate

Before reporting development work complete:

1. Confirm formal source and temporary probes are in the intended final state.
2. Run validation proportional to the changed behavior.
3. Update the relevant stage note with the actual evidence.
4. If a commit was requested, record its exact hash in the notes after commit.
5. Confirm the staged state of `notes/` matches the migration policy in
   `MIGRATION.md`, and that generated `build/` output is absent.
6. Mention the note update in the final handoff.

Development work is not complete when code is committed but the corresponding
learning notes still describe an older baseline.
