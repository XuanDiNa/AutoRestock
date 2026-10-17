# AutoRestock v0.3.0 — 三版本自動補貨 / Three-version main-hand restocking

## 中文

Windows x64 基岩版原生客戶端插件，參考 Tweakeroo Hand Restock。
支援精確版本 **1.26.45.01、1.26.51.01、1.26.52.03**，單一 DLL 自動選版。

### 功能

- 主手物品低於設定門檻時，從主背包 27 格（9–35）尋找完整資料相同的物品補貨。
- 不補副手，不把其他快捷欄當來源；只用 JSON 的 `enabled` 開關，無熱鍵／畫面提示。
- 包內 `enabled=true`、`preRestockThreshold=17`，即剩餘 **≤16** 嘗試補貨。
  欄位省略時舊預設仍為 6，沒有更改解析語義。
- 真正消耗至空手的保底、同連線 ordinary 重新規劃、至少 1500ms 提交間隔。
- 保留完整物品比較、版本身分、65 處載入保護與 21 個既有 hook，不修改遊戲 EXE。

### 安裝／更新／移除

從下方 Assets 下載 ZIP 與 `.sha256`，校驗後完全退出 Minecraft，備份原插件目錄。
使用支援 `preload-native` 的原生 DLL 載入器（測試採用 LeviLauncher），將 ZIP 的 7 個
根目錄檔案放入該版本 `mods/AutoRestock/`。這不是行為包或 `.mcpack`，需要另備相容載入器。
升級時**保留原 config.json、日誌及原停用 `.close` 狀態**，不要直接覆蓋自訂設定。
停用可設 `enabled=false`；移除／回退需退出遊戲後停用本插件或換回備份 DLL／metadata。

### 已驗收版本與限制

使用者已回報 26.45、26.51、26.52 正常使用；三版已安裝 DLL 與本次附件相同。
正式包沿用已驗收 DLL，**未重新編譯／連結／修補**，日誌仍顯示 `0.3.0-test3`。
內部舊 `candidate`／待測文字是凍結建置的歷史說明。現有 test3 使用者不必重裝或重測。

人工回報限實測環境，不保證所有服務端、反作弊、網路條件、恢復分支或長時間穩定性；
BDS 未納入本輪目標。原建置 115 次離線測試是歷史證據，不是本輪新跑的同一套測試。
公開原始碼的獨立離線檢查與人工實測分開；新編譯 DLL 不會取代附件中的已驗收 DLL。

公開前乾淨副本 Debug／Release 建置及 **98 次離線測試通過**（各 49 次，含六映像檢查）。
這不是新增人工遊戲測試；完整驗證範圍見倉庫 `docs/verification.md`。

完整中文：[README](https://github.com/XuanDiNa/AutoRestock/blob/main/README.md)

## English

A Windows x64 native Bedrock client plugin inspired by Tweakeroo Hand Restock.
Supports exact **1.26.45.01, 1.26.51.01 and 1.26.52.03** with automatic profile
selection in one DLL.

### Features

- Refills the main hand from 27 main-inventory slots (9–35), matching full item
  data; no offhand refilling or reserve sourcing from other hotbar slots.
- JSON-only `enabled` switch and threshold, with no hotkey/chat/HUD prompt.
- Shipped `enabled=true`, `preRestockThreshold=17` means attempt at remaining
  **16 or below**. Omitted-field default remains 6; semantics are unchanged.
- Genuine-depletion empty-hand fallback, same-session ordinary replanning and
  at least 1500 ms between native submissions.
- Full item, session/thread/lifetime and transaction checks; exact version
  identities, 65 loaded guards and 21 existing hooks. No game EXE modification.

### Installation, updates and removal

Download the ZIP and adjacent `.sha256` under Assets. Verify its SHA256, fully
exit Minecraft and back up the existing plugin directory. Use a compatible
native loader supporting `preload-native` (tested with LeviLauncher), then
extract the seven root files into that version's `mods/AutoRestock/` directory.
This is not a behavior pack or `.mcpack`; the native loader is required separately.

On upgrade, **preserve your config.json, logs and disabled `.close` state**.
Do not overwrite custom settings with defaults. Set `enabled=false` to disable.
Exit Minecraft before removal or restoring backed-up DLL/metadata; never replace
a loaded DLL.

### Acceptance and test boundaries

The user reported normal use on all three exact versions. Their installed DLLs
match this release. The accepted binary is reused without recompilation,
relinking or patching, so the internal log/build identity is **0.3.0-test3**.
Frozen candidate/pending-test wording is historical. Existing working test3
users need not reinstall or repeat gameplay tests for this metadata-only release.

Acceptance covers tested environments, not every server, anti-cheat, network,
recovery branch or long-duration workload; BDS was not an acceptance target.
The original build's 115 offline executions are historical evidence, separate
from publication-time public-source checks. A source rebuild is not the
manually tested DLL and is never substituted into these Assets.

Before publication, clean public-copy Debug/Release builds and **98 offline
test executions passed** (49 per mode, including six-image checks). These are
not additional in-game tests; see `docs/verification.md` for their exact scope.

Complete English instructions: [README.en.md](https://github.com/XuanDiNa/AutoRestock/blob/main/README.en.md)

## SHA256

Official ZIP:
`90E19A615C1054CE72F123A52B31E3F3C71643937D1641F0EED1389DD424E5F8`

User-tested DLL:
`C2073234D4611FF6241340A1117B33815268D45C00C7075049B39D93E8C8FE6A`

不含遊戲二進位、IDA／反編譯資料、私人日誌或憑證。
No game binaries, IDA/decompilation data, private logs or credentials are included.
