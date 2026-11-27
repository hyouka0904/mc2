# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

MechCommander 2 source port: the original game code on a reimplemented GameOS platform layer (SDL3, SDL3 GPU / OpenGL, ENet). Linux, Windows (MSVC) and macOS (Apple Silicon). Full developer docs: `doc/DEVELOPMENT.md` (Chinese: `doc/DEVELOPMENT.zh-TW.md`). Player docs: `README.md`, `doc/manual/`. Multiplayer / update / localization roadmap: `doc/ROADMAP_MULTIPLAYER_ZH.md`.

See [AGENTS.md](AGENTS.md) for the feature map and targeted reading workflow.

## Working rules

- Reply to the user in Traditional Chinese.
- Always run the game under `timeout` (it can hang; test scripts have their own timeouts).
- Do not work on DXIL (D3D12 shaders) without the user's go-ahead.
- Keep `HANDOFF.md` up to date (goal, done, open items, decisions, test commands and results, next steps) after every significant stage, not only at the end.
- After a test pre-release is published (Release workflow run by hand), update the test-version download link at the top of `README.md` and `README.zh-TW.md` to the new tag without being asked. Do it only once the release exists.
- Roadmap work stops at each milestone in `doc/ROADMAP_MULTIPLAYER_ZH.md` until the user reports real-machine test results.

## Build

```bash
cmake -S . -B build                      # default RelWithDebInfo; SDL3 is downloaded and built statically if not installed
cmake --build build -j"$(nproc)"
```

Options: `-DCMAKE_BUILD_TYPE=Debug` (defines `_ARMOR`: asserts, and extra checks inside game headers), `-DMC2_ENABLE_ASAN=ON`, `-DMC2_VERSION=1.2.3` (otherwise from `git describe`), `-DMC2_SHADER_REQUIRE=msl,hlsl` (CI). Outputs: `build/mc2`, test binaries in `build/out/tests/`.

The game data lives in a separate repo; assemble a runnable game directory with:

```bash
git clone https://github.com/alariq/mc2srcdata.git
MC2_PLACEHOLDER_MOVIES=1 scripts/prepare_game_dir.sh build mc2srcdata game
cd game && timeout 120 ./mc2 -mission mc2_01
```

## Tests

```bash
ctest --test-dir build --output-on-failure          # all unit/tool tests
ctest --test-dir build -R unit_net --output-on-failure   # one test
```

Unit tests use the in-repo framework `tests/framework/mc2test.h` (`TEST(name)`, `CHECK`, `CHECK_EQ`) and are registered with `mc2_add_unit_test(...)` in `tests/CMakeLists.txt`. A test that compiles game sources may need stand-ins for `_ARMOR`-only symbols in Debug builds (see `tests/unit/savelayout_stubs.cpp`).

End-to-end tests run the real game headless (Xvfb, Mesa; they start Xvfb themselves) against a game directory:

```bash
scripts/smoke_test.sh game mc2_01 60
scripts/run_screenshot_tests.py game shots [--renderer gpu] [--update]   # golden images in tests/golden
scripts/run_flow_tests.py game --renderer gl [--only quick_save]          # scripted UI flows
timeout 900 python3 scripts/run_net_test.py game [--scenario version]     # two instances, LAN host + client
```

Deterministic mode (`-fixedstep <ms> -seed <n> -capture-frames ... -input-script <file> -windowed`) makes frames pixel-identical; `MC2_NO_DIALOGS=1` turns error dialogs into stderr output, `MC2_USER_DIR` gives a fresh user folder. Flow tests fail on `STOP`, `FATAL`, `ASSERT`, `MSGBOX` or `Segmentation` in the log. Release builds have no `SPEW` output; use `fprintf(stderr, ...)` for messages that tests check.

CI (`.github/workflows/ci.yml`): Linux RelWithDebInfo + Debug with all CTest tests, Windows and macOS unit tests, then smoke, screenshot (both backends), flow and LAN tests. Doc-only pushes skip CI. `release.yml` builds packages (tags `v*` create a draft release; `workflow_dispatch` with a `version` input builds test packages only).

## Architecture

Four layers, each depending only on those below:

- `code/` — game logic (missions, units, AI, logistics screens, multiplayer). Entry point and main loop: `mechcmd2.cpp`.
- `gui/` (widgets) and `mclib/` (engine: terrain, objects, A* pathfinding, ABL mission script interpreter, `.fst`/`.pak` archives; `mlr/` render pipeline, `gosfx/` effects, `stuff/` math/containers).
- `GameOS/` — the reimplemented platform layer: `gameos/` (window, input, audio, files, fonts on SDL3), `gameos/render/` (backend interface with `gl/` and `gpu/` implementations — OpenGL calls are allowed only in `render/gl/`, enforced by the `render_isolation` test), `gameos/net/gos_net.h` (the only ENet user), `gameos/update/` (update check), `gameos/mods/` (mod system), `src/` (Win32 API replacements).
- Third party in `thirdparty/` (ENet, pl_mpeg) plus SDL3 via `cmake/MC2Dependencies.cmake`.

Cross-cutting points:

- **Files**: `File::open()` prefers a loose file in the game directory, then the `.fst` archives. Settings and saves go to `SDL_GetPrefPath` (`~/.local/share/MechCommander2/` on Linux).
- **Shaders**: GLSL in `shaders/`, loaded at run time by the GL backend and cross-compiled at build time to SPIR-V / MSL / HLSL for SDL3 GPU. The `PerDraw` uniform block must match its C++ struct (`shader_spirv` test).
- **UI strings**: a new `IDS_*` string must be added to `resource.h`, `strings.res.h` and `test_scripts/res_conv/{resource.h,strings.res.h,strings.res.cpp}`; they build the `res/` string library.
- **LAN multiplayer**: host-authoritative star topology. Messages are fixed-size `MCMSG_*` structs in `code/multplyr.h`; `multplyr.cpp` handles rooms, roster, chat and the load handshake, `multplyr_sync.cpp` handles in-mission sync. Bump the protocol stamp (`MC2NET-4` in `multplyr.cpp`) whenever a message changes. Room discovery shares UDP 27015.
- **Saves**: campaign saves (`.fit`, text) must keep loading across versions: accepted range `OldestSaveGameVersionNumber..SaveGameVersionNumber` in `code/logisticsdata.cpp`. Quick saves (`.ims`) store structs byte for byte; the stamp carries a layout hash (`code/savelayout.cpp`). When a saved struct changes size, `unit_savelayout` fails on purpose: update `kCurrent` in the test, never the legacy table. `tests/saves/<version>/` holds old saves loaded by the flow tests.
- **Mods**: `GameOS/gameos/mods/`, tool `tools/mc2mod/`, examples in `examples/`; see `doc/modding.md`.

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
