# AutoRestock v0.3.1 — Tool replacement / 工具替換

## English

Stable release for Windows x64, LeviLauncher, and Minecraft Bedrock
**1.26.45.01 / 1.26.51.01 / 1.26.52.03**. One DLL selects the exact version automatically.

### What's new

- Replace a broken main-hand tool with a matching spare from the 27 main-inventory slots.
- Correct non-stackable tool matching while retaining names, enchantments, tags, and components.
  A spare keeps its own remaining durability.
- Observe native tool breakage and its later main-hand update, rather than treating arbitrary
  empty hands as broken tools.
- Keep existing ordinary-item refill, JSON settings, and exact-version safety checks.
- Refresh the README with the same English/Traditional Chinese layout as MouseTweaksBedrock.

### Download and installation

Download `AutoRestock_0.3.1_MC26.45-26.51-26.52.zip` from Assets, not the automatic source
archives. Close Minecraft, back up the old plugin outside mods, and extract the seven files
into the selected version's `mods/AutoRestock/`. Preserve `config.json`, logs, and disabled
`.close` filenames. No behavior pack or server-side plugin is required.

Defaults are `enabled:true` and `preRestockThreshold:17`: refill at stack count 16 or below,
not a durability threshold. Main hand only; reserves from slots 9–35, not offhand/other hotbar slots.

### Validation and compatibility

The user confirmed working tool replacement and normal use in single-player on all three
versions, including additional tools beyond the initial gold shovel. This release retains that
exact tested DLL without recompilation or patching; internal logs remain **0.3.1-test4**.
Existing working test4 installations do not need a DLL replacement for this release.

The frozen candidate has 123 development verification records. Public-source checks are
independent: 102 executions across Debug/Release, or 106 with six locally supplied game images.
A public-source rebuild is not the manually accepted DLL and is not substituted into Assets.

Multiplayer tool replacement, unusual item metadata, every server implementation, and
long-duration/network edge cases are not fully validated. See the
[README](https://github.com/XuanDiNa/AutoRestock#english) and
[verification notes](https://github.com/XuanDiNa/AutoRestock/blob/main/docs/verification.md).

## 繁體中文

Windows x64、LeviLauncher 正式版插件，支援 Minecraft 基岩版精確版本
**1.26.45.01／1.26.51.01／1.26.52.03**，同一個 DLL 自動選版。

### 本次更新

- 主手工具損壞後，從主背包 27 格換入資料相同的備品。
- 修正不可堆疊工具比對，保留名稱、附魔、標籤與元件檢查；備品保留自己的剩餘耐久。
- 觀察原生工具破損及稍後的主手更新，不把任意空手判定成工具損壞。
- 沿用普通物品補貨、JSON 設定及精確版本保護。
- README 改為與 MouseTweaksBedrock 一致的英文／繁體中文排列。

### 下載與安裝

從 Assets 下載 `AutoRestock_0.3.1_MC26.45-26.51-26.52.zip`，不要下載自動產生的原始碼
壓縮檔。退出遊戲，將舊插件備份到 mods 外面，再把 ZIP 的七個檔案放入對應版本的
`mods/AutoRestock/`。升級保留原有 `config.json`、日誌及 `.close` 停用檔名。
不需要行為包或伺服器插件。

包內設定為 `enabled:true`、`preRestockThreshold:17`，即物品剩餘 16 個或更少時嘗試補貨，
不是工具耐久門檻。只補主手，來源限 9–35 格，不補副手、不取其他快捷格。

### 驗收與相容範圍

使用者已確認三個版本在單人世界的工具替換與正常使用，並測過金鏟以外的工具。
正式包保留這份已測試 DLL，不重新編譯或修補，因此內部日誌仍為 **0.3.1-test4**。
已正常使用 test4 者，不必為這次正式發佈重換 DLL。

凍結候選版已有 123 項開發驗證記錄。公開原始碼獨立測試 Debug／Release 共 102 次，
傳入合法自備六份遊戲映像後共 106 次；新編譯 DLL 不會代替 Assets 中的已驗收 DLL。

伺服器工具替換、特殊物品資料、所有服務端及長時間／網路邊界情境仍未完整驗證。
詳見[README](https://github.com/XuanDiNa/AutoRestock#繁體中文)及
[驗證說明](https://github.com/XuanDiNa/AutoRestock/blob/main/docs/verification.md)。

## Checksums / 校驗

Release Assets include SHA-256 checksums for the ZIP and all seven contained files.
The unchanged, user-tested DLL SHA-256 is:

`1C87F52F1E3FB51E881A5ACF2396B6FD4270F54E02B1E899E87AD9F542CCD556`

附件提供 ZIP 與包內各檔案的 SHA-256；不含遊戲二進位、IDA／反編譯資料、私人日誌或憑證。
No game binaries, IDA/decompilation data, private logs, or credentials are distributed.
