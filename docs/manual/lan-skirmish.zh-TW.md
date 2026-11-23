# 區網對戰說明書

[English](lan-skirmish.md) ｜ [回到 README](../../README.zh-TW.md)

2–8 台電腦在同一個區網（或虛擬區網）上對戰，使用原版的多人地圖與大廳畫面。Windows、macOS、Linux 可以互連。

## 開始前

- 所有人裝**同一個版本**的遊戲。版本或遊戲資料不同時，加入房間會顯示版本不符。
- 只有**開房的電腦**需要允許外部連入，加入的人不用設定，見下方[防火牆](#防火牆)。

## 怎麼玩

1. 主選單 → **Multiplayer** → **Local Network**。
2. 開房的人按 **Host Game**，輸入房間名稱 → **Accept**。
3. 其他人按 **Go to LAN Browser**，選房間 → **Join**。房間列表每秒在區網廣播查詢一次，同一個區網裡的房間會自動出現，不用輸入 IP。
   - 搜不到房間（不同網段、廣播被擋）時，改選 **Internet (Direct TCP/IP)**，輸入主機的 IP → **Join Game**。
4. 房間畫面：主機選地圖（**Load Map**）與規則；每個人選隊伍、陣營，按 **Ready**。主機改任何規則都會取消大家的 Ready。
5. 全員 Ready 後主機按 **Next / Launch**，倒數 5 秒後一起進入整備（主機勾選 **Use Quickstart** 時直接用地圖預設的部隊）。

## 任務中

- 只能選取、命令自己的單位；隊友的單位可以點選查看資訊。
- 按 **P** 暫停全場：每人每場 3 次，任何人都能取消暫停。
- **Esc** 只開自己的選單，戰鬥不會停。
- **Enter** 對全體聊天，**Shift+Enter** 對隊伍聊天。
- 其他操作和單人模式相同，見[原版遊戲說明書](original-game.zh-TW.md#操作)。
- 有人掉線時，他的單位交給主機的 AI 控制；主機掉線時，所有人的任務結束。

## 自己的 IP 在哪裡看

- **Local Network** 面板（說明框下方）和 LAN 房間列表（Host Game 按鈕右邊）會顯示 `YOUR IP ADDRESS`。
- 開房後，房間畫面左下會顯示 `HOST IP ADDRESS`，別人用直接輸入 IP 時填的就是這個。
- 有多張網卡（VPN、VirtualBox、Hyper-V、Tailscale）時會列出多個位址，走預設路由的那張排第一；一般家用區網是 `192.168.x.x` 或 `10.x.x.x` 開頭的那個。
- 也可以在 Windows 的命令提示字元執行 `ipconfig`，看「IPv4 位址」。

## 防火牆

遊戲只用一個埠：**UDP 27015**（遊戲與房間探索共用）。

| 系統 | 要做什麼 |
|---|---|
| **Windows** | 安裝程式預設勾選「Allow LAN multiplayer through Windows Firewall」，安裝時會跳一次系統管理員確認，替 `mc2.exe` 加上防火牆規則（解除安裝時移除）。用 zip 版或沒勾的話，第一次開房時 Windows 會詢問，選「允許」。 |
| **Linux** | Ubuntu 預設沒開防火牆，不用設定。有開 ufw 時執行 `sudo ufw allow 27015/udp`；有開 firewalld（Fedora 預設開）時執行 `sudo firewall-cmd --add-port=27015/udp`。 |
| **macOS** | 系統防火牆預設關閉；開著的話，第一次開房會詢問是否允許連入，選「允許」。 |

## 搜不到房間或連不上

1. 確認兩台電腦連在同一個路由器或 Wi-Fi（訪客網路通常會擋住裝置之間的連線）。
2. 改用 **Internet (Direct TCP/IP)**，輸入主機畫面上的 `HOST IP ADDRESS`。
3. 檢查開房那台電腦的防火牆設定（見上表）。
4. 還是不行時，用除錯記錄啟動，把產生的 `netlog.txt` 和截圖回報給開發者（Linux 的 AppImage 把 `./mc2` 換成 AppImage 的檔名）：
   ```bash
   MC2_NET_LOG=2 ./mc2 > netlog.txt 2>&1
   ```
   Windows 在遊戲資料夾開命令提示字元，依序執行：
   ```bat
   set MC2_NET_LOG=2
   ```
   ```bat
   mc2.exe > netlog.txt 2>&1
   ```

## 跨網遊玩

不在同一個區網時，所有人裝 [Tailscale](https://tailscale.com/) 或 [ZeroTier](https://www.zerotier.com/) 等虛擬區網，再用 **Internet (Direct TCP/IP)** 輸入主機的虛擬區網 IP。

## 目前的限制

- 回收車（Salvage Craft）與把單位撤出戰場的同步尚未完成，建議在房間設定中關閉 Salvage Craft。
