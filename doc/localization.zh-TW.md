# 繁中補丁預覽

目前涵蓋主選單、設定、存讀檔、任務選擇、戰前部署、戰鬥指令、結算、百科介面、多人房間及更新提示。戰役與訓練關卡的標題和概要已翻譯，第一關〈偵察巡邏〉也包含完整簡報與目標。未收錄的原版文字沿用英文。

## 準備

1. 從繁中工作分支建置遊戲並準備獨立的遊戲目錄，步驟見[開發說明](DEVELOPMENT.zh-TW.md)。使用既有 Windows 測試版當作資料來源時，請在測試版的獨立副本中換上這個分支編譯的 `mc2.exe` 與 `mc2res_64.dll`。
2. 安裝 Python 的 Pillow，並準備 Noto Sans TC 的 OTF 或 TTF 字型。Noto Sans TC 採 SIL Open Font License 1.1。預覽由使用者提供字型檔。
3. 在原始碼目錄執行下列指令。工具讀取遊戲目錄內的原版點陣字型，在 `localization/zh-TW/` 產生翻譯及字型。

```sh
python3 -m pip install Pillow
python3 scripts/build_zh_tw_patch.py game /path/to/NotoSansTC-Regular.otf
```

使用 Linux 的 Noto Sans CJK 字型集合時，可指定其中的 TC 字型。

```sh
python3 scripts/build_zh_tw_patch.py game /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc --face-index 3
```

## 啟動

Linux，從遊戲目錄啟動。

```sh
cd game
MC2_LANGUAGE=zh-TW timeout 120 ./mc2
```

Windows PowerShell，從含有 `mc2.exe` 的遊戲目錄啟動。

```powershell
$env:MC2_LANGUAGE = 'zh-TW'
.\mc2.exe
```

移除 `MC2_LANGUAGE` 後重啟，遊戲會以英文啟動，預覽資源可以留在原處。PowerShell 可用 `Remove-Item Env:MC2_LANGUAGE`。

## 編輯翻譯

`localization/zh-TW/strings.txt` 每行使用資源 ID、Tab 和 UTF-8 譯文。用 `\n` 表示換行。保留原文的格式參數（如 `%s` 和 `%ld`）及排列順序，產生工具會比對這些參數。修改後重新執行產生工具，讓字型包含新增的字。用詞參考[繁中譯名](../localization/zh-TW/glossary.md)。

預覽的字型使用單頁貼圖，字數上限由工具依字型尺寸檢查。遊戲內語言選項、中文輸入法、其餘任務的完整簡報與目標，以及百科內文待後續。原始資料檔中的部分名稱和貼圖文字仍為英文。
