# 交接資訊（HANDOFF）

> 每完成一個重要階段就更新這份文件，不等到任務結束。
> 最後更新：2026-11-27（遊戲內語言切換測試版發佈）

## 任務目標

MechCommander 2 原始碼移植（SDL3、SDL3 GPU／OpenGL、ENet），支援 Linux、Windows（MSVC）、macOS（Apple Silicon）。目前在做 `doc/ROADMAP_MULTIPLAYER_ZH.md` 的聯機路線圖，分成三條線：

- **M 線（聯機）**：M1 區網聯機 → M2 共通系統 → M3 合作關卡 → M4 地圖生成 → M5 合作任務擴充 → M6 Roguelike → M7 雙人對戰 → M8 機器學習接口。
- **U 線（更新）**：版本檢查和自動更新。
- **L 線（中文化）**：繁體中文補丁。

詳細計畫在 `doc/ROADMAP_MULTIPLAYER_ZH.md`。

### 工作規則（使用者指定）

- **溝通**
  - 用繁體中文回覆。
  - 給測試步驟時要附完整指令。
  - 每個里程碑結束就停下來，等使用者回報實機測試結果。
- **Git**
  - 路線圖工作分支為 `claude/admiring-maxwell-3xhqba`。臨時修正由獨立分支通過 CI 後整合至 `dev`。master 和 tag 由使用者處理。
  - 不開 PR，除非使用者要求。
  - commit 用 Conventional Commits（見 `CLAUDE.md`），不要寫進模型名稱。
- **執行與範圍**
  - 遊戲一律在 `timeout` 下執行。
  - 沒有使用者同意，不做 DXIL（D3D12 shader）。

## 繁中補丁預覽

### 遊戲內語言切換

- 目標：在設定的遊戲玩法切換英文和繁中，套用後重新啟動生效。
- 實作：語言獨立儲存在使用者資料目錄的 `language.cfg`，載入時同時選擇譯文和字型。尚未儲存選擇時支援 `MC2_LANGUAGE`。只有具備繁中資源的版本會顯示此選項。
- README：兩份 README 都直達目前已發布的 `v0.1.1-test.36`。之後每次發佈 `dev` 測試版時一起更新版本號與連結。
- 本機驗證：Release 建置後 `ctest --test-dir /workspace/scratch/mc2-zh-build --output-on-failure` 通過 23 項。Xvfb 的 OpenGL 及 SDL3 GPU 遊戲流程驗證取消不儲存、英文切到繁中、重啟載入繁中、再切回英文及重啟，並比對主選單截圖。資源函式庫逐條比對 2,108 條譯文，驗證明確選擇英文及既有環境變數。
- 整合驗證：功能提交 `2f138ae` 的 CI 45949062193 全數通過，包含 Windows、Linux、macOS 和畫面、流程、區網測試。Release 45949062200 的三平台安裝包驗證及 Windows、Linux 區塊更新檢查通過。
- 已發佈：`dev` 的 `9a29982` 已上架為 [v0.1.1-test.36](https://github.com/hyouka0904/mc2/releases/tag/v0.1.1-test.36)。CI 45951131773 和 Release 45951945884 全數通過。確認五個安裝包已上傳。以 `gh release download` 下載並核對繁中區塊、Windows 與 Linux 更新清單的 SHA-256，確認有 2,108 條譯文與 12 套字型。
- 下一步：Windows 實機驗收語言切換，M1、U1–U3 驗收仍待使用者回報。

### 測試版整合

- 目標：讓 `dev` 測試版隨附繁中字型和譯文。Release 在 Linux 產生資源並提供給其他平台，Windows 和 Linux 的區塊更新將繁中資源放在獨立區塊。
- 驗證：文字補譯送合併前，CI 45922319707 的 Windows、Linux、macOS、兩種渲染後端的畫面與流程、區網聯機均通過。Release 流程新增安裝內容及區塊更新驗證。首次發佈（45925115516）Linux、macOS 及 Windows 安裝測試通過，Windows 更新驗證缺少由 Linux 產出的繁中區塊 ZIP。共用產物現在會傳遞該 ZIP，Windows 測試結束後移除副本，發佈時仍以 Linux 產出的區塊為準。
- 已發佈：`dev` 的 `d85198e` 已發佈為 [v0.1.1-test.33](https://github.com/hyouka0904/mc2/releases/tag/v0.1.1-test.33)。CI 45927044447 全部通過。Release 45927045219 通過 Windows、Linux、macOS 安裝包檢查，Windows 和 Linux 均驗證可透過區塊更新補回譯文。已確認五個安裝包及 2.5 MB 繁中更新區塊均已上傳。
- 下一步：用測試版驗收繁中介面，預設使用英文，設定 `MC2_LANGUAGE=zh-TW` 可啟用繁中。Windows 實機和 M1、U1–U3 驗收仍待使用者回報。

### 文字補譯：百科、戰場與多人地圖

- 已完成：百科人物、機甲、載具、建築、歷史及裝備描述，並補譯戰場物件、裝備名稱與縮寫、操作提示、勳章、技能、多人圖名及原版「2of3」顯示文字。核對原版任務 FIT 內的簡報及目標資源 ID，補上第 21 關的工業園區次要目標。
- 版面：戰役簡報與百科正文預留文字縮排和捲軸的空間。保留機甲與載具型號、駕駛員呼號、存檔與地圖檔名、連線預設名稱。原版圖片維持原樣。
- 驗證：`python3 scripts/build_zh_tw_patch.py /workspace/scratch/mc2-zh-hud-game /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc --face-index 3` 產生 2,104 條譯文、1,604 個擴充字形及 12 套字型。直接呼叫資源函式庫逐條比對 2,104 項，並驗證重載後的原版語言。`ctest --test-dir /workspace/scratch/mc2-zh-build --output-on-failure` 23 項通過。Linux OpenGL 的 `run_flow_tests.py --only options,campaign,encyclopedia,load_game,solo_mission,pause_menu` 六條流程通過。預設英文的主選單、戰役選擇、第一關三張基準比對均通過。截圖已確認第二關簡報換行與多人地圖清單顯示「三取二」。
- 尚待：Windows 實機及更多解析度校對。M1、U1–U3 的實機待測狀態與 M2 排程沿用原規劃。

### 文字補譯：關卡與百科首批

- 目標：補齊可由文字資源或資料檔翻譯的遊戲內容。原版圖片維持原樣。
- 已完成：新增 245 條譯文，包含 24 關戰役、5 關訓練及 18 條多人簡報的其餘內文，159 條對應目標，以及百科歷史與 30 條載具介紹。
- 驗證：`python3 scripts/build_zh_tw_patch.py /workspace/scratch/mc2-zh-hud-game /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc --face-index 3` 產生 1,132 條譯文、1,296 個擴充字形、12 套字型。Linux 的 `MC2_LANGUAGE=zh-TW timeout 340 python3 scripts/run_flow_tests.py /workspace/scratch/mc2-zh-hud-game --out /workspace/scratch/mc2-zh-text-milestone --renderer gl --only campaign,encyclopedia --timeout 155` 通過兩個流程。第二關簡報顯示繁中，右緣有文字裁切需要追查。
- 下一步：補齊人物、機甲、裝備、建築與剩餘提示；盤點原版資料檔的顯示文字及修正長文裁切。補譯尚在進行，Windows 實機驗收待測。

### 繁中介面首版

- 目標：在同一分支先完成主要遊戲介面譯文，使用 Linux 截圖檢查版面。中文輸入法留待後續。
- 已完成：涵蓋設定與快捷鍵、存讀檔、戰前部署、戰鬥與支援面板、結算、百科介面、多人房間與更新提示。加入全部戰役和訓練關卡的標題及概要，保留原有英文備援。
- 決策：保留存檔檔名樣式、預設連線名稱與機甲型號。產生工具檢查格式參數並按需要放寬單頁字型貼圖寬度。共用字型元件將已格式化的文字視為字面值，以正確顯示百分號。
- 補丁產生：`python3 scripts/build_zh_tw_patch.py game /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc --face-index 3` 產生 887 條譯文、796 個擴充字形、12 套字型。直接載入資源函式庫逐筆比對 887 條譯文，並驗證存檔名稱及預設英文。
- Linux Release 的 `ctest --test-dir /workspace/scratch/mc2-zh-build --output-on-failure` 通過 23 項。英文 `run_screenshot_tests.py` 的主選單、戰役與第一關三張比對均通過。以 `MC2_LANGUAGE=zh-TW` 執行 `run_flow_tests.py --renderer gl`，設定、戰役、存讀檔、單人任務、百科、暫停與快速存讀檔流程通過。800×600 的繁中截圖另核對戰鬥操作提示、資源點、連線方式及多人任務設定。
- 區網測試：標準 `run_net_test.py` 在設定的 280 秒時限內完成 89 秒同步比對，主機未在時限內退出。以同一驗證器縮短主客端幀數後重跑通過，兩端均正常退出，共比對 90 秒、8 個單位遭擊破，存活單位最大位置差 41，最終擊破與損失一致。
- 待辦：完整關卡簡報和目標（第一關已譯）、百科內文、未涵蓋的裝備與原版資料檔或貼圖文字、Windows 實機與其他解析度。M1、U1–U3 實機驗收及 M2 狀態維持原排程。

### 初始預覽

- 目標：在 `feat/zh-tw-localization`（起點 `dev` 提交 `9ec2c9d`）驗證主選單與第一關的中文字形、斷行、任務文字與英文備援。
- 已完成：UTF-8 繪製與量測、基本中文標點禁則、相容舊字型的稀疏字形表、外部翻譯覆寫，以及 30 條試譯。預覽以 `MC2_LANGUAGE=zh-TW` 啟用，翻譯與疊加字型置於遊戲資料夾的 `localization/zh-TW/`。
- 補丁產生已確認：`python3 scripts/build_zh_tw_patch.py /workspace/scratch/mc2-zh-game /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc --face-index 3` 產生 12 套字型，每套涵蓋試譯用的 201 字。
- Linux Release 的 `ctest --test-dir /workspace/scratch/mc2-zh-build --output-on-failure` 共 23 項通過。`python3 scripts/run_screenshot_tests.py game shots --renderer gl --only main_menu campaign_select mc2_01_0300` 三張預設英文截圖均符合基準。
- 以 `MC2_LANGUAGE=zh-TW timeout 285 python3 scripts/run_flow_tests.py game --renderer gl --only campaign,solo_mission --timeout 135` 驗證戰役及單人任務流程，並檢閱主選單與第一關簡報截圖。以資源 DLL 直接確認英文備援與重新啟動後的語言選擇。
- 待辦：Windows 實機、其他解析度與全量翻譯。字型多頁、遊戲內語言設定、輸入法支援仍待後續。M1 與 U1–U3 維持實機待測，M2 保持待辦。

## 開發文件整理

- 工作分支：`docs/development-map`。開發指南、聯機路線圖、玩家手冊、Mod 文件及發佈說明集中到 `doc/`。根目錄 README 保留中英入口，`HANDOFF.md` 保留原路徑。
- 新增 [功能與檔案索引](doc/code-map.md) 和 [AGENTS.md](AGENTS.md)。先查相關條目，再用 `rg -n` 核對入口、共用狀態、連動檔案與測試。
- 已驗證：Markdown 相對連結與標題錨點檢查通過，`git diff --check HEAD` 通過。發佈流程和 CI 已同步改用 `doc/` 的路徑。
- 分支提交 `0be7619` 通過 Windows、Linux Debug／RelWithDebInfo 及 Linux 遊戲端到端測試（雙渲染後端、UI、區網與版本相容性）。macOS 排隊後取消，沒有開始執行。依既有條件整合至 `dev`。[CI 執行紀錄](https://github.com/hyouka0904/mc2/actions/runs/45857701890)
- 發佈流程的 Windows 與 Linux 打包檢查通過，macOS 尚無結果。[打包執行紀錄](https://github.com/hyouka0904/mc2/actions/runs/45857485973)
- 下一步：M1、U1–U3 仍待實機回報，M2 保持待辦，平行開發擱置。

## 測試版下載入口

- 中英 README 在正式版下載下方新增測試版入口，指向 GitHub Releases，以便選取最新的 Pre-release。已核對目前正式版 v0.1.0 尚無遊戲內更新功能，v0.1.1-test.24 提供 Windows 和 Linux 套件。
- 以 `git merge-base --is-ancestor` 確認修正與文件分支的提交均已整合至 `dev`。M1、U1–U3 仍待實機回報。

## 臨時修正

- 目標：修正關卡 MOD 動態生成與移除、Resource Points 顯示、維修車遇到卡模時的指令延續。
- 工作分支：`fix/mc2-mission-regressions`，從 `dev` 分出。兩者起點為已測的 Claude 分支 `bf8494e`。
- 里程碑維持 M1、U1–U3 的實機待測。這次插入修正，M2 保持待辦。
- 合併條件：同一修復版本的 Windows 與 Linux CI 均須通過。macOS 如因 runner 不可用而無結果，可略過。
- 本機確認：Release 建置、MOD／ENet／更新相關 4 組 CTest、`mc2_01` ABL 連續 16 波共 512 次建立與清場，以及 `objectsuicide` 後重啟用通過。預置單位連續四波移除後，檔案描述符依序從 97 降到 85、73、61、49。
- 資源點以主機同步的餘額更新 HUD，扣款與取消退款共用既有的資源差額訊息。地圖缺少起始值時以零顯示。800×600 實測完整標籤會裁掉數字，寬度不足時改用 RP 標籤。單人 HUD 已確認完整顯示 22000。雙實例測試確認客戶端顯示 3000，扣除探測器費用變成 1000，取消後回到 3000。
- 回歸確認：維修指令跨越避障移動保留，超出維修距離時回到接近階段。同座標部署及遠距離接近兩個本機關卡測試都成功修回裝甲。不同平台的實機操作仍待驗證。
- 修復分支 `8940d6c` 的 CI 五項全過，涵蓋 Windows、macOS、Linux Debug／RelWithDebInfo 與 Linux 遊戲端到端。聯機、版本提示、兩個渲染後端的畫面和 UI 流程均通過。[CI 執行紀錄](https://github.com/hyouka0904/mc2/actions/runs/45847432179)
- 測試命令
  - `cmake --build build --target mc2 unit_net unit_update unit_mods`
  - `ctest --test-dir build -R '^(unit_net|unit_update|unit_mods|tool_mc2mod)$' --output-on-failure`
  - `timeout 900 python3 scripts/run_net_test.py game --out net --timeout 900`（CI 內執行同一聯機測試）
- 下一步：在 `dev` 整合版驗證 M1、U1–U3 的 Windows／Linux 跨實體裝置操作，並回報原地圖的維修脫困情境。保持 M2 待辦。

## 已完成事項

### 移植與基礎（早期）

- 平台與渲染：
  - SDL2 換成 SDL3。
  - 新增渲染後端抽象層，有 SDL3 GPU 和 OpenGL 兩個後端。
  - 支援 macOS 和 Windows，三平台都有 CI。
- 遊戲內容與修正：
  - Mod 系統。
  - 影片播放（pl_mpeg）。
  - 修好音效爆音。
- 版本與發佈：
  - 版本號系統。
  - 打包腳本 `scripts/package.py`，加上 `release.yml`。
- 測試：
  - 截圖比對測試（`tests/golden`）。
  - 流程測試（`scripts/run_flow_tests.py`）。
- 存檔安全：
  - 快速存檔記錄結構佈局的指紋（`code/savelayout.cpp`）。
  - 戰役存檔讀取時能容忍格式差異。
  - CI 會讀舊版存檔（`tests/saves/`）。

### M1 區網聯機（N1–N9）

- 傳輸層用 ENet（`GameOS/gameos/net/gos_net.*`）。
- 房間探索、大廳、遊戲內同步、指令歸屬、斷線處理、暫停、結算都已完成。
- `scripts/run_net_test.py` 會開兩個遊戲實例，一台開房、一台加入來測試。
- N9：
  - 畫面上顯示本機 IP。
  - 有多張網卡時，對每張網卡的子網廣播位址送出搜尋。

### U 線（使用者要求提前做）

- **U1–U3**：
  - 啟動時查詢 GitHub 最新版（`GameOS/gameos/update/`），24 小時查一次，結果記在 `update.ini`。
  - 主選單顯示新版通知。
  - 房間列表顯示版本，版本不符時提示。
- **測試版頻道**：
  - 選項「RECEIVE TEST VERSIONS」打開後，遊戲也會收到預發布版。
  - 在 GitHub 上手動按 Run workflow 會發布預發布版 `v<next>-test.<run>`。
- **遊戲內下載安裝**：依安裝方式下載對應的安裝包，用 SHA256SUMS 驗證，裝好後自動重開。
- **區塊更新**（commit b312106）：
  - 安裝包拆成兩種區塊：
    - 每個平台一個程式區塊。
    - 六個資料區塊，所有平台共用。
  - 另附一份 manifest，列出每個區塊、每個檔案和它的 SHA-256。
  - 遊戲只下載內容有變的區塊，也只替換有變的檔案；要嘛全部換成功，要嘛完全不動。
  - Windows 遊戲資料夾沒有寫入權限時（裝在 Program Files），會用管理員權限執行 `mc2.exe -apply-update <plan>`，跳一次 UAC，並同步更新「已安裝的應用程式」裡的版本號。
  - AppImage、macOS，以及沒有 manifest 的 release，仍然下載整包。
- **資料建置可重現**（commit 6dc5355）：
  - tgl.fst 原本有沒清零的欄位，現已改掉。
  - 兩次乾淨建置產出的 571 個檔案完全相同，所以資料區塊在不同版本之間不會變。
- **Linux 打包**：會 strip 掉除錯資訊，mc2 從 67 MB 降到 9 MB。

## 未完成事項

1. **等使用者實測（最優先）**：M1、N9、U1–U3 在 Windows 和 Linux 實機上的測試結果。**收到回報前不開始 M2。**
2. **等使用者實測區塊更新**：
   - 先按兩次 Run workflow，產生兩個測試版。
   - 第二次更新應該只下載程式區塊：Linux 約幾 MB，Windows 是 mc2.exe 加上 DLL。
   - 裝在 Program Files 時會跳 UAC。
3. **macOS CI 待確認**：
   - commit 67c5a31 修了 `unit_update` 在 macOS 上的 SIGPIPE 問題。
   - 但 macOS 的 job 連續幾次 15 分鐘內排不到機器、被取消，所以這個修正還沒在 macOS 上跑過。
   - 需要按 Re-run failed jobs：https://github.com/hyouka0904/mc2/actions/runs/45632610803
4. **U4 minisign 簽章**：要使用者建立 secret `MINISIGN_KEY`（建議同時開 2FA）。不急。
5. **之後的里程碑**：M2–M8、U5–U7、L 線，見 ROADMAP 和下面的「下一步」。

## 重要決策

- **聯機設計**：
  - 主機權威的星狀拓撲。
  - 訊息改動時要更新協定標記 `MC2NET-1`。
  - 搜尋房間共用 UDP 27015。
- **房間人數**：1–4 人，一人也能開。
- **玩法設計**：
  - 「改裝」取代原本的武器特性：每台機甲只能裝一個；要付解鎖費和安裝費，換掉時安裝費不退。
  - M7 佔領模式用勝利條；指揮中心被毀直接判負。
  - M8 不提供畫面影像；做 M8 之前要先完成 A0（模擬和畫面幀率脫鉤）。
  - 里程碑順序 M5 → M6 → M7 → M8。
- **更新機制**：
  - 用平台內建的 HTTPS：Windows 用 WinHTTP，Linux 和 macOS 執行時才載入 libcurl。
  - 開發版不會查詢更新。
  - `MC2_UPDATE_URL` 可指向本機假伺服器做測試。
- **區塊切法**：
  - 資料區塊：textures、models（tgl.fst）、archives（其他 .fst）、sound、movies、misc。
  - zip 裡的檔案日期固定、排序固定，所以同樣內容產出的區塊位元組完全相同。
- **簽章金鑰**：只有負責發佈的機器需要，或放在 GitHub Secrets；多台開發機不必各放一份。
- **macOS CI 被取消**：是 GitHub 那邊 Mac 機器不夠用，不是程式問題。

## 測試指令與結果

```bash
cmake -S . -B build && cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
MC2_PLACEHOLDER_MOVIES=1 scripts/prepare_game_dir.sh build mc2srcdata game
scripts/run_screenshot_tests.py game shots
scripts/run_flow_tests.py game --renderer gl
timeout 900 python3 scripts/run_net_test.py game
```

最近一次結果（2026-11-25，commit b312106 和 67c5a31）：

- **本機**：
  - ctest 23/23 通過。
  - 截圖測試 18/18 和 golden 一致。
  - 流程測試（gl）全部通過。
  - 區塊更新端對端測試：用本機伺服器發 0.2.0-test.1 → test.2，只下載 1/7 個區塊（28.8 MB，未 strip），重開後版本正確。
- **CI（67c5a31）**：
  - Linux（Debug 和 RelWithDebInfo）、Windows、截圖、流程、聯機測試都通過。
  - macOS 排不到機器，被取消。
- **Release workflow（b312106）**：
  - Linux：「Test a part update」通過。
  - Windows：「Test a part update in Program Files」通過，包含提權套用、正在執行的 exe 改名、登錄檔版本號、最後解除安裝。
  - macOS：dmg 打包通過。

## 下一步

1. 等使用者回報 M1、U1–U3 和區塊更新的實機測試；有問題就先修。
2. macOS CI 有排到機器時，確認 `unit_update` 通過。
3. 使用者確認 M1 後開始 **M2**，順序 S5 → S1 → S4 → S2 → S3 → S6（詳見 ROADMAP 第 4 節）。M2 做完後同樣停下來等實測。
