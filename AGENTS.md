# Repository guidance

- Reply to the user in Traditional Chinese. Working rules and build/test commands: [CLAUDE.md](CLAUDE.md).
- Reading order:
  1. [HANDOFF.md](HANDOFF.md): current status and what is waiting.
  2. The matching entry in [the feature map](doc/code-map.md). Then verify symbols and callers with `rg`, and read the affected files and tests.
  3. Only when needed, one section of [the development guide](doc/DEVELOPMENT.md) or [the roadmap](doc/ROADMAP_MULTIPLAYER_ZH.md). List the headings with `grep -n '^#' <file>` first; do not read either file whole.
- Update the code-map entry when its locations or dependencies change. Follow the roadmap's real-machine acceptance gates.
