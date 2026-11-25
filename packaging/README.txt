MechCommander 2 - unofficial port
=================================

Free and non-commercial. Not affiliated with or endorsed by Microsoft.
Independent fork of alariq/mc2 (https://github.com/alariq/mc2); not
affiliated with or endorsed by its author, alariq.
Licenses: see the "licenses" folder.

Starting the game
-----------------
Windows   Start menu > MechCommander 2, or mc2.exe in the game folder.
          Windows SmartScreen may warn about an unknown publisher (the
          program is not code-signed): "More info" > "Run anyway".
macOS     Drag "MechCommander 2" to Applications and open it. The app is not
          notarized: the first time, macOS refuses to open it; go to System
          Settings > Privacy & Security and click "Open Anyway".
Linux     AppImage: chmod +x MechCommander2-*.AppImage and run it
          (needs FUSE 2: package libfuse2 / libfuse2t64).
          Tarball: run ./mc2 in the extracted folder.

Saved games and settings
------------------------
Windows   %APPDATA%\MechCommander2\
macOS     ~/Library/Application Support/MechCommander2/
Linux     ~/.local/share/MechCommander2/

Uninstalling the game does not delete them.

Command line
------------
mc2 -version            print the version
mc2 -renderer gl        use OpenGL instead of the default (Vulkan / D3D12 /
                        Metal through SDL3 GPU), e.g. for old graphics drivers
mc2 -windowed           stay in a window
mc2 -mods <dir>         load mods (see the project README)
mc2 -noupdatecheck      do not look for a new version

New versions
------------
At start the game asks GitHub whether a newer version has been released
(at most once a day; nothing about you or your computer is sent). If there
is one, the main menu shows "NEW VERSION ... AVAILABLE" in the bottom left
corner. Click it and choose UPDATE: the game downloads the new version,
checks it, installs it and starts again (saved games and settings are
kept). On macOS it opens the downloaded disk image; drag the app to
Applications. If that is not possible it opens the download page instead.
Options > Gameplay > "Receive test versions" also offers unreleased test
builds (they may have bugs). To turn the check off for good, put check=0
in update.ini in the saved games folder above.

-------------------------------------------------------------------------

MechCommander 2 非官方移植版
==========================

免費、非商業發佈，與微軟無關，也未經微軟認可。授權條款請見 licenses 資料夾。
本專案是 alariq/mc2（https://github.com/alariq/mc2）的獨立 fork，
與原作者 alariq 無關，也未經其認可。

啟動
----
Windows   開始選單 > MechCommander 2，或執行遊戲資料夾中的 mc2.exe。
          程式沒有數位簽章，SmartScreen 可能顯示警告：按「其他資訊」>「仍要執行」。
macOS     把「MechCommander 2」拖到「應用程式」後開啟。程式未經 Apple 公證，第一次
          會被擋下：到「系統設定」>「隱私權與安全性」按「強制打開」。
Linux     AppImage：chmod +x MechCommander2-*.AppImage 後執行（需要 FUSE 2，
          套件 libfuse2 或 libfuse2t64）。壓縮檔：解壓後執行資料夾中的 ./mc2。

存檔與設定
----------
Windows   %APPDATA%\MechCommander2\
macOS     ~/Library/Application Support/MechCommander2/
Linux     ~/.local/share/MechCommander2/

解除安裝不會刪除存檔。

命令列參數
----------
mc2 -version            顯示版本
mc2 -renderer gl        改用 OpenGL（預設為 SDL3 GPU：Vulkan / D3D12 / Metal），
                        適合舊的顯示卡驅動程式
mc2 -windowed           以視窗模式執行
mc2 -mods <資料夾>      載入 Mod（說明見專案 README）
mc2 -noupdatecheck      不檢查新版本

新版本
------
遊戲啟動時會向 GitHub 查詢有沒有新版本（一天最多一次，不會送出任何關於你
或你電腦的資料）。有新版時，主選單左下角會顯示「NEW VERSION ... AVAILABLE」，
點一下再選 UPDATE：遊戲會下載新版、驗證、安裝後重新啟動（存檔和設定都保留）。
macOS 會打開下載好的磁碟映像，請把 app 拖到「應用程式」。無法自動更新時改為
打開下載頁面。選項 > Gameplay 勾選「Receive test versions」也會收到尚未正式
發佈的測試版（可能有問題）。想永久關閉檢查，在上面的存檔資料夾裡的 update.ini
寫入 check=0。
