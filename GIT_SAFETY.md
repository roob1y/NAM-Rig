# Git safety checklist (NAM Rig)

Written after the drive-rack tangle. The whole mess came from **one thing:
conflict markers got committed.** These habits catch it every time.

## Before every commit — the golden command

```
git diff --check
```

Git's built-in conflict-marker + whitespace detector. Prints nothing = safe.
Prints anything = you still have `<<<<<<<` / `=======` / `>>>>>>>` somewhere —
do NOT commit until it's silent.

## After every merge / stash pop / rebase / cherry-pick

Run `git status` and read the words:

- **"Unmerged paths"** or **"both modified"** → not finished. Fix those files,
  then `git add` them. Never `git add`/`commit` while these show.
- **"Automatic merge failed"** / **"fix conflicts and then commit"** → stop,
  open the named files, search for `<<<<<<<`, resolve, `git add`.
- A conflicting **`git stash pop`** is the sneaky one — it leaves markers but
  doesn't always nag. Always `git diff --check` after a pop.

## Merge direction (the other thing that bit us)

Check out the branch that should **receive** the changes, then merge the
**source** in:

```
git checkout drive-rack      # branch receiving the changes
git merge mod-rework         # source being pulled in
```

"On drive-rack, merge mod-rework" puts mod work INTO drive-rack. Doing it
backwards drags drive work onto the mod branch.

## Two structural habits

- **Commit small and often.** Committed work is safe; you can undo a commit,
  but not work you never saved. The original loss was a big uncommitted chunk
  swept into a stash.
- **Resolve by source of truth.** For the drive/mod merge: Drive code comes from
  drive-rack, mod code (`src/rig/ModBlock.h`) comes from mod-rework.

## Tests (local, Windows build)

```
build-clang\mod_test_artefacts\Release\mod_test.exe
build-clang\drive_test_artefacts\Release\drive_test.exe
```

Both must pass before the merge is considered done.
