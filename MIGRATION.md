# Repository Migration Snapshot

This repository temporarily tracks local development documentation so a clone
can carry the complete project context to a new environment.

## Included in the snapshot

- `notes/`: stage notes, reviews, validation evidence, and reading guides;
- `AGENTS.md`: repository collaboration and completion rules;
- `NEXT_STEP.md`: the current development plan.

Generated files under `build/` are deliberately excluded. They contain object
files, EFI binaries, disk images, firmware variable copies, and serial logs that
can be reproduced through the Makefile.

## Return the documents to local-only maintenance

After cloning the migrated repository, run the following from the repository
root. `--cached` removes paths from the Git index but leaves the local files on
disk:

```bash
git rm -r --cached notes AGENTS.md NEXT_STEP.md
```

Then add these entries to the clone's `.git/info/exclude`:

```text
/notes/
/AGENTS.md
/NEXT_STEP.md
```

Review the staged deletion, then commit and push it:

```bash
git diff --cached --stat
git commit -m "docs: return migration notes to local-only"
git push origin x86-64
```

The documents remain in that clone's working directory and the migration
snapshot remains recoverable from Git history. New clones made after the cleanup
commit will not receive the local-only documents automatically.
