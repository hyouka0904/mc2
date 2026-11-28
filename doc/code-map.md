# 功能與檔案索引

先查相關條目，再用 `rg -n` 確認符號、呼叫端和測試。下表的程式路徑皆從 repo 根目錄起算。建置和實機測試準備見[開發說明](DEVELOPMENT.zh-TW.md)，未實作項目的依賴見[roadmap](ROADMAP_MULTIPLAYER_ZH.md)。

| 功能 | 入口與定位 | 狀態來源與連動 | 驗證 |
|---|---|---|---|
| 聯機開房、加入與模式入口 | `code/multplyr.cpp` 的 `MultiPlayer::hostSession`、`joinSession`；`code/mpconnectiontype.cpp`、`code/mpgamebrowser.cpp`、`code/mpparameterscreen.cpp` | 主機維護房間與名單。訊息結構在 `code/multplyr.h`，傳輸在 `GameOS/gameos/net/`，進關同步在 `code/multplyr_sync.cpp`。改封包格式時同步檢查協定版本。 | `unit_net`、LAN 測試與 `--scenario version` |
| 地圖與關卡載入 | `code/mission.cpp` 的 `Mission::init`；`mclib/terrain.cpp` 的 `Terrain::init` | Mission 讀取關卡設定與地圖封包，再初始化地形、物件和目標。多人地圖載入畫面在 `code/mploadmap.cpp`。新增地圖時一起驗證載入與尋路。 | 冒煙測試與 LAN 測試 |
| 任務目標 | `code/objective.cpp` 的 `CObjectives::Read`、`Status`、`Save` | 目標由任務設定載入到陣營，`code/team.cpp` 查詢狀態，`code/missiongui.cpp` 與 `code/controlgui.cpp` 讀取並顯示。調整判定時一起確認完成條件、獎勵和結算。 | 實際完成一個目標並確認 HUD、獎勵及結算 |
| 房間準備、部署與聊天 | `code/mpparameterscreen.cpp`、`code/missionbegin.cpp`、`code/pilotreadyscreen.cpp`、`code/chatwindow.cpp` | `code/multplyr.cpp` 核對整備狀態和預算、從房間啟動載入握手並轉送聊天。`code/multplyr.h` 定義收件人及訊息格式。 | LAN 整備與開局、多人收件人檢查 |
| System 選單（右上角） | `code/logisticsmenu.{h,cpp}` 的 `LogisticsMenu`、`LogisticsLeaveDialog`；由 `code/missionbegin.cpp` 的 MAINMENU 分支開啟 | 按鈕沿用 `mcl_mm.fit` 94–97，確認框用 `mcl_dialog.fit`（Esc、點框外 = 取消）。回到主選單：戰役問要不要存（寫 `savegame/Autosave.fit`），聯機問要不要離開房間（`leaveToTitle` 釋放 MPlayer），Solo 直接離開。關閉時呼叫原畫面的 `begin()`。 | 流程 `main_menu`、聯機 `--scenario away` |
| 房間準備與暫離 | `code/mpparameterscreen.cpp` 的 `setPresent`、`preparationValid`、`toggleAway` | 只有整備有效、人在房間畫面、沒暫離時才是 ready。開 System 選單、Player Prefs、Add AI 時取消 ready，回來時自動恢復。`MC2Player::away` 由主機接收，`allPlayersReady()` 遇到暫離回傳 false。`canReadyForMap()` 依地圖名稱快取。 | 聯機 `--scenario away`、`logistics` |
| 本機 IP 顯示 | `code/lanaddresses.cpp`；`GameOS/gameos/net/gos_net.cpp` 的 `gos_NetLocalInterfaces` | 三個大廳畫面共用。只顯示預設路由的位址加「(+N)」，滑鼠移上去時列出全部位址和網卡名稱。搜房會對每張網卡的子網廣播位址送查詢。 | `unit_net` |
| 更新檢查與通知 | `GameOS/gameos/update/gos_update.cpp`；`code/mainmenu.cpp` | 更新模組維護版本、快取與頻道；「稍後」只隱藏到下次啟動（`MainMenu::bUpdateLater`）。`code/optionsarea.cpp` 設定測試版頻道，下面有常駐更新按鈕（`OptionsGamePlay::updateButtonText`），安裝由 `gos_update_install.cpp` 處理；提示框的預計下載量來自 `startEstimate()`（背景讀 manifest、比對已裝檔案），下載中顯示 `downloadBytes()` 的已下載／總量。聯機版本握手走 `code/multplyr.cpp`。 | `unit_update`、LAN `--scenario version` |
| Mod 載入與執行中生成／回收 | `GameOS/gameos/mods/`、`tools/mc2mod/`；`code/ablmc2.cpp` 的 `execCreateInfantry` | Mod 資料載入與 ABL 執行是兩個入口。單位生成和移除走 `code/mission.cpp` 的 `Mission::addMover`、`removeMover`，重用槽位要一起檢查 `code/objmgr.cpp` 及感測器、外觀、無線電和移動資源。 | `unit_mods`、`tool_mc2mod`；用關卡腳本反覆生成與移除，檢查槽位及檔案描述符 |
| 資源點 HUD 與多人同步 | `code/logisticsdata.cpp` 的 `LogisticsData::addResourcePoints`、`decrementResourcePoints`、`syncResourcePoints` | `code/controlgui.cpp` 顯示、預扣及退回點數，`code/multplyr_sync.cpp` 交換變動並套用主機快照，`code/saveload.cpp` 處理存讀檔。 | 用主客端各自確認支援指令預扣與取消，並檢查低解析度 HUD |
| 維修命令與脫困 | `code/warrior.cpp` 的 `MechWarrior::orderRefit`；`code/missiongui.cpp` | UI 下令後由單位執行維修，檢查脫困移動與替換命令時的維修預約和目標狀態。維修命令的續行判定在 `code/tacordr.cpp`，可搜尋 `TACTICAL_ORDER_REFIT`。 | 令維修車在卡住並脫困後繼續維修，再下替換命令確認取消 |
| 繁中預覽 | `localization/zh-TW/strings.txt`、`scripts/build_zh_tw_patch.py`、`scripts/check_translations.py`；`res/libmain.cpp` | `code/optionsarea.cpp` 提供語言選項，立即重啟使用 `GameOS/gameos/update/gos_update_install.cpp` 的關閉後重啟流程。`code/prefs.cpp` 在使用者目錄保存選擇，`code/mechcmd2.cpp` 啟動時將語言交給 `GameOS/gameos/gameos_res.cpp`。字串用原版資源 ID 覆寫。 `GameOS/gameos/gos_font.cpp` 讀取擴充字形，`gameos_graphics.cpp` 解碼、斷行與繪製，`gui/afont.cpp` 測量簡報清單的行高。 `code/missionbriefingscreen.cpp` 與 `code/mechlopedia.cpp` 保留文字縮排及捲軸空間，`code/mploadmap.cpp` 處理原版直接儲存的「2of3」顯示名稱。 | 按[繁中補丁文件](localization.zh-TW.md)產生補丁，核對設定、戰役、戰鬥與多人房間，並比較預設英文截圖 |

測試入口（從 repo 根目錄執行，`build` 與 `game` 需先依開發說明準備）：

```sh
ctest --test-dir build -R 'unit_net|unit_update|unit_mods|tool_mc2mod' --output-on-failure
timeout 900 python3 scripts/run_net_test.py game
timeout 900 python3 scripts/run_net_test.py game --scenario version
scripts/smoke_test.sh game mc2_01 60
```
