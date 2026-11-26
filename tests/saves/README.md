# Saves from released versions

`<version>/` holds saves written by that release, loaded by the flow tests
(`scripts/run_flow_tests.py`: `old_campaign_save`, `old_quick_save`,
`foreign_quick_save`) so later versions keep reading them.

- `After_Mission_mc2_01.fit`: campaign save after mission one (format 10004).
- `testgame.ims`: in-mission quick save of `-mission mc2_01`, Linux x86_64
  (the quick save layout is per platform).

0.1.0: written by the 0.1.0 save code (format unchanged up to the layout
fingerprint), with the `campaign` and `quick_save` flows.

See "Save game compatibility" in the [development guide](../../doc/DEVELOPMENT.md#save-game-compatibility).
