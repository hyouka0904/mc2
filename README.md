# MechCommander 2 開源引擎與 Linux 移植版

[原作者網站](https://alariq.github.io/mc2-website/)

本專案是《MechCommander 2》（微軟 2001 年發行的即時戰術遊戲）的開源引擎重製與跨平台移植。
微軟在 Shared Source 授權下公開了遊戲程式碼，但其底層引擎 **GameOS** 只提供標頭檔（`.h`）而沒有實作；
本專案依照這些介面，以 **SDL2 + OpenGL** 重新實作了整個 GameOS，使遊戲能以 64 位元在 Linux 與 Windows 上執行，
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
4. [自動化測試](#自動化測試)
5. [在 Windows 上建置](#在-windows-上建置)
6. [已知限制與待辦事項](#已知限制與待辦事項)
7. [授權](#授權)

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
│   ├ gameos/    以 SDL2 / OpenGL / SDL_mixer 實作繪圖、輸入、音效、  │
│   │            檔案、字型、除錯；utils/ 為著色器、影像、矩陣等工具   │
│   └ src/       Win32 API 的替代實作（字串、檔案、訊息框、時間…）    │
├──────────────────────────────────────────────────────────────┤
│ 第三方函式庫：SDL2、SDL2_mixer、SDL2_ttf、GLEW、OpenGL、zlib       │
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
| `tests/` | 自動化測試（見[自動化測試](#自動化測試)）。 |
| `scripts/` | `prepare_game_dir.sh`（建置遊戲資料並組出可執行的遊戲目錄）、`smoke_test.sh`（無頭端到端測試）。 |
| `cmake/sdl2/` | 尋找 SDL2 系列函式庫用的 CMake 模組。 |
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
sudo apt-get install build-essential cmake libsdl2-dev libsdl2-mixer-dev libsdl2-ttf-dev libglew-dev libgl-dev zlib1g-dev
```

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

`tests/golden/manifest.txt` 列出 16 張基準圖：4 個任務各 3 幀、主選單、戰役選擇、Solo Mission 畫面、
暫停中的任務。執行與比對：

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

> 以下為原作者的說明，本次更新未在 Windows 上重新驗證。

使用 CMake，並準備好 SDL2、SDL2_mixer、SDL2_ttf、zlib、GLEW。建議自行從原始碼編譯 zlib（記得複製 `zconf.h`）。
相依套件目錄結構範例：

```
3rdparty
├── include
│   ├── zconf.h, zlib.h
│   ├── GL/      (glew.h, wglew.h …)
│   └── SDL2/    (SDL 與 SDL_mixer 標頭檔)
└── lib
    ├── x64/     (glew32.lib, SDL2.lib, SDL2main.lib, SDL2_mixer.lib, zlib.lib 及對應 dll)
    └── x86/
```

產生 Visual Studio 2017 64 位元專案：

```bat
cmake.exe -G "Visual Studio 15 2017 Win64" -DCMAKE_PREFIX_PATH=c:/path_to_your_dependencies/ -DCMAKE_LIBRARY_ARCHITECTURE=x64 ..
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
