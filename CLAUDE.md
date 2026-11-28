# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

MechCommander 2 source port: original game code on a reimplemented GameOS layer (SDL3, SDL3 GPU / OpenGL, ENet). Linux, Windows (MSVC), macOS (Apple Silicon). Start with [AGENTS.md](AGENTS.md).

## Working rules

- Reply to the user in Traditional Chinese.
- Always run the game under `timeout` (it can hang; test scripts have their own timeouts).
- Do not work on DXIL (D3D12 shaders) without the user's go-ahead.
- Keep `HANDOFF.md` short and current: status, items awaiting real-machine tests, open items, decisions, latest test results. Change details belong in commits, design notes in `doc/code-map.md`. No HANDOFF-only commit when nothing changed.
- Push only to the assigned `claude/...` branch. Merging into `dev` needs the user's OK each time; never push `master` or tags; no PR unless asked.
- Prefer the original game's behaviour unless something is a confirmed bug.
- After a test pre-release is published (Release workflow run by hand), update the test-version download link at the top of `README.md` and `README.zh-TW.md` to the new tag without being asked. Do it only once the release exists.
- Roadmap work stops at each milestone in `doc/ROADMAP_MULTIPLAYER_ZH.md` until the user reports real-machine test results.
- Save tokens: read `doc/DEVELOPMENT.md` and the roadmap by section (`grep -n '^#' <file>`, then read the range), not whole.

## Build and test

```bash
cmake -S . -B build && cmake --build build -j"$(nproc)"   # SDL3 is fetched if not installed
ctest --test-dir build --output-on-failure
MC2_PLACEHOLDER_MOVIES=1 scripts/prepare_game_dir.sh build mc2srcdata game   # data: github.com/alariq/mc2srcdata
scripts/run_screenshot_tests.py game shots [--renderer gpu] [--update]
scripts/run_flow_tests.py game --renderer gl [--only <flow>]
timeout 900 python3 scripts/run_net_test.py game [--scenario logistics|version|away]
```

- Copy a fresh `build/mc2` and `build/out/res/libmc2res_64.so` into the game directory before end-to-end tests.
- `MC2_NO_DIALOGS=1` turns dialogs into stderr; `MC2_USER_DIR` gives a fresh user folder. Flow tests fail on `STOP`, `FATAL`, `ASSERT`, `MSGBOX`, `Segmentation` in the log. Release builds have no `SPEW`; use `fprintf(stderr, ...)` for messages tests check.
- Unit tests use `tests/framework/mc2test.h` and `mc2_add_unit_test(...)`. Debug builds define `_ARMOR`.
- Build options, deterministic mode, CI and packaging: `doc/DEVELOPMENT.md` ("Automated tests", "Packages and releases").

## Architecture

`code/` game logic (entry `mechcmd2.cpp`) → `gui/`, `mclib/` (engine, `mlr/`, `gosfx/`, `stuff/`) → `GameOS/` (SDL3 platform layer; `render/gl/` is the only place OpenGL calls are allowed; `net/gos_net.h` is the only ENet user; `update/`, `mods/`). Feature → file map: `doc/code-map.md`.

- **Files**: loose files in the game directory win over `.fst` archives. Settings and saves go to `SDL_GetPrefPath`.
- **Shaders**: GLSL in `shaders/`, cross-compiled at build time for SDL3 GPU. `PerDraw` must match its C++ struct (`shader_spirv` test).
- **UI strings**: a new `IDS_*` goes into `res/strings/strings.res.h` (number), `res/strings/strings.res.cpp` (English text, latin-1) and `localization/zh-TW/strings.txt`. `res/strings/original/` is the frozen upstream `.rc` source; do not edit or rerun it.
- **LAN multiplayer**: host-authoritative star. Fixed-size `MCMSG_*` structs in `code/multplyr.h`. Non-MCMSG wire structs live in `code/multplyr_wire.h` with the protocol stamp (`MC2NET-4`); bump it whenever a message or a synced struct changes. `unit_netlayout` fails when a size changes: bump the stamp, then update `kStamp` and `kCurrent` in its test (Windows value from the CI log). UDP 27015.
- **Saves**: campaign `.fit` must keep loading (`OldestSaveGameVersionNumber..SaveGameVersionNumber`). Quick saves carry a layout hash; when a saved struct changes size, update `kCurrent` in `unit_savelayout`, never the legacy table.
- **CRLF files**: many sources are CRLF; keep line endings when editing.

## Git Commit Guidelines

### Format
Follow Conventional Commits 1.0.0: `<type>(<scope>): <description>` (scope optional).

Types: feat, fix, docs, style, refactor, perf, test, build, ci, chore, revert.

### Message rules
- Write in English, imperative mood, lowercase type and scope.
- Subject ≤ 72 chars, no trailing period.
- Describe the actual change, not the task request. Avoid vague subjects like "update" or "fix bug".
- Add a body when the motivation or details are not obvious. Separate subject, body, and footer with blank lines.
- Mark breaking changes with `!` and a `BREAKING CHANGE:` footer.
- Never fabricate issue references or test results.

### Workflow
- Check `git status` and `git diff` before staging; review staged changes before committing.
- One logical change per commit; split unrelated changes.
- Never commit unrelated user modifications, secrets, credentials, or temp files.
- Run relevant tests before committing. Never bypass Git hooks.
- Never amend, rebase, force-push, or push to a remote without explicit permission. Exception: commits may be pushed directly to the session's assigned work branch (`claude/...`) without asking; never push to `master` or tags.
