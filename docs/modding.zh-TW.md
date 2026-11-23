# Mod / Modding / Modpack

[English](modding.md) ｜ [回到 README](../README.zh-TW.md)

| 名詞 | 在本專案中的意義 |
|---|---|
| **Mod**（遊戲模組） | 一個資料夾：`mod.ini`（描述）＋ `content/`（內容）。`content/` 的目錄結構與遊戲目錄相同，裡面的檔案會**取代**遊戲原本的檔案（包括打包在 `.fst` 裡的檔案）或**新增**檔案。 |
| **Modding**（製作模組） | 用 `mc2mod` 工具建立、檢查、除錯 Mod；或在程式中使用 `mods/mod_system.h` 的 API。 |
| **Modpack**（模組包） | 一個 `modpack.ini`：列出要啟用的 Mod 與版本要求，可覆寫優先序，也可以在旁邊的 `mods/` 資料夾自帶 Mod。 |

## 能改什麼

《MechCommander 2》的遊戲內容幾乎都是資料檔，所以 Mod 可以修改或新增：

| 內容 | 路徑（`content/` 底下） |
|---|---|
| 機甲、載具、武器、駕駛員等數值（物品） | `data/objects/*.csv`、`*.fit` |
| 任務與地圖（新增任務後可用 `-mission <名稱>` 直接啟動） | `data/missions/`、`data/terrain/` |
| 遊戲邏輯與 AI（ABL 腳本） | `data/missions/*.abl`、`data/missions/warriors/` 等 |
| 介面版面與圖片 | `data/art/*.fit`、`*.tga` |
| 音效、字型、著色器 | `data/sound/`、`assets/graphics/`、`shaders/` |

檔名不分大小寫（遊戲開檔前一律轉小寫），`\` 與 `/` 視為相同。

## Mod 的結構

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

## 載入順序規則

1. 一個 Mod 一定在它所依賴的 Mod **之後**載入（依賴未被選取但已安裝時會自動加入）。
2. 其餘依 `priority` 由小到大，相同時依 id 字母順序。
3. 多個 Mod 提供同一個檔案時，**最後載入者勝出**。
4. 找不到依賴、版本不符、互相衝突、循環依賴時，遊戲會列出原因並以結束碼 3 結束，不會帶著錯誤設定執行。

## Modpack

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

## 在遊戲中使用

| 方式 | 效果 |
|---|---|
| 遊戲目錄下有 `mods/` 資料夾 | 自動啟用其中 `enabled = true` 的所有 Mod |
| `./mc2 -mods <資料夾>` | 使用指定資料夾中啟用的 Mod |
| `./mc2 -mods <資料夾> -modpack <modpack.ini>` | 只啟用 Modpack 列出的 Mod |

啟用時，遊戲啟動會在 stderr 印出 `MODS:` 開頭的載入順序與檔案覆蓋清單。

## 製作工具 mc2mod

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

## 程式介面

- `GameOS/gameos/mods/mod_system.h`（命名空間 `mc2mod`）：`Version` / `VersionReq`、`ModManifest`、`ModpackManifest`、
  `ModRegistry`、`resolveLoadOrder()`、`Overlay`（虛擬檔案系統）、`describe()`、`createModSkeleton()`、`validateMod()`。
  不依賴遊戲其他部分，可給工具或其他程式使用。
- `GameOS/gameos/mods/gos_mods.h`：遊戲端介面 `gos_ModsInit()`、`gos_ModResolvePath()`、`gos_ModsShutdown()`。
  `mclib/file.cpp` 的 `File::open()` 與 `fileExists()` 在讀檔前會先問覆蓋層，因此所有透過遊戲檔案層讀取的資料都能被 Mod 取代。

## 範例

`examples/mods/` 有兩個範例 Mod，都替換主選單的背景貼圖 `data/art/mcl_mm_1.tga`（原檔在 `art.fst` 內）：
`red_main_menu`（紅）與 `blue_main_menu`（藍，`priority = 5`）。兩者同時啟用時藍色勝出；
`examples/modpacks/red_menu_pack` 以 `[priority]` 覆寫，讓紅色勝出。

```bash
./mc2 -mods ../examples/mods
```

```bash
./mc2 -mods ../examples/mods -modpack ../examples/modpacks/red_menu_pack/modpack.ini
```

## 測試

| 測試 | 內容 |
|---|---|
| `unit_mods` | 18 個案例：版本與版本要求、INI 解析、manifest 錯誤回報、Modpack、依賴排序、自動帶入依賴、缺少依賴 / 版本不符 / 循環 / 衝突、選用依賴、覆蓋層與大小寫、骨架與檢查、遊戲端掛勾 |
| `tool_mc2mod` | 以 `mc2mod` 走完整個製作流程（建立 → 加內容 → 檢查 → 加依賴 → 解析 → 查詢 → 錯誤的 Modpack），並檢查範例 |
| 截圖比對 `main_menu_mods` / `main_menu_modpack` | 實際啟動遊戲載入範例 Mod 與 Modpack，確認畫面上的貼圖分別被換成藍色與紅色 |
