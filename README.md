# AutoRestock

[English](#english) · [繁體中文](#繁體中文)

## English

Automatic main-hand restocking for Minecraft Bedrock. Keep building when your stack runs low, and replace a broken tool with a matching spare from your inventory.

A client-side native DLL loaded through LeviLauncher. No behavior pack, server plugin, or companion protocol is required.

[Download 0.3.1](https://github.com/XuanDiNa/AutoRestock/releases/tag/v0.3.1) · [Report an issue](https://github.com/XuanDiNa/AutoRestock/issues)

### Features

- **Automatic refill:** move matching items into the main hand when the remaining stack falls below your configured threshold.
- **Broken-tool replacement:** equip a matching spare after a tool breaks. The spare keeps its own remaining durability; tools are not repaired.
- **Main inventory only:** use the 27 main-inventory slots as reserves. Other hotbar slots and the offhand are left alone.
- **Item-data matching:** check names, enchantments, tags, and components. Tool durability is ignored only when comparing copies to find a spare.
- **Simple settings:** enable or disable restocking and choose the stack threshold in JSON, with automatic reload and no hotkey.

Refilling needs a suitable reserve and an eligible gameplay context. It does not create items or guarantee uninterrupted use at an empty stack.

### Requirements

- Windows x64
- Minecraft **1.26.45.01 / 1.26.51.01 / 1.26.52.03**
- LeviLauncher with native DLL plugin support and the Microsoft Visual C++ x64 runtime

One DLL supports these three exact game versions. Other builds are not supported; the plugin stays inactive if its version checks fail.

### Installation

1. Fully close Minecraft. If upgrading, back up the old plugin **outside the mods directory**.
2. Download the plugin ZIP from [Releases](https://github.com/XuanDiNa/AutoRestock/releases). The automatic source-code archives are not installable packages.
3. Create `mods/AutoRestock/` in the selected game version's directory and extract the ZIP's seven files into it.
4. When upgrading, keep your existing `config.json`, logs, and disabled `.close` filenames. Use the bundled default config only for a first installation.

Do not replace DLLs while Minecraft is running or load old and new copies together. To roll back, close the game and restore the old DLL and metadata from your backup. This plugin cannot be imported as a `.mcpack`.

### Configuration

Edit `config.json` beside the DLL:

```json
{
  "enabled": true,
  "preRestockThreshold": 17
}
```

`enabled` toggles restocking. `preRestockThreshold` accepts 1–64 and uses a strict "less than" comparison: **17 means refill at 16 items or fewer**. This is a stack-count threshold, not a tool-durability threshold.

Settings reload automatically, usually within 500 ms. Invalid edits keep the last valid settings. See the [default config](config.json) and [setting descriptions](config.schema.json). Release downloads include archive and per-file SHA-256 checksums.

### Notes and feedback

**0.3.1 is a stable release.** Basic restocking and broken-tool replacement have been manually tested in single-player on all three supported versions. Multiplayer tool replacement, unusual item data, every server implementation, and long-duration edge cases still need broader testing. Follow your server's rules when using client plugins.

The release keeps the exact tested DLL, so its internal build/log identifier remains `0.3.1-test4`. Existing users of that build do not need to replace the DLL for this release.

Feedback and issue reports are welcome! Include your exact game version, single-player or server environment, item and reserve details, steps to reproduce, and a relevant excerpt from `AutoRestock.log`. Remove personal information before sharing logs.

Technical details: [verification and test coverage](https://github.com/XuanDiNa/AutoRestock/blob/main/docs/verification.md) · [release notes](https://github.com/XuanDiNa/AutoRestock/blob/main/docs/release-v0.3.1.md). Build and verification tools: [xmake](https://github.com/XuanDiNa/AutoRestock/blob/main/xmake.lua) · [verifier](https://github.com/XuanDiNa/AutoRestock/blob/main/tools/verify_release.ps1).

Inspired by [Tweakeroo Hand Restock](https://github.com/maruohon/tweakeroo). This is an independent Bedrock adaptation, not an official port. See [third-party notices](THIRD_PARTY_NOTICES.txt). No project source license has been granted; public availability alone does not grant a license.

## 繁體中文

為 Minecraft 基岩版自動補充主手物品。方塊快用完時從背包補上，工具損壞後也能換入同類備品。

透過 LeviLauncher 載入的客戶端原生 DLL 插件，不需要行為包、伺服器插件或配套協定。

[下載 0.3.1](https://github.com/XuanDiNa/AutoRestock/releases/tag/v0.3.1) · [回報問題](https://github.com/XuanDiNa/AutoRestock/issues)

### 主要功能

- **自動補貨**：主手物品低於設定數量時，從背包補入資料相同的物品。
- **工具替換**：工具損壞後，換入同類備品。備品保留自己的剩餘耐久，不會修復工具。
- **只取主背包**：使用主背包 27 格作為來源，不取其他快捷格，也不補副手。
- **比對物品資料**：檢查名稱、附魔、標籤與元件；尋找工具備品時，只在比較用副本上忽略耐久差異。
- **簡單設定**：透過 JSON 開關補貨、調整數量門檻，設定自動重新載入，不設熱鍵。

背包中必須有合適的備品，且目前遊戲狀態允許補貨。插件不會產生物品，也不保證耗盡時完全無縫銜接。

### 環境需求

- Windows x64
- Minecraft **1.26.45.01／1.26.51.01／1.26.52.03**
- 支援原生 DLL 插件的 LeviLauncher，以及 Microsoft Visual C++ x64 執行階段

同一個 DLL 支援上述三個精確版本，不適用於其他小版本；版本校驗不符時，插件不會啟用。

### 安裝方式

1. 完整退出 Minecraft。升級前，先將舊插件備份到 **mods 目錄外面**。
2. 從 [Releases](https://github.com/XuanDiNa/AutoRestock/releases) 下載插件 ZIP，不要下載自動產生的 Source code 壓縮檔。
3. 在對應遊戲版本的目錄建立 `mods/AutoRestock/`，將 ZIP 內的七個檔案解壓縮到該資料夾。
4. 升級時保留原有的 `config.json`、日誌與 `.close` 停用檔名；首次安裝才使用包內預設設定。

不要在遊戲執行中替換 DLL，也不要同時載入新舊插件。需要回退時，退出遊戲後還原備份的舊 DLL 與版本描述檔。本插件不能當作 `.mcpack` 匯入。

### 設定

編輯 DLL 同目錄的 `config.json`：

```json
{
  "enabled": true,
  "preRestockThreshold": 17
}
```

`enabled` 控制補貨開關；`preRestockThreshold` 可設為 1–64，採用「嚴格小於」判定：**17 表示剩餘 16 個或更少時補貨**。這是物品數量門檻，不是工具耐久門檻。

設定通常在 500ms 內自動重新載入；格式錯誤時保留上一份有效設定。可參考[預設設定](config.json)與[設定欄位說明](config.schema.json)。Release 附有 ZIP 及包內各檔案的 SHA-256 校驗值。

### 注意事項與回饋

**0.3.1 為正式版。** 三個支援版本均已在單人世界人工測試基本補貨與工具損壞替換。伺服器上的工具替換、特殊物品資料、所有服務端組合及長時間邊界情境，仍需要更完整的測試。使用客戶端插件時，請遵守伺服器規則。

正式版保留實測通過的原 DLL，因此內部建置與日誌仍顯示 `0.3.1-test4`。已正常使用這份 test4 的使用者，不必為本次正式發佈重換 DLL。

歡迎回報問題或提出建議！請附上精確遊戲版本、單人或伺服器環境、物品與備品資料、重現步驟，以及 `AutoRestock.log` 的相關片段。公開日誌前，請先移除個人資訊。

技術文件：[驗證與測試範圍](https://github.com/XuanDiNa/AutoRestock/blob/main/docs/verification.md) · [版本說明](https://github.com/XuanDiNa/AutoRestock/blob/main/docs/release-v0.3.1.md)。建置與驗證工具：[xmake](https://github.com/XuanDiNa/AutoRestock/blob/main/xmake.lua) · [驗證腳本](https://github.com/XuanDiNa/AutoRestock/blob/main/tools/verify_release.ps1)。

本專案參考 [Tweakeroo Hand Restock](https://github.com/maruohon/tweakeroo)，是獨立的基岩版適配實作，並非官方移植。第三方授權見[相關聲明](THIRD_PARTY_NOTICES.txt)。本專案未另行授予原始碼授權；公開可見不等於取得使用授權。
