---
name: Vendor engine sources
overview: Replace the PixelRoot32 and APU git submodules with a source-only snapshot at the current pins, keep the third_party paths, and record that those trees may be patched.
todos:
  - id: prune-trees
    content: Keep include, src, LICENSE, and the engine TRADEMARK.md; delete the rest of both checkouts including .git
    status: completed
  - id: drop-submodules
    content: Remove gitlinks and .gitmodules, then add the remaining files as normal blobs
    status: completed
  - id: record-decision
    content: Update architecture.md, both cursor rules, README, CMake errors, flash.bat, and open_questions.md
    status: completed
isProject: false
---

# Vendor the engine as source

Copy the pinned trees into this repo and drop the submodule gitlinks. No engine behavior change. No commit unless you ask.

Pins already checked out:

- [third_party/pixelroot32](third_party/pixelroot32) tag `1.12.0`, commit `826be88110921b5fa1ad0952720f82d509373649`
- [third_party/pixelroot32-apu](third_party/pixelroot32-apu) tag `v2.0.0`, commit `b603b3a95412ec0bb0075d61dbbb6c8072ddc17c`

Both are MIT. `LICENSE` stays in each tree. The engine also keeps `TRADEMARK.md`. The APU tree has no trademark file.

## What stays

- `pixelroot32`: `include/`, `src/`, `LICENSE`, `TRADEMARK.md`
- `pixelroot32-apu`: `include/`, `src/`, `LICENSE` (the `.cpp` is not linked today; [cmake/pixelroot32.cmake](cmake/pixelroot32.cmake) only compiles `pixelroot32/src`, but the headers still include `ApuCore.h`)

Dropped from both checkouts: `.git`, `.github`, `.vscode`, `.codegraph`, docs, assets, examples, tests, scripts, PlatformIO, library manifests, and the engine README/changelog. Upstream history is not imported.

Paths stay. [main/CMakeLists.txt](main/CMakeLists.txt) and [pc/CMakeLists.txt](pc/CMakeLists.txt) keep the same include dirs. The missing-tree errors stop telling you to run `git submodule update --init`.

## Git

`git add` on a directory that still has a `.git` file recreates a submodule (mode `160000`). Order:

1. Delete everything in each tree except the files above, including the `.git` gitlink file.
2. `git rm --cached` both paths and remove [.gitmodules](.gitmodules).
3. `git add` the remaining files so they are normal blobs.
4. `git submodule deinit` and drop the local `.git/config` / `.git/modules` entries so this clone does not still track them.

## Rules and record

Same change updates the decision record.

- [architecture.md](architecture.md): layout notes the vendored pins. The language section stops saying the engine is unmodified. The integration section says the trees may be patched, and this vendor step does not change behavior. New decision-log row for 2026-10-16. Board pins and the panel stay in the drawer.
- [.cursor/rules/architecture.mdc](.cursor/rules/architecture.mdc): replace “do not patch `third_party/pixelroot32`” with permission to patch both vendored trees. New C++ we write stays in the host and the ST7789 drawer.
- [.cursor/rules/function-comments.mdc](.cursor/rules/function-comments.mdc): `third_party/` stays exempt from the Portuguese function comments, including when a function there is edited.
- [README.md](README.md) and [flash.bat](flash.bat): drop submodule init. If `Engine.h` or `ApuCore.h` is missing, `flash.bat` fails instead of fetching.
- [open_questions.md](open_questions.md): one new decision-log row so it does not still say the engine is a submodule.

Old files under `.cursor/plans/` stay as history.