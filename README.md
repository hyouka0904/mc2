# MechCommander 2 開源引擎與 Linux 移植版

[原作者網站](https://alariq.github.io/mc2-website/)

本專案是《MechCommander 2》（微軟 2001 年發行的即時戰術遊戲）的開源引擎重製與跨平台移植。
微軟在 Shared Source 授權下公開了遊戲程式碼，但其底層引擎 **GameOS** 只提供標頭檔（`.h`）而沒有實作；
本專案依照這些介面，以 **SDL3 + OpenGL** 重新實作了整個 GameOS，使遊戲能以 64 位元在 Linux 與 Windows 上執行，
並修正了許多原版遊戲本身就存在的錯誤。

- 單人戰役、任務、存讀檔、機甲實驗室等功能皆可運作。
- **網路對戰未實作。**
- 音效已可播放（含左右聲道定位 panning），但都卜勒等 3D 音效尚未支援。

> 原作者的話：因烏克蘭戰爭，作者目前無暇維護本專案，歡迎社群協助。

---

## 目錄

1. [專案架構](#專案架構)
2. [在 Linux 上建置](#在-linux-上建置)
3. [建置遊戲資料並執行](#建置遊戲資料並執行)
4. [Mod / Modding / Modpack](#mod--modding--modpack)
5. [自動化測試](#自動化測試)
6. [在 Windows 上建置](#在-windows-上建置)
7. [已知限制與待辦事項](#已知限制與待辦事項)
8. [授權](#授權)

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
│   │       └ gl/  OpenGL 實作（全專案唯一呼叫 OpenGL 的地方）        │
│   │            utils/ 為影像、矩陣、截圖、輸入腳本等工具             │
│   └ src/       Win32 API 的替代實作（字串、檔案、訊息框、時間…）    │
├──────────────────────────────────────────────────────────────┤
│ 第三方函式庫：SDL3、SDL3_mixer、SDL3_ttf、GLEW、OpenGL、zlib       │
└──────────────────────────────────────────────────────────────┘
```

### 目錄說明

| 目錄 / 檔案 | 說明 |
|---|---|
| `code/` | 遊戲主程式（約 200 個檔案）。入口在 `mechcmd2.cpp`，負責初始化、命令列解析與主迴圈。 |
| `mclib/` | 遊戲引擎核心。例如 `move.cpp`（A* 路徑搜尋）、`pqueue.cpp`（路徑搜尋用的優先佇列）、`txmmgr.cpp`（貼圖與繪製批次管理）、`abl*.cpp`（ABL 任務腳本直譯器）、`ffile.cpp` / `packet.cpp`（`.fst` / `.pak` 資料包格式）。 |
| `gui/` | 介面元件庫。 |
| `GameOS/` | 平台層，見上方分層圖。 |
| `shaders/` | GLSL 著色器，執行時從遊戲目錄的 `shaders/` 載入（修改後會自動重新載入）。 |
| `res/` | 字串資源動態函式庫 `libmc2res_64.so`，內容來自 `test_scripts/res_conv/strings.res.cpp`。 |
| `data_tools/` | 資料建置工具：`aseconv`（ASE 模型轉 TGL）、`makefst`（打包 / 解包 `.fst`）、`makersp`（產生檔案清單）、`pak`（打包 / 解包 `.pak`）。 |
| `text_tool/` | 字型烘焙工具：把 TTF 字型轉成遊戲使用的字型圖集（`.bmp`）與字元資訊（`.glyph`）。不需要顯示器。 |
| `Viewer/` | 模型檢視器（開發用）。 |
| `GameOS/gameos/mods/` | Mod 系統（`mod_system.h`：版本、manifest、Modpack、載入順序、檔案覆蓋層；`gos_mods.h`：遊戲端掛勾）。 |
| `tools/mc2mod/` | Mod 製作工具 `mc2mod`（建立、檢查、列出、解析載入順序）。 |
| `examples/mods/`、`examples/modpacks/` | 範例 Mod 與 Modpack（也用於自動測試）。 |
| `tests/` | 自動化測試（見[自動化測試](#自動化測試)）。 |
| `scripts/` | `prepare_game_dir.sh`（建置遊戲資料並組出可執行的遊戲目錄）、`smoke_test.sh`（無頭端到端測試）。 |
| `cmake/MC2Dependencies.cmake` | 取得 SDL3 / SDL3_mixer / SDL3_ttf：優先用已安裝的版本，否則自動下載固定版本並靜態編譯。 |
| `.github/workflows/ci.yml` | GitHub Actions：建置、單元測試、實際執行遊戲的冒煙測試。 |
| `test_scripts/` | 原作者移植時用的轉換腳本（字串資源轉換、檔名轉小寫等）。 |

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
| `build/out/Viewer/viewer` | 模型檢視器 |

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

另外需要 `ffmpeg` 來轉換動畫：

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

- **第一次執行**時遊戲會檢測硬體、寫入 `options.cfg` 後自動結束（原版設計），請再執行一次。
- 直接進入指定任務（跳過選單）：`./mc2 -mission mc2_01`
- 設定檔與存檔放在 `~/.mechcommander2/`（若有設定 `XDG_CONFIG_HOME` 則放在該目錄下）。

---

## Mod / Modding / Modpack

| 名詞 | 在本專案中的意義 |
|---|---|
| **Mod**（遊戲模組） | 一個資料夾：`mod.ini`（描述）＋ `content/`（內容）。`content/` 的目錄結構與遊戲目錄相同，裡面的檔案會**取代**遊戲原本的檔案（包括打包在 `.fst` 裡的檔案）或**新增**檔案。 |
| **Modding**（製作模組） | 用 `mc2mod` 工具建立、檢查、除錯 Mod；或在程式中使用 `mods/mod_system.h` 的 API。 |
| **Modpack**（模組包） | 一個 `modpack.ini`：列出要啟用的 Mod 與版本要求，可覆寫優先序，也可以在旁邊的 `mods/` 資料夾自帶 Mod。 |

### 能改什麼

《MechCommander 2》的遊戲內容幾乎都是資料檔，所以 Mod 可以修改或新增：

| 內容 | 路徑（`content/` 底下） |
|---|---|
| 機甲、載具、武器、駕駛員等數值（物品） | `data/objects/*.csv`、`*.fit` |
| 任務與地圖（新增任務後可用 `-mission <名稱>` 直接啟動） | `data/missions/`、`data/terrain/` |
| 遊戲邏輯與 AI（ABL 腳本） | `data/missions/*.abl`、`data/missions/warriors/` 等 |
| 介面版面與圖片 | `data/art/*.fit`、`*.tga` |
| 音效、字型、著色器 | `data/sound/`、`assets/graphics/`、`shaders/` |

檔名不分大小寫（遊戲開檔前一律轉小寫），`\` 與 `/` 視為相同。

### Mod 的結構

```
mods/
└── laser_rebalance/
    ├── mod.ini
    └── content/
        └── data/objects/...
```

```ini
[mod]
id = laser_rebalance          ; 小寫英數與 _ . -，必須唯一
name = Laser Rebalance
version = 1.2.0
author = 你的名字
description = 調整雷射武器數值
priority = 0                  ; 數字越大越晚載入，同名檔案以晚載入者為準
enabled = true                ; 未使用 Modpack 時是否啟用

[dependencies]                ; 必須存在的 Mod 與版本要求
core_tweaks = ^1.0

[optional]                    ; 存在時才影響載入順序
fancy_ui = >=2.0

[conflicts]                   ; 不能同時使用的 Mod（與版本）
old_lasers = *
```

版本要求語法：`*`、`1.2.3`（完全相同）、`>=1.0`、`>1.0`、`<=2`、`<2`、`^1.2`（≥1.2.0 且 <2.0.0）、
`~1.2`（≥1.2.0 且 <1.3.0），多個條件以逗號連接表示「且」：`>=1.0, <1.5`。

### 載入順序規則

1. 一個 Mod 一定在它所依賴的 Mod **之後**載入（依賴未被選取但已安裝時會自動加入）。
2. 其餘依 `priority` 由小到大，相同時依 id 字母順序。
3. 多個 Mod 提供同一個檔案時，**最後載入者勝出**。
4. 找不到依賴、版本不符、互相衝突、循環依賴時，遊戲會列出原因並以結束碼 3 結束，不會帶著錯誤設定執行。

### Modpack

```ini
[modpack]
id = hardcore
name = Hardcore Pack
version = 1.0.0

[mods]                        ; 要啟用的 Mod = 版本要求（只啟用這些）
laser_rebalance = ^1.2
core_tweaks = *

[priority]                    ; 選用：覆寫個別 Mod 的優先序
laser_rebalance = 50
```

Modpack 資料夾中若有 `mods/` 子資料夾，裡面的 Mod 會優先於全域 Mod 目錄中同 id 的版本。

### 在遊戲中使用

| 方式 | 效果 |
|---|---|
| 遊戲目錄下有 `mods/` 資料夾 | 自動啟用其中 `enabled = true` 的所有 Mod |
| `./mc2 -mods <資料夾>` | 使用指定資料夾中啟用的 Mod |
| `./mc2 -mods <資料夾> -modpack <modpack.ini>` | 只啟用 Modpack 列出的 Mod |

啟用時，遊戲啟動會在 stderr 印出 `MODS:` 開頭的載入順序與檔案覆蓋清單。

### 製作工具 mc2mod

建置後位於 `build/out/tools/mc2mod/mc2mod`：

```bash
mc2mod new mods laser_rebalance "Laser Rebalance"
```

```bash
mc2mod validate mods/laser_rebalance
```

```bash
mc2mod list mods
```

```bash
mc2mod resolve mods --pack packs/hardcore/modpack.ini
```

```bash
mc2mod which mods data/objects/pilots.csv
```

```bash
mc2mod new-pack packs/hardcore/modpack.ini hardcore laser_rebalance=^1.2 core_tweaks
```

| 指令 | 說明 |
|---|---|
| `new` | 建立 Mod 骨架（`mod.ini`、`content/data/...`、README） |
| `validate` | 檢查 manifest、`content/` 是否為空、是否有只差大小寫的檔名 |
| `list` | 列出已安裝的 Mod |
| `resolve` | 顯示載入順序、被多個 Mod 提供的檔案與所有錯誤（有錯誤時結束碼為 1） |
| `which` | 查詢某個遊戲檔案最終由哪個 Mod 提供、覆蓋了誰 |
| `new-pack` | 產生 Modpack manifest |

### 程式介面

- `GameOS/gameos/mods/mod_system.h`（命名空間 `mc2mod`）：`Version` / `VersionReq`、`ModManifest`、`ModpackManifest`、
  `ModRegistry`、`resolveLoadOrder()`、`Overlay`（虛擬檔案系統）、`describe()`、`createModSkeleton()`、`validateMod()`。
  不依賴遊戲其他部分，可給工具或其他程式使用。
- `GameOS/gameos/mods/gos_mods.h`：遊戲端介面 `gos_ModsInit()`、`gos_ModResolvePath()`、`gos_ModsShutdown()`。
  `mclib/file.cpp` 的 `File::open()` 與 `fileExists()` 在讀檔前會先問覆蓋層，因此所有透過遊戲檔案層讀取的資料都能被 Mod 取代。

### 範例

`examples/mods/` 有兩個範例 Mod，都替換主選單的背景貼圖 `data/art/mcl_mm_1.tga`（原檔在 `art.fst` 內）：
`red_main_menu`（紅）與 `blue_main_menu`（藍，`priority = 5`）。兩者同時啟用時藍色勝出；
`examples/modpacks/red_menu_pack` 以 `[priority]` 覆寫，讓紅色勝出。

```bash
./mc2 -mods ../examples/mods
```

```bash
./mc2 -mods ../examples/mods -modpack ../examples/modpacks/red_menu_pack/modpack.ini
```

### 測試

| 測試 | 內容 |
|---|---|
| `unit_mods` | 18 個案例：版本與版本要求、INI 解析、manifest 錯誤回報、Modpack、依賴排序、自動帶入依賴、缺少依賴 / 版本不符 / 循環 / 衝突、選用依賴、覆蓋層與大小寫、骨架與檢查、遊戲端掛勾 |
| `tool_mc2mod` | 以 `mc2mod` 走完整個製作流程（建立 → 加內容 → 檢查 → 加依賴 → 解析 → 查詢 → 錯誤的 Modpack），並檢查範例 |
| 截圖比對 `main_menu_mods` / `main_menu_modpack` | 實際啟動遊戲載入範例 Mod 與 Modpack，確認畫面上的貼圖分別被換成藍色與紅色 |

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
| `unit_sound` | 音效 API（SDL3_mixer）：播放一次 / 迴圈 / 暫停 / 停止、音量線性、左右聲道定位、釋放資源；不需要音效卡 |
| `unit_render_state` | 由 GameOS 渲染狀態推出的管線描述、32 位元管線鍵（可逆、互不重複）、管線快取 |
| `render_isolation` | 檢查 OpenGL 只出現在 `GameOS/gameos/render/gl/` |
| `unit_mods` / `tool_mc2mod` | Mod 系統與製作工具（詳見 Mod 章節） |
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

若遊戲提前結束（當機）、跳出訊息框、出現 OpenGL 錯誤、不回應 SIGTERM，或畫面全黑，測試就會失敗。
結果截圖會存為 `game/smoke.png`。

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
| `-perf-csv <file>` | 每幀寫一行統計：幀號、實際耗時（毫秒）、draw call 數、管線狀態切換次數、出現過的不同管線數 |
| `-mods <dir>` / `-modpack <file>` | 啟用 Mod / Modpack（見 [Mod / Modding / Modpack](#mod--modding--modpack)） |
| `-windowed` | 忽略遊戲的全螢幕設定，一律以視窗執行（全螢幕行為取決於視窗管理員，會讓截圖因環境而異） |

測試在 800×600 的 Xvfb 畫面上以 `-windowed` 執行。`tests/golden/manifest.txt` 列出 18 張基準圖：4 個任務各 3 幀、主選單、戰役選擇、Solo Mission 畫面、
暫停中的任務，以及載入範例 Mod / Modpack 的主選單。執行與比對：

```bash
sudo apt-get install python3-pil python3-numpy
```

```bash
scripts/run_screenshot_tests.py game shots
```

任一通道差異大於 8/255 的像素算「不同」，不同像素達 0.5% 即失敗，並在 `shots/diff/` 輸出熱度圖。
畫面刻意變更（例如修改 shader）後，用 `--update` 重新產生基準圖並一起提交。
單獨比較兩張圖或兩個資料夾可用 `scripts/compare_screenshots.py`。

### 持續整合

`.github/workflows/ci.yml` 在每次 push / PR 時執行：

1. 以 RelWithDebInfo 與 Debug 建置並執行全部 CTest 測試；
2. 下載遊戲資料、建置資料，並以冒煙測試實際遊玩 `mc2_01` 60 秒；
3. 執行截圖比對測試，上傳截圖、熱度圖與記錄檔。

---

## 在 Windows 上建置

> 本次更新尚未在 Windows 上實機驗證（CI 只跑 Linux）。

SDL3 系列會由 CMake 自動下載並靜態編譯（與 Linux 相同），只需自行準備 zlib 與 GLEW，
並以 `CMAKE_PREFIX_PATH` 指向它們：

```
3rdparty
├── include
│   ├── zconf.h, zlib.h
│   └── GL/      (glew.h, wglew.h …)
└── lib
    └── x64/     (glew32.lib, zlib.lib 及對應 dll)
```

產生 Visual Studio 64 位元專案（版本依安裝的 Visual Studio 調整）：

```bat
cmake.exe -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=c:/path_to_your_dependencies/ ..
```

字串資源 `libmc2res` 會跟主程式一起建置，不需要另外執行 `res_conv.pl`。

---

## 已知限制與待辦事項

尚未完成的項目（原始碼中保留了對應的 `TODO` 註解）：

- **網路對戰**未實作。
- **多貼圖模型的 GPU 繪製**：`mclib/tgl.cpp` 的硬體繪製路徑以整個模型的第一張貼圖繪製，
  若模型使用多張貼圖會不正確；需要在載入時依貼圖分段並支援繪製索引範圍。
- **光照完全移到著色器**：目前部分光照仍在 CPU 計算（`mclib/msl.cpp`、`shaders/gos_tex_vertex_lighted.vert`）。
- **記憶體堆積追蹤**：`gos_Malloc` 尚未依 heap 記錄配置，因此 heap 銷毀時不會釋放其記憶體（`GameOS/gameos/gameos.cpp`）。
- 減少 draw call 數量、GameOS 繪圖 API 的型別安全（`gameos_graphics.cpp` 中的設計註解）。
- 都卜勒等 3D 音效。
- `GameOS/include/platform_*.h` 中的 `FIXME` 來自 Wine 的 Win32 標頭，屬於上游註解。

`code/`、`mclib/` 中大量標註為 `HACK` 的註解是微軟 2001 年原始碼的開發紀錄，描述的是刻意的遊戲行為（AI、網路封包、地圖格子等），
修改會改變遊戲玩法，因此保留原樣。

---

## 授權

- 原始遊戲程式碼以 Shared Source Limited Permission License 釋出，請見 `EULA.txt`。
- 本專案作者撰寫的程式碼以 GPL v3 授權，請見 `license.txt`。
