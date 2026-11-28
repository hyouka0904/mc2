# 開發說明

[English](DEVELOPMENT.md) ｜ [回到 README](../README.zh-TW.md)

給想自己編譯、修改程式或參與開發的人。只想玩遊戲的話，到 [README](../README.zh-TW.md) 下載安裝包即可。

## 目錄

1. [專案架構](#專案架構)
2. [在 Linux 上建置](#在-linux-上建置)
3. [建置遊戲資料並執行](#建置遊戲資料並執行)
4. [區網聯機的實作](#區網聯機的實作)
5. [版本檢查](#版本檢查)／[存檔相容性](#存檔相容性)
6. [自動化測試](#自動化測試)
7. [在 Windows 上建置](#在-windows-上建置)／[在 macOS 上建置](#在-macos-上建置)
8. [安裝包與發佈](#安裝包與發佈)
9. [已知限制與待辦事項](#已知限制與待辦事項)

- Mod 系統的格式、工具與程式介面：[doc/modding.zh-TW.md](modding.zh-TW.md)
- 聯機模式（合作、Roguelike、雙人對戰、機器學習接口）與中文補丁的工作清單：[ROADMAP_MULTIPLAYER_ZH.md](ROADMAP_MULTIPLAYER_ZH.md)

---

## 專案架構

### 分層結構

程式由上而下分為四層，上層只依賴下層：

```
┌──────────────────────────────────────────────────────────────┐
│ code/      遊戲邏輯：任務、機甲與載具、AI、戰鬥、後勤畫面、UI     │
├──────────────────────────────────────────────────────────────┤
│ gui/       UI 元件（按鈕、清單、字型、動畫）                      │
│ mclib/     遊戲引擎：地形、物件、路徑搜尋、ABL 腳本、貼圖管理       │
│   ├ mlr/     繪圖管線（MLR, 3D 模型與頂點池）                     │
│   ├ gosfx/   特效系統（粒子、卡片、形狀特效）                     │
│   └ stuff/   數學與容器函式庫（向量、矩陣、四元數、鏈結串列、串流）  │
├──────────────────────────────────────────────────────────────┤
│ GameOS/    平台抽象層（本專案重新實作的部分）                     │
│   ├ include/   原始 GameOS 介面 + Win32 型別定義                 │
│   ├ gameos/    以 SDL3 / SDL3_mixer 實作視窗、輸入、音效、檔案、字型；  │
│   │   └ render/  繪圖後端介面（Device / Shader / PipelineState）；     │
│   │       ├ gl/  OpenGL 實作（全專案唯一呼叫 OpenGL 的地方）        │
│   │       └ gpu/ SDL3 GPU 實作（Vulkan / D3D12 / Metal）            │
│   │            utils/ 為影像、矩陣、截圖、輸入腳本等工具             │
│   └ src/       Win32 API 的替代實作（字串、檔案、訊息框、時間…）    │
├──────────────────────────────────────────────────────────────┤
│ 第三方：SDL3（含 mixer、ttf）、GLEW、zlib、pl_mpeg、ENet     │
└──────────────────────────────────────────────────────────────┘
```

### 目錄說明

| 目錄 / 檔案 | 說明 |
|---|---|
| `code/` | 遊戲主程式（約 200 個檔案）。入口在 `mechcmd2.cpp`，負責初始化、命令列解析與主迴圈。 |
| `mclib/` | 遊戲引擎核心。例如 `move.cpp`（A* 路徑搜尋）、`pqueue.cpp`（路徑搜尋用的優先佇列）、`txmmgr.cpp`（貼圖與繪製批次管理）、`abl*.cpp`（ABL 任務腳本直譯器）、`ffile.cpp` / `packet.cpp`（`.fst` / `.pak` 資料包格式）。 |
| `gui/` | 介面元件庫。 |
| `GameOS/` | 平台層，見上方分層圖。 |
| `shaders/` | GLSL 著色器。OpenGL 後端執行時從遊戲目錄的 `shaders/` 載入；建置時另外編成 SPIR-V / MSL / HLSL（見「著色器」）。 |
| `res/` | 字串資源動態函式庫 `libmc2res_64.so`，內容來自 `test_scripts/res_conv/strings.res.cpp`。 |
| `tools/data_tools/` | 資料建置工具：`aseconv`（ASE 模型轉 TGL）、`makefst`（打包 / 解包 `.fst`）、`makersp`（產生檔案清單）、`pak`（打包 / 解包 `.pak`）。 |
| `tools/text_tool/` | 字型烘焙工具：把 TTF 字型轉成遊戲使用的字型圖集（`.bmp`）與字元資訊（`.glyph`）。不需要顯示器。 |
| `GameOS/gameos/mods/` | Mod 系統（`mod_system.h`：版本、manifest、Modpack、載入順序、檔案覆蓋層；`gos_mods.h`：遊戲端掛勾）。 |
| `tools/mc2mod/` | Mod 製作工具 `mc2mod`（建立、檢查、列出、解析載入順序）。 |
| `examples/mods/`、`examples/modpacks/` | 範例 Mod 與 Modpack（也用於自動測試）。 |
| `thirdparty/enet/` | ENet 1.3.18（MIT 授權）可靠 UDP 函式庫，區網聯機用。遊戲只透過 `GameOS/gameos/net/gos_net.h` 使用它（連線、可靠／不可靠通道、在遊戲埠 27015 上回應房間探索查詢）。 |
| `thirdparty/pl_mpeg/` | 單檔 MPEG-1 影片／MP2 音訊解碼器（MIT 授權），播放過場動畫用。`code/mc2movie.cpp` 把每幀轉成 RGBA 更新到一張材質，以一個四邊形畫出，所以所有繪圖後端都能顯示；動畫的聲音經 `gosAudio_*PCMStream` 送進 SDL3_mixer。若 `data/sound/` 有同名 `.wav`，則改播那個檔案。 |
| `thirdparty/ARM/` | 原版 XNA ARM（素材資源管理）介面的標頭檔，`mclib/msl.cpp` 和 `aseconv` 用它記錄素材關聯；本移植版一律編譯它的空實作（`LINUX_BUILD`）。 |
| `tests/` | 自動化測試（見[自動化測試](#自動化測試)）。 |
| `scripts/` | `prepare_game_dir.sh`（建置遊戲資料並組出可執行的遊戲目錄）、`smoke_test.sh`（無頭端到端測試），以及測試、打包、效能量測腳本。 |
| `cmake/MC2Dependencies.cmake` | 取得 SDL3 / SDL3_mixer / SDL3_ttf：優先用已安裝的版本，否則自動下載固定版本並靜態編譯。 |
| `.github/workflows/ci.yml` | GitHub Actions：建置、單元測試、實際執行遊戲的冒煙測試。 |
| `test_scripts/res_conv/` | 介面字串原始檔（`strings.res.cpp`、`resource.h`）和原作者的轉換腳本 `res_conv.pl`。 |
| `doc/` | 玩家說明書與 Mod 說明。 |

### 遊戲資料流程

遊戲資料不在本 repo，而是放在 [mc2srcdata](https://github.com/alariq/mc2srcdata)。建置流程如下：

```
mc2srcdata（原始素材）
   │  aseconv / makefst / makersp / pak / text_tool / ffmpeg
   ▼
*.fst、*.pak、data/、assets/（打包後的遊戲資料）
   │  scripts/prepare_game_dir.sh
   ▼
遊戲目錄：mc2 + libmc2res.so + shaders/ + 遊戲資料
```

執行時，`File::open()` 會先找遊戲目錄中的散檔，找不到再到 `.fst` 資料包中找。

---

## 在 Linux 上建置

已在 **Ubuntu 24.04（GCC 13、CMake 3.28）** 驗證過。

### 1. 安裝相依套件

```bash
sudo apt-get install build-essential cmake git libglew-dev libgl-dev zlib1g-dev libfreetype-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxtst-dev libxfixes-dev libxkbcommon-dev libpulse-dev libasound2-dev
```

Ubuntu 24.04 沒有 SDL3 套件，所以第一次設定時 CMake 會自動下載並靜態編譯固定版本的
SDL 3.4.8、SDL3_mixer 3.2.4（只開 WAVE 等不需外部函式庫的格式）與 SDL3_ttf 3.2.2（使用系統 FreeType），
產出的執行檔不依賴任何 SDL 共享函式庫。上面的 X11 / PulseAudio / ALSA 開發套件是讓 SDL3 能編進對應的後端。
若系統已安裝 SDL3（例如 `/opt/sdl3`），可用 `-DCMAKE_PREFIX_PATH=/opt/sdl3` 讓 CMake 直接使用。

### 2. 編譯

```bash
cmake -S . -B build
```

```bash
cmake --build build -j"$(nproc)"
```

未指定 `CMAKE_BUILD_TYPE` 時，預設使用 `RelWithDebInfo`（有最佳化、保留除錯資訊）。
其他選項：

| 選項 | 說明 |
|---|---|
| `-DCMAKE_BUILD_TYPE=Debug` | 除錯版，啟用 `gosASSERT` 等斷言 |
| `-DMC2_BUILD_TESTS=OFF` | 不編譯測試 |
| `-DMC2_ENABLE_ASAN=ON` | 啟用 AddressSanitizer 檢查記憶體錯誤 |
| `-DMC2_SDL3_SOURCE=AUTO\|SYSTEM\|FETCH` | SDL3 來源：預設 `AUTO`（有安裝就用、沒有就下載）；`SYSTEM` 只用已安裝的；`FETCH` 一律下載 |

### 3. 產出檔案

| 檔案 | 用途 |
|---|---|
| `build/mc2` | 遊戲執行檔 |
| `build/out/res/libmc2res_64.so` | 字串資源（放進遊戲目錄時需改名為 `libmc2res.so`） |
| `build/out/data_tools/{aseconv,makefst,makersp,pak}` | 資料建置工具 |
| `build/out/text_tool/text_tool` | 字型烘焙工具 |

---

## 建置遊戲資料並執行

### 1. 取得遊戲資料

```bash
git clone https://github.com/alariq/mc2srcdata.git
```

過場動畫以 Git LFS 儲存。若已安裝 `git-lfs`，clone 後執行：

```bash
git -C mc2srcdata lfs pull
```

另外需要 `ffmpeg`：資料建置會把動畫轉成 MPEG-1（`.mpg`），遊戲用內建的 [pl_mpeg](../thirdparty/pl_mpeg/pl_mpeg.h)（MIT 授權）解碼播放。

```bash
sudo apt-get install ffmpeg
```

### 2. 建置資料並組出遊戲目錄

```bash
scripts/prepare_game_dir.sh build mc2srcdata game
```

這個腳本會把資料工具複製到 `mc2srcdata/build_scripts`，執行資料建置（約 15 秒），
再把資料、執行檔、`libmc2res.so` 與 `shaders/` 組合到 `game/` 目錄。

若無法取得 LFS 動畫檔，可以用 1 秒的測試畫面代替（遊戲可正常執行，只是過場動畫是測試畫面）：

```bash
MC2_PLACEHOLDER_MOVIES=1 scripts/prepare_game_dir.sh build mc2srcdata game
```

### 3. 執行

```bash
cd game && ./mc2
```

- **第一次執行**時直接以最高畫質預設（`hiprefs.cfg`）開始。原版會先跑一次硬體檢測再自動結束，現在只有加 `-sniffer` 時才會執行。
- 顯示版本：`./mc2 -version`（視窗標題也會顯示）。版本號在 CMake 設定時由 `git describe` 決定，也可用 `-DMC2_VERSION=1.2.3` 指定。
- 直接進入指定任務（跳過選單）：`./mc2 -mission mc2_01`
- 設定檔（`options.cfg`、`orgprefs.cfg`）與存檔放在系統的應用程式資料目錄（`SDL_GetPrefPath`），所以遊戲目錄可以是唯讀的：
  Linux 為 `~/.local/share/MechCommander2/`（或 `$XDG_DATA_HOME/MechCommander2/`），
  macOS 為 `~/Library/Application Support/MechCommander2/`，Windows 為 `%APPDATA%\MechCommander2\`。
  設定環境變數 `MC2_USER_DIR` 可改用其他目錄（測試時用來從空目錄開始）。
- **舊存檔遷移**：舊版放在 `~/.mechcommander2/`（或 `$XDG_CONFIG_HOME/.mechcommander2/`、Windows 的
  `%USERPROFILE%\.mechcommander2\`）。新目錄是空的時候，遊戲會把舊目錄的內容複製過去，並在舊目錄留下
  `MOVED.txt` 說明新位置；舊檔案不會刪除。新目錄已有資料時不會再複製。
  舊版寫在遊戲目錄裡的 `options.cfg` / `orgprefs.cfg` 也會在第一次啟動時複製到新目錄。
- 遊戲需要在遊戲目錄（含 `data/` 的目錄）中執行。從檔案管理員（Finder、檔案總管）直接開啟時，
  遊戲會依序到執行檔旁邊、執行檔的 `../share/mechcommander2/`（Linux 安裝包）、macOS `.app` 的 `Contents/Resources/`、或 `.app` 旁邊尋找 `data/` 並切換過去。
  也可以用 `-datapath <目錄>` 直接指定遊戲目錄。注意其他參數裡的相對路徑（例如 `-mods`）會以該目錄為基準。
- 錯誤訊息會以對話框顯示並等待選擇；以 `-fixedstep`、輸入腳本執行，或設定 `MC2_NO_DIALOGS=1` 時，
  只寫到標準錯誤輸出並自動回答（測試腳本都會設定）。
- 設定 `MC2_NO_MOVIES=1` 時所有過場動畫立即結束。截圖測試與效能基準會設定它，讓結果與動畫檔（Git LFS 的真正影片或長度不同的測試畫面）無關。

---

## 區網聯機的實作

玩家操作見[區網對戰說明書](manual/lan-skirmish.zh-TW.md)。

### 運作方式

- **主機權威**：主機跑完整的模擬（AI、命中判定、傷害、任務勝負）；客端只送出指令，並顯示主機送來的狀態。客端掉線時，主機的 AI 接手他的單位；主機掉線時，客端的任務結束。
- 傳輸層是 [ENet](../thirdparty/enet/)（可靠／不可靠 UDP），包在 `GameOS/gameos/net/gos_net.h` 後面，遊戲碼不直接碰 ENet。
- 星狀拓撲：客端只連主機，主機轉送。訊息是 `code/multplyr.h` 中的 `MCMSG_*` 結構，欄位一律是固定大小的型別，三個平台的位元組排列相同。
- 房間探索和遊戲共用 UDP 27015：掃描端把查詢送到 255.255.255.255、每張網卡的子網廣播位址和 127.0.0.1，每個主機回覆自己的房間資訊。
- `code/multplyr.cpp`：房間、名單、顏色與隊伍、聊天、載入握手。`code/multplyr_sync.cpp`：任務中的同步——單位的移動／狀態 chunk（有變動才送，10 Hz）、開火、致命傷、無線電、命中、世界事件（地雷、火焰、砲擊、佔領建築）、指令、支援單位、暫停、勝負判定。

### 除錯用環境變數

| 變數 | 作用 |
|---|---|
| `MC2_NET_LOG=2` | 標準輸出列出每一則網路訊息（預設 `1` 只列連線、加入、載入步驟等事件，`0` 關閉） |
| `MC2_NET_STATEFILE=<檔案>` | 任務中每秒寫下所有單位的位置與狀態、擊殺／損失（客端用主機的任務時鐘），用來比對兩台電腦是否一致 |
| `MC2_NET_TEST_CHARGE=1` | 只給自動測試用：主機在第 10 秒命令自己的單位攻擊最近的敵人 |
| `MC2_NET_TEST_VERSION=<版本>`、`MC2_NET_TEST_PROTOCOL=<stamp>` | 只給自動測試用：假裝成另一個版本（房間廣播裡的遊戲版本、網路協定 stamp） |

### 測試

- `unit_net`（CTest）：本機開主機與客端，互傳 1000 個可靠封包（順序與內容）、64 KB 分片、廣播、房間探索（廣播與直接 IP）、本機網卡列舉（每個位址都能被直接 IP 查到）、連不上時的逾時。
- `scripts/run_net_test.py game`：在同一台機器開兩個遊戲實例，用腳本操作主選單 → 開房／加入 → 任務整備 → 開始遊戲，進入 WetWork 作戰；比對兩邊的擊毀與失能狀態、擊殺數及位置。
- `scripts/run_net_test.py game --scenario version`：主機假裝成 9.9.9 版、協定也不同；客端的房間列表要顯示「VERSION 9.9.9」，按 Join 要跳出版本對話框，而且不能連上。

### 房間裡的版本

- 房間廣播的最後是主機的遊戲版本。0.2 以前的主機送的是較短的廣播，仍然收得到，版本顯示成「< 0.2」。客端在協定 stamp 和遊戲資料雜湊都和自己相同時，才把房間標成可加入。
- 不能加入的房間，地圖欄改顯示 `VERSION x.y.z`（版本相同但資料不同時顯示 `DIFFERENT GAME DATA`）。按 Join 不會連線，直接顯示雙方的版本。主機比較新時，房間列表的對話框有 **Download**（打開 Release 頁面）；直接輸入 IP 的面板則寫出下載位置。主機比較舊時，提示主機要更新。
- 能不能一起玩仍然由協定 stamp（`code/multplyr.cpp` 的 `MC2NET-4`）決定，版本字串只用來顯示。網路訊息有任何改動時，都要更新 stamp。

---

## 版本檢查

程式在 `GameOS/gameos/update/gos_update.{h,cpp}`（檢查）和 `gos_update_install.cpp`（下載與安裝），引擎初始化後由 `gameosmain.cpp` 啟動，`code/mainmenu.cpp` 負責顯示。

- 背景執行緒查詢 `https://api.github.com/repos/hyouka0904/mc2/releases/latest`（逾時 5 秒）。這個網址只回傳已發佈的正式版，不會有草稿或預覽版。取得的 tag 用語意化版本和 `MC2_VERSION` 比較（預覽版排在正式版之前）。
- 結果存在使用者資料夾的 `update.ini`，**24 小時最多連網查一次**。請求只帶一個通用的 `User-Agent`。
- HTTPS：Windows 用 WinHTTP；Linux 和 macOS 用 `dlopen` 載入系統的 libcurl（建置時不需要它，AppImage 也會用系統的憑證）。找不到 libcurl 就跳過檢查。
- 開發版不檢查：解析不了的版本、`0.0.0*`（本機和 CI 的測試建置），以及 `git describe` 產生、在 tag 之後的版本（`1.2.0-14-gabc1234`）。
- 主選單：左下角一行會閃爍的「NEW VERSION x.y.z AVAILABLE - CLICK HERE」，只在真正的主選單出現。對話框有 **Download**（打開 GitHub 的 Release 頁面；在跑新版之前，下次啟動還會提示）和 **Later**（隱藏通知到下次啟動遊戲）。選項 > Gameplay 的「接收測試版本」下面有更新按鈕（勾選時或已知有新版時出現）：可再檢查、下載並安裝（附預計大小），或顯示下載進度。
- 關閉方式：`-noupdatecheck`、`MC2_NO_UPDATE_CHECK=1`（CI 有設）、`update.ini` 裡的 `check=0`，以及決定性或腳本模式（`-fixedstep`、`-input-script`）。
- `MC2_UPDATE_URL=<網址>` 讓檢查改連測試伺服器（可以用 http）；設了它，開發版和腳本模式也會檢查。
- 測試：`unit_update`（CTest）涵蓋版本解析與排序、Release JSON（草稿、預覽版、格式錯誤、不是 GitHub 的網址）、`update.ini`、延後提示的規則，以及對 127.0.0.1 上小型 HTTP 伺服器的完整背景檢查（快取、伺服器錯誤、`check=0`、開發版）。
- **測試版頻道**：選項 > Gameplay >「Receive test versions」（`update.ini` 的 `channel=test`）。打開後每次啟動都查詢 `releases?per_page=20`（不受 24 小時限制），取最新的版本，預發布版也算；切換這個選項會立刻重新檢查。測試版來自手動執行 Release workflow（見[發佈新版本](#發佈新版本)）。
- **下載與安裝**：對話框的 **Update** 會把對應安裝方式的安裝包下載到使用者資料夾的 `updates/`（提示文字顯示進度），用 Release 的 `SHA256SUMS.txt` 驗證，再詢問要不要安裝並重新啟動：

  | 安裝方式 | 安裝包 | 安裝做法 |
  |---|---|---|
  | Windows 安裝版（`mc2.exe` 旁邊有 `unins000.exe`） | `-windows-x64-setup.exe` | `/VERYSILENT /DIR=<遊戲資料夾> /MC2RELAUNCH=1`；遊戲結束，安裝程式裝完再開遊戲（`mc2.iss` 的 `IsGameUpdate`；資料夾不能寫入時加 `/ALLUSERS`，會跳 UAC） |
  | Windows 免安裝版（zip） | `-windows-x64.zip` | 直接取代檔案；執行中的 `mc2.exe` 和 DLL 先改名成 `*.mc2old`，下次啟動時刪除 |
  | Linux AppImage（有 `$APPIMAGE`） | `-linux-x86_64.AppImage` | 寫到舊檔旁邊，再改名蓋過去 |
  | Linux 壓縮檔（`<根目錄>/usr/bin/mc2`） | `-linux-x86_64.tar.gz` | 和 zip 一樣直接取代 |
  | macOS | `-macos-arm64.dmg` | 打開磁碟映像，由玩家把 app 拖到「應用程式」 |

- **區塊更新**（Windows 安裝版和免安裝版、Linux 壓縮檔）：Release 有對應安裝方式的 `<安裝包>-manifest.json` 時，遊戲只下載有變動的部分，不再下載整包（約 600 MB）：
  - `scripts/package.py` 把每個安裝包拆成區塊：`-<平台>-program.zip`（執行檔、shader、授權檔，所有不屬於遊戲資料的檔案），以及遊戲資料的 `-data-<名稱>.zip`（`textures`、`models` = `tgl.fst`、`archives` = 其他 `.fst`、`sound`、`movies`、`misc`）。資料區塊只由 Linux job 產生一次，各平台共用（`--write-data-parts`／`--data-parts`；Windows 會檢查自己的資料是否相同）。清單列出每個區塊（檔名、大小、SHA-256、在安裝目錄下的資料夾）和其中每個檔案（路徑、大小、SHA-256、可執行位元）。
  - 遊戲先用 `SHA256SUMS.txt` 驗證清單，再計算已安裝檔案的雜湊（先比大小；`updates/filehash.txt` 依大小和修改時間記住算過的雜湊），只下載含有缺少或不同檔案的區塊，每個區塊都用清單驗證。只解出這些區塊裡有變動的檔案，替換前再驗證一次，和整包安裝一樣「全部成功或完全不動」。跳過好幾個版本也可以，因為比對的是目前安裝的檔案，不是上一版。
  - 只改程式時只下載程式區塊：Linux 約 5 MB（打包時拿掉執行檔的除錯資訊，保留函式名稱），Windows 是 `mc2.exe` 加上 DLL 的大小。資料沒改時，每個 Release 的資料區塊都相同：遊戲資料的建置現在每次逐位元組相同（以前 `aseconv` 會寫出未初始化的位元組，讓 `tgl.fst` 每次建置都不一樣；`mclib/tgl.*`、`msl.cpp` 現在會先清零）。
  - 遊戲資料夾不能寫入時（Windows 裝在 Program Files 給所有使用者）：遊戲用系統管理員權限啟動 `mc2.exe -apply-update <計畫檔>`（跳一次 UAC），等它完成再重新啟動。這個輔助程式負責寫入檔案、把舊檔排定在 Windows 下次重新開機時刪除，並更新「已安裝的應用程式」顯示的版本。
  - 區塊更新新增的檔案不在安裝程式的解除安裝清單裡，解除安裝後會留在遊戲資料夾。
  - AppImage 和 macOS 仍然下載整包；沒有清單的 Release 也一樣。

  所有新檔案都先寫成 `*.mc2new`，全部寫完才開始改名，所以下載壞掉時什麼都不會改。只接受 `https://github.com/hyouka0904/mc2/releases/download/` 的下載（或 `MC2_UPDATE_URL` 的測試伺服器）。其他情況（不認得的安裝方式、資料夾唯讀、沒有對應安裝包、失敗）都退回打開 Release 頁面。下載好但還沒安裝的安裝包，下次啟動會直接使用；比較舊的會刪掉。
- 檢查碼只能防下載損壞，防不了 GitHub 帳號被盜；簽章（minisign，工作清單的 U4）還沒做。
- 測試：`unit_update` 另外涵蓋版本清單（頻道、草稿、安裝包名稱）、SHA-256 測試向量、`SHA256SUMS.txt`、挑選安裝包、辨認壓縮檔安裝、解開 zip 和 tar.gz（長檔名、符號連結、`..` 項目、損壞的壓縮檔必須什麼都不改）以及下載（錯誤和取消時會刪掉檔案）；區塊更新方面涵蓋清單（離開遊戲資料夾的路徑、錯誤的雜湊）、找出有變動的檔案和雜湊快取、套用區塊（雜湊錯誤或缺檔時什麼都不改；可執行位元）以及輔助程式的計畫檔。Release workflow 會對壓縮檔版和裝在 Program Files 的 Windows 全使用者安裝各套用一次區塊更新（`scripts/part_plan.py` 產生計畫檔；輔助程式會取代自己正在執行的 exe，並更新「已安裝的應用程式」的版本）。

---

## 存檔相容性

存檔放在使用者目錄的 `savegame/`，共有兩種，跨版本時的表現差很多。

| | 戰役存檔（`*.fit`，任務之間） | 任務中快速存檔（`testgame.ims`） |
|---|---|---|
| 內容 | C-Bills、駕駛員、機甲設計與庫存、戰役進度 | 整個戰場 |
| 格式 | 文字，`名稱 = 值` | 遊戲的資料結構（`MechData`、`MoverData`…）整塊寫入 |
| 版本 | `VersionNumber`（`code/logisticsdata.cpp` 的 `SaveGameVersionNumber`） | `versionStamp` 加上格式指紋 |
| 跨版本 | 必須一直能讀 | 資料結構改變後可能不能讀；會顯示訊息拒絕 |

**戰役存檔**：`load()` 接受 `OldestSaveGameVersionNumber` 到 `SaveGameVersionNumber` 之間的格式（目前都是 10004），「讀取遊戲」清單也顯示同樣範圍。要改格式時：把 `SaveGameVersionNumber` 加 1，在 `load()` 裡繼續讀舊號碼並轉換改變的部分，`OldestSaveGameVersionNumber` 不動。存檔裡提到、但這個版本沒有的東西不再讓讀檔失敗：找不到底盤的設計、不存在或放不下的零件、用了不存在設計的機甲都會略過，並在 stderr 印一行 `[save]`。讀檔成功會印 `[save] campaign save loaded (format N): ...`。

**快速存檔**：檔頭是 `00.06.01.0614 L<雜湊>`。雜湊（`code/savelayout.cpp`）是快速存檔寫入的每個資料結構大小的 FNV-1a；讀檔時也是靠封包大小分辨物件種類。雜湊不同的檔案會被拒絕，顯示「Cannot load Quick Save. Version does not match game」，任務繼續進行，不會讀進錯誤的資料。加指紋之前的檔案（只有 `00.06.01.0614`）在結構仍和當時相同時照樣能讀：判斷依據是 `SaveLayoutIsLegacy()` 裡凍結的數值。指紋依平台而不同（Windows 的 `long` 是 4 位元組），所以快速存檔也不能在 Windows 和 Linux 之間搬。

存進檔案的結構改變大小時，`unit_savelayout` 會刻意失敗：更新 `tests/unit/test_savelayout.cpp` 的 `kCurrent`（不要改 legacy 數值），並在發佈說明寫明舊的快速存檔不能再讀。

**測試裡的舊存檔**：`tests/saves/<版本>/` 放每個版本的戰役存檔和一份 Linux 快速存檔，CI 的流程測試 `old_campaign_save`、`old_quick_save`、`foreign_quick_save` 會讀它們。存檔格式有改變的版本，就新增一個資料夾。

更新不會動到使用者資料夾，所以存檔和設定在更新後都還在。

---

## 自動化測試

### 單元與工具測試（CTest）

```bash
ctest --test-dir build --output-on-failure
```

| 測試 | 內容 |
|---|---|
| `unit_platform` | 跨平台字串函式（`S_strcmp` 等）與 Win32 替代實作（`MessageBox` 自動回應） |
| `unit_kv_registry` | Windows 登錄檔模擬（讀寫、格式、邊界情況） |
| `unit_image_tga` | TGA 貼圖解碼：未壓縮與 RLE 壓縮、24/32/16 位元、損壞資料 |
| `unit_pqueue` | 路徑搜尋優先佇列，包含與參考模型比對的隨機測試 |
| `unit_run_options` | 決定性模式參數、PNG 截圖編碼、固定步長時鐘、輸入腳本解析 |
| `unit_sound` | 音效 API（SDL3_mixer）：播放一次 / 迴圈 / 暫停 / 停止、音量線性、左右聲道定位、釋放資源；3D 音效的距離衰減、依聽者方向定位、都卜勒、移動聽者；輸出限幅器（小聲不變、大聲不削波並會恢復、多個大聲音效同時播放不削波）；不需要音效卡 |
| `unit_heap` | GameOS 記憶體堆積：`gos_Malloc` / `new` 依目前 heap 記錄、`gos_DestroyMemoryHeap(heap, false)` 釋放剩餘配置、`(heap, true)` 只回報洩漏且區塊仍可用 |
| `unit_render_state` | 由 GameOS 渲染狀態推出的管線描述、32 位元管線鍵（可逆、互不重複）、管線快取 |
| `unit_net` | 區網傳輸層，見[區網聯機的實作](#區網聯機的實作) |
| `unit_update` | 版本檢查，見[版本檢查](#版本檢查) |
| `unit_savelayout` | 快速存檔寫入的資料結構大小，見[存檔相容性](#存檔相容性) |
| `render_isolation` | 檢查 OpenGL 只出現在 `GameOS/gameos/render/gl/` |
| `shader_validation` | 以 glslangValidator 離線編譯並連結所有著色器變體（與執行時相同的 `#version 420`、`ALPHA_TEST` 定義與 `#include` 展開），不需要 GPU；未安裝 `glslang-tools` 時略過 |
| `shader_spirv` | 所有變體編成 Vulkan SPIR-V 並通過 `spirv-val`、兩個階段的介面能連結、轉出 MSL / HLSL，並檢查 `PerDraw` uniform block 與 C++ 結構的位移一致；`-DMC2_SHADER_REQUIRE=msl,hlsl` 可要求轉換器必須存在（CI 使用） |
| `unit_mods` / `tool_mc2mod` | Mod 系統與製作工具（詳見 [doc/modding.zh-TW.md](modding.zh-TW.md)） |
| `unit_stuff` | 執行原版 Stuff 數學函式庫內建、但從未被呼叫的自我測試 |
| `tool_fastfile` / `tool_pak` / `tool_rsp` | 資料工具的打包→解包往返測試 |
| `tool_text_tool` | 在沒有顯示器的環境下烘焙字型 |

測試使用 `tests/framework/mc2test.h` 這個不依賴外部套件的小型框架。新增測試的方式：

```cpp
#include "mc2test.h"

TEST(my_feature_works)
{
    CHECK(1 + 1 == 2);
    CHECK_EQ(answer(), 42);
}
```

再到 `tests/CMakeLists.txt` 用 `mc2_add_unit_test(...)` 註冊即可。

### 端到端冒煙測試（實際執行遊戲）

在 Xvfb 虛擬顯示器與 Mesa 軟體 OpenGL 下實際遊玩一個任務，不需要 GPU：

```bash
sudo apt-get install xvfb x11-utils imagemagick libgl1-mesa-dri
```

```bash
scripts/smoke_test.sh game mc2_01 60
```

測試使用全新的使用者資料目錄（`MC2_USER_DIR`），所以也檢查第一次啟動：遊戲必須直接進入任務，並把 `options.cfg` 寫到使用者目錄。
若遊戲提前結束（當機）、跳出訊息框、出現 OpenGL 錯誤、不回應 SIGTERM、沒有寫出設定檔，或畫面全黑，測試就會失敗。
結果截圖會存為 `game/smoke.png`。額外的遊戲參數放在環境變數 `MC2_SMOKE_ARGS`，例如以 SDL3 GPU 後端執行：

```bash
MC2_SMOKE_ARGS="-renderer gpu" scripts/smoke_test.sh game mc2_01 60
```

SDL3 GPU 後端在沒有 GPU 的環境使用 Mesa 的軟體 Vulkan（lavapipe）：`sudo apt-get install mesa-vulkan-drivers vulkan-validationlayers`。

### 截圖比對測試（決定性模式）

遊戲支援幾個 GameOS 層的命令列參數，讓每次執行都產生逐像素相同的畫面：

| 參數 | 作用 |
|---|---|
| `-fixedstep <ms>` | 決定性模式：每一幀時鐘固定前進 `<ms>` 毫秒，`frameRate` 固定為 `1000/<ms>`，主迴圈不再 sleep |
| `-seed <n>` | 固定亂數種子，取代所有以時間為種子的地方（`gos_srand`、`srand(time)`、ABL `seedRandom(-1)`） |
| `-capture-frames a,b,c` | 在指定幀（從 1 起算）讀回 back buffer 存成 PNG |
| `-capture-dir <dir>` | 截圖輸出目錄 |
| `-exit-after <n>` | 第 `<n>` 幀後結束（預設為最後一個截圖幀） |
| `-input-script <file>` | 依腳本重播滑鼠／鍵盤事件，期間忽略真實輸入（格式見 `GameOS/gameos/utils/input_script.h`） |
| `-perf-csv <file>` | 每幀寫一行統計：幀號、實際耗時（毫秒）、draw call 數、管線狀態切換次數、出現過的不同管線數、主執行緒 CPU 時間（毫秒） |
| `-datapath <dir>` | 指定遊戲目錄（含 `data/` 的目錄） |
| `-version` | 顯示版本後結束 |
| `-mods <dir>` / `-modpack <file>` | 啟用 Mod / Modpack（見 [doc/modding.zh-TW.md](modding.zh-TW.md)） |
| `-windowed` | 忽略遊戲的全螢幕設定，一律以視窗執行（全螢幕行為取決於視窗管理員，會讓截圖因環境而異） |
| `-shader-reload` | 開發用：著色器原始碼變更時在遊戲中重新編譯（每半秒檢查一次，預設關閉） |
| `-renderer gl\|gpu` | 繪圖後端：`gpu`＝SDL3 GPU（預設，Linux 上為 Vulkan），`gl`＝OpenGL。截圖比對腳本一律明確指定。環境變數 `MC2_GPU_DEBUG=1` 開啟 Vulkan 驗證層，`SDL_GPU_DRIVER` 可指定驅動 |
| `-no-batching` | 關閉繪製合併，每個 `gos_Draw*` 呼叫各畫一次（比較效能或排查問題用） |
| `-gpu-shapes` | 3D 模型的實心部分改用 `gos_RenderShape` 在 GPU 上轉換與打光（光照與 CPU 路徑一致）；在軟體繪圖上較慢，預設關閉 |

測試在 800×600 的 Xvfb 畫面上以 `-windowed` 執行，使用一個預先放入低畫質預設（`minprefs.cfg`，基準圖以此產生）的全新使用者目錄。`tests/golden/manifest.txt` 列出 18 張基準圖：4 個任務各 3 幀、主選單、戰役選擇、Solo Mission 畫面、
暫停中的任務，以及載入範例 Mod / Modpack 的主選單。執行與比對：

```bash
sudo apt-get install python3-pil python3-numpy
```

```bash
scripts/run_screenshot_tests.py game shots
```

任一通道差異大於 8/255 的像素算「不同」，不同像素達 0.5% 即失敗，並在 `shots/diff/` 輸出熱度圖。
基準圖由 OpenGL 後端產生；SDL3 GPU 後端用同一組基準圖比對（`--renderer gpu`，加 `--gpu-validation` 時開啟 Vulkan 驗證層，記錄中出現任何驗證訊息即失敗）。
在 lavapipe 上量得兩個後端的差異：選單與介面畫面 0%，任務畫面最多 0.25%（3D 模型邊緣的光柵化差異），因此門檻維持 0.5% 不變。
畫面刻意變更（例如修改 shader）後，用 `--update` 重新產生基準圖並一起提交。
單獨比較兩張圖或兩個資料夾可用 `scripts/compare_screenshots.py`。

### UI 流程測試（走過遊戲的每個畫面）

```bash
scripts/run_flow_tests.py game --renderer gl          # 全部流程
scripts/run_flow_tests.py game --only pause_menu      # 只跑指定的流程
```

用腳本操作滑鼠和鍵盤，實際玩過每個畫面。每個流程都有逾時（預設 300 秒），超過就判定為卡死。記錄檔中不能出現 STOP、FATAL、ASSERT、MSGBOX 或 Segmentation。使用者目錄的路徑刻意包含大寫字母與空格，和預設的 `~/.local/share/MechCommander2` 一樣。每 100 幀截一張圖，和記錄檔一起留在 `<out>/<流程>/`，方便查出流程在哪裡出錯。

| 流程 | 操作與檢查 |
|---|---|
| `options` | 主選單 → Options，切換四個分頁後按 Accept；解析度不能被改掉 |
| `encyclopedia` | 百科的六個分類 → Close |
| `exit` | Exit → Yes，遊戲要在時限前自行結束 |
| `campaign` | 新戰役 → 第一關（用除錯鍵 Ctrl+Alt+Shift+W 直接過關）→ 結算 → 選關 → 簡報 → 機甲庫 → 駕駛員 → 出擊第二關；要留下戰役存檔 |
| `load_game` | Load Game 要列出 `campaign` 留下的存檔，讀檔後進入後勤畫面 |
| `solo_mission` | Solo Mission 要列出已完成的任務，選取後進入簡報、機甲庫、駕駛員畫面 |
| `quick_save` | 用快捷鍵：沒有存檔時先 Quick Load（滑鼠不能被鎖住），再 Quick Save、Quick Load；記錄檔要有 `Saved game` / `Loaded game` |
| `pause_menu` | 先框選單位，再用暫停選單 Quick Save、Quick Load、Options → Accept、Exit → Yes |
| `old_campaign_save` | 用 `tests/saves` 裡每個版本的戰役存檔「讀取遊戲」；log 必須顯示格式、3 台機甲、80000 C-Bills，且沒有任何東西被略過 |
| `old_quick_save` | Quick Load 0.1.0 的快速存檔；必須印出 `Loaded game` |
| `foreign_quick_save` | Quick Load 一份帶別的版本標記的快速存檔；必須顯示訊息拒絕、沒有 `Loaded game`、任務繼續 |

`load_game` 和 `solo_mission` 會用到 `campaign` 留下的存檔，所以要和它一起跑，或排在它之後。

這些流程曾經抓到的問題（都已修正）：

- 顯示卡名稱很長時，選項畫面的文字換行陷入無窮迴圈。
- 在選項畫面按 Accept，解析度就被改掉：SDL3 回報的色深是 24，設定檔存的是 32，比對不到時清單選了第一項。
- 使用者目錄的路徑被整個轉成小寫，在 Linux 上存檔全部失敗。
- 讀檔失敗時，滑鼠沒有恢復。
- 暫停選單的按鈕點了沒反應：判斷「是不是剛拖曳完」用的是上一次點擊的狀態，而且這個狀態沒有初始化。框選單位之後，第一下點擊一定會被吞掉。
- 用 `-mission` 直接開始任務時，在暫停選單的 Options 按 Accept 會當掉。

### 持續整合

`.github/workflows/ci.yml` 在每次 push / PR 時執行；只改文件（`*.md`、`doc/`）的 push 會跳過，要強制執行時在 Actions 頁面按「Run workflow」：

1. 以 RelWithDebInfo 與 Debug 建置並執行全部 CTest 測試（Linux），另在 Windows（MSVC）與 macOS（Apple Silicon）建置並執行單元測試；
2. 下載遊戲資料、建置資料，並以冒煙測試實際遊玩 `mc2_01` 60 秒；
3. 執行截圖比對測試與 UI 流程測試（兩個繪圖後端），上傳截圖、熱度圖與記錄檔。

macOS job 另外檢查 `mc2.app` 的內容，並執行 `mc2 -version`，輸出不對或程式當掉時該步驟失敗。

每個 job 都設有 `timeout-minutes`，遊戲卡住時不會一直佔用 CI。

`.github/workflows/release.yml` 建置並測試三個平台的安裝包，見[安裝包與發佈](#安裝包與發佈)。

---

## 在 Windows 上建置

> CI（`windows-latest`、MSVC x64）會建置全部程式並執行單元測試；安裝包由玩家實機測試。

SDL3 系列會由 CMake 自動下載並靜態編譯（與 Linux 相同）。zlib、GLEW、FreeType 建議用 vcpkg 安裝
（靜態函式庫 + 動態 CRT，與 CMake 的 Release 設定相同）：

```bat
vcpkg install zlib glew freetype --triplet x64-windows-static-md
cmake -S . -B build -A x64 -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build --config RelWithDebInfo
ctest --test-dir build -C RelWithDebInfo
```

字串資源 `mc2res_64.dll` 會跟主程式一起建置，放在 `mc2.exe` 旁邊或遊戲目錄中即可。
SDL3 GPU 後端在 Windows 上會使用 Vulkan（讀取 SPIR-V）；D3D12 需要 DXIL，目前依決策暫緩。

## 在 macOS 上建置

> CI（`macos-latest`、Apple Silicon）會建置全部程式、執行單元測試並檢查 `.app`；macOS 實機遊玩仍需人工驗證。

最低支援 macOS 13。macOS 只有已淘汰的 OpenGL 4.1，所以預設不建置 OpenGL 後端（`MC2_ENABLE_OPENGL=OFF`），
只使用 SDL3 GPU（Metal）：

```bash
brew install glslang spirv-tools spirv-cross freetype
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMC2_SHADER_REQUIRE=msl
cmake --build build -j"$(sysctl -n hw.ncpu)"
ctest --test-dir build
```

產出 `build/mc2.app`（字串資源 `libmc2res.dylib` 在 `Contents/Resources/`）。`scripts/prepare_game_dir.sh`
會把 `mc2.app` 複製到遊戲目錄，並建立 `./mc2` 指向其中的執行檔，可在終端機執行；
從 Finder 開啟時，遊戲會到 `.app` 旁邊尋找 `data/`。

---

## 安裝包與發佈

### 下載的安裝包

| 平台 | 檔案 | 內容 |
|---|---|---|
| Windows 10 以上（x64） | `MechCommander2-<版本>-windows-x64-setup.exe` | Inno Setup 安裝程式：預設裝在使用者自己的目錄，不需要系統管理員權限，也可選擇替所有使用者安裝到 Program Files。會建立開始選單捷徑（桌面捷徑可選），加入區網聯機的防火牆規則（可取消，預設勾選），並提供解除安裝。 |
| | `MechCommander2-<版本>-windows-x64.zip` | 免安裝版，解壓後執行 `mc2.exe` |
| macOS 13 以上（Apple Silicon） | `MechCommander2-<版本>-macos-arm64.dmg` | `MechCommander 2.app`，遊戲資料放在 `Contents/Resources/`，拖到「應用程式」即可 |
| Linux x86_64 | `MechCommander2-<版本>-linux-x86_64.AppImage` | 單一執行檔，內含 GLEW 等不一定有安裝的函式庫（OpenGL、Vulkan、X11 用系統的）。需要 FUSE 2 |
| | `MechCommander2-<版本>-linux-x86_64.tar.gz` | 同樣的內容，解壓後執行 `./mc2` |

每個安裝包都包含完整的遊戲資料（約 480 MB），下載後不需要另外建置資料。另外附有授權條款（`licenses/`）和給玩家的說明 `README.txt`（中英對照，內容包括啟動方式、存檔位置、常用參數）。

**沒有程式碼簽章**：Windows SmartScreen 會警告「未知的發行者」，macOS 第一次會拒絕開啟，需要到「系統設定 > 隱私權與安全性」按「強制打開」。若要免除這些警告，Windows 需要購買程式碼簽章憑證，macOS 需要 Apple 開發者帳號（每年 99 美元）並做公證；`scripts/package.py` 裡的 macOS 簽章目前是 ad-hoc（Apple Silicon 執行程式的最低要求）。

### 發佈新版本

```bash
git tag v0.1.0
```

```bash
git push origin v0.1.0
```

`.github/workflows/release.yml` 會：

1. **Linux**：建置、跑單元測試、從 mc2srcdata 建置遊戲資料（必須拿到 Git LFS 的真正影片，不使用測試畫面），產出 AppImage 與 tar.gz。接著從別的目錄、用全新的使用者目錄執行唯讀的 AppImage，以 OpenGL 與 SDL3 GPU 各玩 `mc2_01` 300 幀並檢查截圖不是全黑。
2. **Windows**：以 MSVC 建置，產出安裝程式與 zip（Visual C++ 執行階段 DLL 一起放在 `mc2.exe` 旁邊）。測試靜默安裝、檢查檔案、執行 `mc2.exe -version`、解除安裝。
3. **macOS**：建置、產出 dmg（重新 ad-hoc 簽章並產生圖示）。掛載後檢查簽章、檔案與 `mc2 -version`。
4. 三個平台都通過後，建立一個**草稿** GitHub Release，附上所有安裝包與 `SHA256SUMS.txt`，說明取自 `doc/RELEASE_NOTES.md`。確認無誤後再到 GitHub 上按「Publish」。

遊戲資料和編譯好的著色器（SPIR-V / MSL / HLSL）與平台無關，只在 Linux 建置一次，再交給 Windows 和 macOS 使用。
**測試版**：在 Actions 頁面手動執行（Actions > Release > Run workflow，選分支），會發佈一個**預發布版** `v<下一個修訂號>-test.<執行編號>`（例如 `v0.1.0` 之後是 `v0.1.1-test.42`），附上安裝包和 `SHA256SUMS.txt`，tag 打在該分支的 commit 上。打開「Receive test versions」的遊戲會提示更新到它；其他人看不到（`releases/latest` 不回傳預發布版，而且 `0.1.1-test.42` 排在 `0.1.1` 之前）。只保留最新 5 個測試版和它們的 tag。選項：`version` 手動指定版本；取消 `publish` 只保留 workflow artifact；取消 `macos` 跳過 macOS（測試版本來就不等 macOS）。修改 `packaging/`、`scripts/package.py` 時，會以 `0.0.0-test` 建置並測試安裝包但不發佈，每個安裝包各自保留為一個 workflow artifact（不再另外壓縮）。Release 另外附上區塊更新用的區塊和清單（見[版本檢查](#版本檢查)）。

從 `dev` 發佈新的測試版後，一併更新 `README.md`、`README.zh-TW.md` 的測試版版本號與直達連結。

Windows 與 macOS 的 CI 機器沒有可用的 GPU，所以那兩個平台只測安裝流程與程式能否啟動，沒有實際進入遊戲。

安裝包測試曾經抓到的問題（都已修正）：

- **AppImage 啟動約 8 秒就結束**：遊戲啟動時檢查「目前目錄」的剩餘空間，AppImage 掛載的唯讀映像檔回報 0，於是跳出「硬碟空間不足」對話框。現在改為檢查存檔所在的使用者目錄。
- **Windows 上一進遊戲就黑屏閃退**（v0.1.0 草稿，玩家回報）：SDL3 GPU 建立裝置時宣告支援 SPIR-V、MSL、DXIL 三種著色器，SDL 在 Windows 上因此優先選了 D3D12，但安裝包沒有 DXIL（依決策暫緩），所有著色器都載入失敗，之後存取空指標當掉，也沒有退回 OpenGL。現在只宣告實際附上的格式（Windows 上因此使用 Vulkan），並在初始化時先建立一個著色器測試，失敗就改用 OpenGL。CI 的 Windows 機器沒有 GPU，所以測不到這條路徑。
- **macOS 上 `mc2 -version` 沒有作用，遊戲照常啟動後當掉**：`gameos.hpp` 先 `#pragma pack(push,4)` 才引入標準函式庫，在 macOS（libc++）上讓 `std::string`、`std::vector` 的對齊變成 4，不同原始檔看到的結構配置不一樣，主程式讀錯了命令列選項的位置。現在 `gameos.hpp` 在設定對齊之前先引入會用到的標準標頭，`run_options.h` 也加上 `static_assert`，對齊再被改到時會直接編譯失敗。新增程式碼時，標準標頭請在 `gameos.hpp` 之前引入。

### 在本機打包

```bash
python3 scripts/package.py --platform linux --build build --game game --version 0.1.0 --out dist
```

`--game` 是 `scripts/prepare_game_dir.sh` 組出的遊戲目錄，只取其中的資料。Linux 加上 `--linuxdeploy <linuxdeploy 執行檔>` 才會產出 AppImage；Windows 需要安裝 [Inno Setup 6](https://jrsoftware.org/isinfo.php) 才會產出安裝程式（沒有則只產出 zip）；macOS 用系統內建的 `sips`、`iconutil`、`codesign`、`hdiutil`。

相關檔案都在 `packaging/`：圖示（`icon.png` / `icon.ico`，原創的簡單圖樣，未使用原作商標）、Linux 的 `.desktop`、Windows 的安裝程式腳本 `mc2.iss` 與版本資源 `mc2.rc`、玩家說明、第三方授權清單與 Release 說明。

---

## 已知限制與待辦事項

### 繪圖後端

`GameOS/gameos/render/render_device.h` 定義後端介面，遊戲端程式碼不需要知道用的是哪一個：

| 後端 | 參數 | 說明 |
|---|---|---|
| SDL3 GPU | 預設，或 `-renderer gpu` | Linux 與 Windows 上為 Vulkan，macOS 上為 Metal；讀取建置時編好的 `shaders/compiled/`。只向 SDL 要求實際附上的著色器格式，目前沒有 DXIL，所以 Windows 不會用 D3D12 |
| OpenGL | `-renderer gl`；或 SDL3 GPU 無法啟動時自動改用 | OpenGL 4 Core Profile，執行時編譯 `shaders/`；位於獨立的 `gameos_gl` 函式庫，只有遊戲本體連結它 |

未指定 `-renderer` 時先試 SDL3 GPU，失敗（例如沒有 Vulkan 驅動、缺少 `shaders/compiled/`，或驅動程式無法建立附上的著色器）就重建視窗改用 OpenGL；明確指定時不會自動切換。OpenGL 後端保留一個版本週期作為備援，之後再移除（見下方「決策紀錄」）。

GameOS 是即時模式 API，SDL3 GPU 則要求上傳不能在 render pass 裡做，狀態也要預先烘進 pipeline。所以 `render/gpu/gpu_device.cpp` 先錄下整個畫面：pipeline、貼圖、參數，以及這一幀改過的頂點資料（複製到每幀的上傳環形緩衝區）。畫面結束時一次上傳，重播到離屏目標，再複製到 swapchain。沒有變動的緩衝區會升級成獨立的 GPU 緩衝區。viewport 偏移 0.01 像素，讓點陣化結果和 GL 一致（選單逐像素相同）。

兩個後端共用（`gameos_graphics.cpp`）：

- **合併繪製**：連續、狀態相同的繪製合成一次，繪製順序不變。`-no-batching` 可以關閉。
- **貼圖圖集**：32／64／128 像素的貼圖另外複製到 1024×1024 的頁面。只有在貼圖座標保證落在格子內時才使用，所以畫面完全相同。
- **`gos_RenderShape`**：直接傳入網格、矩陣和光源索引，連續的模型會合併。3D 模型的硬體路徑使用它。

成果：mc2_01 每幀的繪製次數從 242 降到 68，GL 平均幀時間從 26.7 ms 降到 11.5 ms（Mesa）。

**`-gpu-shapes`**（3D 模型在 GPU 上變換和打光）預設關閉：

- 上游版本會在右邊 100 像素處把每個模型再畫一次（「鬼影」建築）。本移植拿掉了這一份，只用 CPU 路徑，和原版相同。
- GPU 路徑已經修好，打光也移到 shader（`shaders/include/lighting.hglsl`），和 CPU 路徑的 golden 圖相差在 0.25% 以內。
- 在 Mesa 軟體繪圖上反而比較慢，要等真實 GPU 實測。

### 效能基準與 CPU 端優化

- `scripts/benchmark.py <遊戲目錄>` 用決定性模式各跑 mc2_01、mc2_07 900 幀，回報實際時間和主執行緒的 CPU 時間。
- `--exe a,b` 可以交錯執行兩個執行檔做 A/B 比較。不同時間跑的結果會差 5–10%。
- 在 Mesa 上，實際時間大多花在軟體點陣化。換成真實 GPU 時，瓶頸是主執行緒的 CPU 時間（每幀約 2.8 ms）。
- 已經做過兩輪優化，沒有剩下的熱點；遊戲邏輯的各個子系統各佔 5–10%。
- 有些 kernel 上 `perf` 不能用。這時用 gdb 取樣，計時用 `std::chrono`：固定步長模式下，遊戲自己的計時器回報的是模擬時間。

### 著色器

`shaders/` 是同一份 GLSL 原始碼，供兩種目標使用：

| 目標 | 編譯時機 | 說明 |
|---|---|---|
| OpenGL | 執行時，由驅動程式編譯 | GameOS 在前面加上 `#version 420` 與變體定義（如 `ALPHA_TEST`） |
| Vulkan / Metal / D3D12 | 建置時，`scripts/build_shaders.py` | glslangValidator 編成 SPIR-V → `spirv-val` 驗證 → spirv-cross 轉 MSL 與 HLSL → 有安裝 `dxc` 時再編成 DXIL |

建置輸出在 `<build>/shaders_out/{spirv,msl,hlsl,dxil}/<程式>.<變體>.<階段>.*`，另有 `shaders.json` 列出每個階段使用的取樣器與 uniform buffer 數量（SDL3 GPU 建立著色器時需要）。`prepare_game_dir.sh` 會把它複製到遊戲目錄的 `shaders/compiled/`。CMake 找不到 `glslangValidator` 或 `spirv-val` 時略過這一步，不影響 OpenGL 版。

撰寫規則（`shaders/include/gos_common.hglsl`）：

- 不要寫 `#version`，也不要宣告零散的 `uniform`。所有由 C++ 以名稱設定的參數（`mvp`、`wvp_`、`vp`、`fog_color`、`Foreground`…）都在共用的 `PerDraw` uniform block 裡，對應 `GameOS/gameos/render/shader_params.h` 的 `gos_render::PerDraw`。新增參數時兩邊一起改，並更新 `build_shaders.py` 的位移表。
- uniform block 用 `GOS_UBO(GL 綁定點, 階段內序號)`，貼圖用 `GOS_SAMPLER(單元)`。在 Vulkan 下它們會依 SDL3 GPU 的規則展開成 `set` / `binding`（頂點階段：貼圖 set 0、uniform set 1；片段階段：貼圖 set 2、uniform set 3）。
- 階段之間的 `in` / `out` 都要寫 `layout(location = N)`，兩個階段的編號要一致。
- `gl_Position` 經過 `gos_clip()`：OpenGL 下不變，其他目標會把深度範圍從 -1..1 轉成 0..1。
- 加上 `-shader-reload` 參數時，修改 `shaders/` 下的檔案會在遊戲中自動重新編譯（開發用，預設關閉）。

### 決策紀錄

| 問題 | 決定 |
|---|---|
| 音效遷移方式 | 直接改用 SDL3_mixer（不經過 SDL2_mixer 過渡期） |
| 著色器來源語言 | 保留 GLSL，改為 Vulkan 風格後編成 SPIR-V / MSL / HLSL（方案 A） |
| SDL3 取得方式 | 先找系統安裝的版本，找不到就自動下載並編譯固定版本（`MC2_SDL3_SOURCE=AUTO\|SYSTEM\|FETCH`） |
| 最低支援平台 | Ubuntu 24.04、Windows 10、macOS 13 |
| OpenGL 備援 | SDL3 GPU 成為預設後，OpenGL 後端保留一個版本週期再移除 |
| Viewer（模型檢視器） | 不再需要，已移除 |
| 階段 7 的停止條件 | mc2_01 每幀 draw call 降到 100 以下 |
| DXIL 輸出 | 暫緩：需要在 CI 下載 Microsoft 的 dxc 預編譯執行檔，待另行決定 |
| 新檔案的授權標頭 | 本移植新增的原始碼、腳本、著色器、CMake 與 CI 檔加上 `SPDX-License-Identifier: GPL-3.0-only`；微軟原始檔不加，維持原有聲明 |
| 網路對戰 | 列入範圍：區網聯機已完成，其餘模式見工作清單 |

### 音效輸出

- 混音器跑在裝置的取樣率（通常是 48 kHz、浮點）。沒有音效裝置時（例如測試）是 22.05 kHz。
- 每條音軌以一半音量混音，最後由 `gosLimiter` 補回來，所以加總後不會硬削波。
- 裝置緩衝區預設 2048 frames，可以用 `SDL_AUDIO_DEVICE_SAMPLE_FRAMES` 覆寫。
- 在 Windows 上的效果還要實機確認。

### 記憶體與未定義行為檢查

```bash
cmake -S . -B build-asan -DMC2_ENABLE_ASAN=ON -DCMAKE_CXX_FLAGS="-fsanitize=undefined -fno-sanitize=alignment,vptr" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=undefined"
```

```bash
cmake --build build-asan -j
```

- 執行時加 `ASAN_OPTIONS=detect_leaks=0`。原版有刻意不釋放的靜態記憶體池，所以 `unit_stuff` 的洩漏報告可以忽略。
- 加上 `-DCMAKE_BUILD_TYPE=Debug` 也會檢查 assert。Debug 版遇到 PAUSE 會中斷，除非設定了 `MC2_NO_DIALOGS`。
- 關卡、全部流程測試和聯機情境都這樣跑過，沒有錯誤。修正清單用 `git log -i --grep=asan` 查。
- ASan 的配置器需要 sized delete 和 nothrow new 的多載，遊戲已經提供。

### 其他尚未完成的項目

- `-gpu-shapes` 要不要改成預設：要等真實 GPU 實測。
- OpenGL 備援在一個發佈週期後移除（見決策紀錄）。DXIL 暫緩。
- 還沒在 Windows、macOS 實機、真實 GPU 上驗證過，也還沒做長時間遊玩測試。遊戲的 CI 只在 Linux＋Mesa 上跑。
- 編譯警告目前是 0（GCC 13），之後也要維持。
- `code/`、`mclib/` 和 `GameOS/include/platform_*.h` 裡的 `HACK`、`FIXME` 註解來自原版或上游，描述的是刻意的行為，不要動。
