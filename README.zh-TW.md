# MechCommander 2 開源移植版

[English](README.md)

## [⬇ 下載最新版](https://github.com/hyouka0904/mc2/releases/latest)

**[⬇ 下載測試版（v0.1.1-test.36）](https://github.com/hyouka0904/mc2/releases/tag/v0.1.1-test.36)**

**回到正式版。** 在「選項 → 遊戲玩法」取消勾選「接收測試版」，之後只會收到高於目前版本的正式版更新。若要立即回退，請先關閉遊戲並備份存檔與設定，再將[最新正式版](https://github.com/hyouka0904/mc2/releases/latest)安裝或解壓到另一個位置。兩版可能共用存檔資料夾，位置請參考安裝包內的 `README.txt`。回退後的存檔相容性尚未驗證。

舊版可在[所有版本](https://github.com/hyouka0904/mc2/releases)找到，支援的平台以該版本的 Assets 為準。目前正式版（v0.1.0）尚未內建更新功能。

在 Release 頁面下方的 Assets 選擇你的平台：

| 平台 | 檔案 |
|---|---|
| Windows 10 以上（x64） | `…-windows-x64-setup.exe`（安裝程式），或 `…-windows-x64.zip`（免安裝） |
| macOS 13 以上（Apple Silicon） | `…-macos-arm64.dmg` |
| Linux x86_64 | `…-linux-x86_64.AppImage`，或 `…-linux-x86_64.tar.gz` |

每個安裝包都附完整的遊戲資料（約 600 MB），下載後就能玩。安裝步驟、存檔位置和常用參數見安裝包裡的 `README.txt`。內建更新功能的版本會在遊戲啟動時檢查有沒有新版本（一天最多一次），有的話在主選單提示，點一下就能下載並安裝（遊戲會重新啟動，存檔保留）。Windows 版和 Linux 壓縮檔只下載有變動的部分，通常只有幾 MB。選項 > Gameplay 勾選「Receive test versions」，也會收到尚未正式發佈的測試版。加上 `-noupdatecheck` 就不檢查。

> **本專案是 [alariq/mc2](https://github.com/alariq/mc2) 的獨立 fork，與原作者 alariq 無關，也未經其認可；與微軟無關。**
> 問題請回報到本 repository，不要回報給原專案。

---

## 專案簡介

《MechCommander 2》是微軟 2001 年發行的即時戰術遊戲：你指揮一支機甲（BattleMech）小隊，在任務之間整備機甲、配置武器、指派駕駛員，在任務中下達移動與攻擊指令。

微軟以 Shared Source 授權公開了遊戲原始碼，但底層引擎 GameOS 只有標頭檔。原作者 alariq 用 SDL2 + OpenGL 重新實作了 GameOS；本 fork 再改用 SDL3，繪圖預設走 Vulkan／Metal，讓遊戲以 64 位元在 Windows、macOS、Linux 上執行，並修正了許多原版就有的錯誤。

- 單人戰役、單人任務、存讀檔、機甲整備都能正常遊玩。
- **區網聯機**：2–8 台電腦在同一區網對戰，Windows、macOS、Linux 可以互連。
- 支援 Mod 與 Modpack。
- 接下來的聯機模式（合作關卡、Roguelike、雙人對戰）正在開發中。

**[開發說明（建置、架構、測試、發佈）→](doc/DEVELOPMENT.zh-TW.md)**

[功能與檔案索引](doc/code-map.md)

---

## 聯機模式說明書

單人遊戲的玩法和原版相同；任務中按 **F1** 可以看完整的熱鍵清單。

| 模式 | 說明書 | 狀態 |
|---|---|---|
| 區網對戰（原版多人模式） | **[區網對戰說明書](doc/manual/lan-skirmish.zh-TW.md)** | 可以玩 |
| 合作關卡（攻堅、護送、回收、偵查） | 完成後提供 | 開發中 |
| Roguelike 攻堅／防守 | 完成後提供 | 規劃中 |
| 雙人對戰（佔領、殲滅） | 完成後提供 | 規劃中 |

---

## 其他

- [Mod / Modding / Modpack](doc/modding.zh-TW.md)：用 Mod 修改遊戲資料、製作工具 `mc2mod`。
- [聯機模式與中文補丁的工作清單](doc/ROADMAP_MULTIPLAYER_ZH.md)（只有中文）。
- [原作者網站](https://alariq.github.io/mc2-website/)。

### 與原專案的差異

本 fork 從原專案的 `7130807`（2022 年 4 月的 master）分出。以下列出本 fork 新增的內容，以及哪些修正來自原作者。

#### 本 fork 新增
- **繪圖**：SDL2 換成 SDL3（含 SDL3_mixer、SDL3_ttf）；新的 SDL3 GPU 後端為預設（Linux / Windows 用 Vulkan，macOS 用 Metal），OpenGL 改為備援；著色器在建置時編譯成 SPIR-V 與 MSL；批次繪製與貼圖圖集，第一關的繪製次數降到 100 次以下；TG 模型的光照移進著色器。
- **平台與發佈**：macOS 支援（`.app`）；三個平台的安裝包與 release 流程，CI 會實際安裝並啟動安裝包；存檔與設定移到系統的使用者資料夾並自動搬移舊資料；版本號與 `-version`；啟動時檢查磁碟空間；精簡 Win32 平台層。
- **聯機**：區網對戰（ENet、主機權威、房間探索、斷線處理、網路暫停）。
- **測試**：單元測試、無畫面的任務冒煙測試、固定步長的截圖比對測試、UI 流程測試、網路測試、效能量測腳本，以及 Linux / Windows / macOS 三平台 CI。
- **功能**：Mod / Modpack 系統與 `mc2mod` 工具；3D 音效 API；記憶體配置追蹤。
- **修正與整理**：能以 GCC 13 建置；編譯警告 643 → 0；A* 尋路錯誤與開放清單效能；RLE TGA 解碼；快速存讀檔、選項畫面、暫停選單；macOS 上 `#pragma pack(4)` 造成的結構排列錯誤；以 AddressSanitizer + UBSan 找到的記憶體錯誤與未定義行為；音效改以裝置取樣率混音並加上限幅器。細節見[開發說明](doc/DEVELOPMENT.zh-TW.md)。

#### 來自原作者的修正
下列修正出自原作者在本 fork 分出之後的上游修改，由本 fork 移植（括號內為原專案的 commit）：

- `RollDice()` 改用 15 位元的亂數，Linux 上的擲骰才會正確（`1c601fc`）
- 選項檔少了 `ResolutionY` 或 `AltitudeMaximumLo` 時，不再把另一個值也重設（`88412ec`、`85adda9`）
- 資訊視窗中編號大於 127 的零件不再顯示錯誤的縮寫（`3bfc4ae`）
- 設定熱鍵時按到已使用的按鍵不再陷入無窮迴圈（`8dd96f8`）
- 非 Windows 系統上，資料檔中的 `\` 路徑會轉成 `/`（`a22583e`、`73edc36`）
- alpha test 只捨棄完全透明的像素，與原版繪圖相同，指北針與小地圖外框因此恢復（`29b5a88`）

下列功能參考原作者的做法，在本 fork 的新架構上重新實作：影片播放（pl_mpeg）、`-datapath` 參數、只改變視窗高度時重新排版任務介面。

#### 原專案有、本 fork 沒有
- 硬體 T&L 繪圖路徑：本 fork 的 SDL3 GPU 後端採用不同做法，不適用。
- Windows 預編譯第三方套件與建置教學：本 fork 改用 vcpkg，見[在 Windows 上建置](doc/DEVELOPMENT.zh-TW.md#在-windows-上建置)。
- 多螢幕視窗處理（displayIndex）。

### 授權

- 原始遊戲程式碼以 Shared Source Limited Permission License 釋出，請見 `EULA.txt`。
- 原作者 alariq 撰寫的 GameOS 重新實作以 GPL v3 授權，請見 `license.txt`；本 fork 的修改同樣以 GPL v3 授權。
- 本移植新增的檔案在開頭標示 `SPDX-License-Identifier: GPL-3.0-only`；沒有這行的檔案沿用上面兩條的說明。
- GPL v3 與 Shared Source 授權第 (E) 條（原始碼散布只能沿用該授權）能否並存並不明確；公開或商業散布前請先諮詢法律意見。遊戲美術素材只能非商業使用。

> 原作者的話：因烏克蘭戰爭，作者目前無暇維護本專案，歡迎社群協助。
