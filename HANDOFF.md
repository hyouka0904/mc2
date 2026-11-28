# 交接資訊（HANDOFF）

> 只記下次接手需要的狀態。改動細節看 `git log`，設計和檔案位置看 [code-map](doc/code-map.md)，計畫看 [ROADMAP](doc/ROADMAP_MULTIPLAYER_ZH.md)。
> 最後更新：2026-11-28

## 目前狀態

- 已完成：
  - 移植與基礎：SDL3、GPU／GL 雙後端、三平台、Mod、影片、打包、存檔相容。
  - M1 區網聯機（N1–N9）。
  - U1–U3、測試版頻道、遊戲內下載安裝、區塊更新。
  - 繁中補丁：2,108 條譯文、遊戲內語言切換。
  - 大廳修正：整備流程、聊天、System 選單、準備與暫離。
- 最新測試版：[v0.1.1-test.42](https://github.com/hyouka0904/mc2/releases/tag/v0.1.1-test.42)（dev `1cf6f4f`）。
- 停在停止點 1＋U-a：等使用者實機回報。**收到回報前不開始 M2。**

## 待使用者實機確認

1. M1、N9、U1–U3：Windows＋Linux 兩台搜房、加入、對戰、版本提示。
2. 區塊更新：連發兩個測試版，第二次應只下載程式區塊；裝在 Program Files 時跳 UAC。
3. 大廳：
   - 房主從 System 選單離開後，客端的畫面。
   - 「暫離／我回來了」的中文寬度，長房名和 AWAY 按鈕會不會重疊。
   - 房主暫離時，客端看得到、房主不能開始。
   - 同隊時聊天出現 TEAM。
   - Windows 網卡名稱（中文 Windows）。
4. 戰役 System → 回到主選單 → 儲存，`Autosave.fit` 能否讀回。
5. 機甲顏色預覽（選項畫面和戰場）、繁中語言切換、小字清晰度。
6. 擱置：選項畫面預覽機甲「穿模」，使用者說先不處理。

## 未完成事項

- U4 minisign 簽章：需要使用者建立 secret `MINISIGN_KEY`，不急。
- 之後的里程碑：M2–M8、U5–U7、L4–L6，見 ROADMAP。

## 工作規則（使用者指定，補充 CLAUDE.md）

- 工作分支 `claude/admiring-maxwell-3xhqba`。推 `dev` 要先問（使用者說「要」才合併並發測試版）。不碰 master 和 tag，不開 PR。
- 原版行為優先；確認是 bug 才改。
- 沒有內容變動時，不做只改 HANDOFF 的 commit。

## 重要決策

- 聯機：主機權威星狀拓撲，協定 stamp 目前是 `MC2NET-4`，搜房共用 UDP 27015。房間 1–4 人，一人可開。
- 玩法（M5 之後）：
  - 「改裝」取代武器特性：每台機甲一個，收解鎖費＋安裝費，換掉不退。
  - 佔領用勝利條，指揮中心被毀判負。
  - M8 不提供影像，做之前先完成 A0。
  - 順序 M5 → M6 → M7 → M8。
- 更新：
  - 平台內建 HTTPS（WinHTTP；Linux、macOS 執行時才載入 libcurl）。開發版不查更新；`MC2_UPDATE_URL` 可以指向假伺服器。
  - 資料區塊分 textures、models、archives、sound、movies、misc（另有繁中區塊），內容相同時產出的位元組也相同。
- macOS CI 偶爾被取消：GitHub 排不到 Mac，不是程式問題。

## 測試指令

```bash
cmake -S . -B build && cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
scripts/run_screenshot_tests.py game shots [--renderer gpu]
scripts/run_flow_tests.py game --renderer gl
timeout 900 python3 scripts/run_net_test.py game [--scenario logistics|version|away]
```

- 測試前要把 `build/mc2` 和 `build/out/res/libmc2res_64.so` 複製到遊戲目錄，否則會跑到舊版本，或出現 missing string res。
- ASan：`-DCMAKE_BUILD_TYPE=Debug -DMC2_ENABLE_ASAN=ON`。`unit_stuff` 的洩漏報告來自原版記憶體池，可以忽略。

## 最近一次結果（2026-11-27，`1cf6f4f`）

- Release：
  - ctest 23/23、smoke。
  - 截圖 gl、gpu 各 18/18；流程 gl、gpu 各 12/12。
  - 聯機 battle、logistics、version、away 都通過。
- ASan：12 個流程和聯機 away、battle 都通過，沒有 ASan 錯誤。
- Release workflow run 42：三平台打包、安裝和區塊更新測試都通過，已發佈 test.42。

## 下一步

1. 等實機回報；有問題就先修。
2. 使用者確認後開始 M2，順序 S5 → S1 → S4 → S2 → S3 → S6（ROADMAP 第 4 節），做完再停下來等實測。
