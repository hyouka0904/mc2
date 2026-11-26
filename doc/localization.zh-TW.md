# 繁中補丁預覽

預覽包含主選單、第一關〈偵察巡邏〉的簡報、任務名稱和目標。缺少翻譯的文字沿用英文。

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

`localization/zh-TW/strings.txt` 每行使用資源 ID、Tab 和 UTF-8 譯文。用 `\n` 表示換行，修改後重新執行產生工具，讓字型包含新增的字。用詞參考[繁中譯名](../localization/zh-TW/glossary.md)。

預覽的字型使用單頁貼圖，字數上限由工具依字型尺寸檢查。遊戲內語言選項、中文輸入法及其餘關卡翻譯待後續。
