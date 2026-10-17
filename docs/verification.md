# Verification and test coverage / 驗證與測試範圍

## English

### Release artifact and manual acceptance

Version 0.3.1 packages the unchanged user-tested 0.3.1-test4 DLL. The user confirmed
broken-tool replacement and normal use in single-player on exact Minecraft
1.26.45.01, 1.26.51.01 and 1.26.52.03, including tools beyond the initial gold shovel.
The packaged DLL SHA-256 is
`1C87F52F1E3FB51E881A5ACF2396B6FD4270F54E02B1E899E87AD9F542CCD556`.

Acceptance covers tested cases, not every item/metadata combination, server, network,
recovery branch, reconnect-persistence scenario, or long-duration workload. Multiplayer
tool-break replacement has not been separately accepted. Native accepted results and
diagnostics do not replace the user's gameplay report.

### Offline suites

The frozen candidate has 123 recorded development verification executions, including private
analysis/native-leaf laboratories not distributed here. This public source has an independent
portable suite:

- 29 plain test executables per build mode.
- Three exact profiles, each with six native hook scenarios and one legacy ABI scenario.
- One unsupported-host DLL refusal test per mode.
- Total: 51 executions per mode, 102 for Debug and Release.
- Optional six locally supplied images add two calls per mode: six-image identity/loaded
  guards/tamper refusal and original/loader 52 profile checks. Both modes then total 106.

Tool tests cover singleton identity, durability normalization on owned copies, delayed native
break-to-hand application, spare damage preservation, and context/ID/UI/slot/expiry/manual-removal
refusal. These use controlled external ABI bodies with production runtime logic, not the full game.

All three exact profiles retain 98 mapped keys, 67 loaded-byte guards and 23 hooks. Public tests
exercise forwarding, unwinding, and actual MinHook installation/removal/byte restoration in
isolated fixture images; they do not attach to Minecraft.

Publication check on 2026-10-18: fresh Debug and Release builds passed all 106 executions with
six read-only game images. The frozen ZIP and all seven member hashes passed verification.
Both the retained release DLL and the independent public rebuild passed AMD64, export and
runtime-dependency checks; the retained DLL also refused an unsupported test host. The public
rebuild has SHA-256 `DBCF4C06D064469A347A6327BED6EF9A6E509694FD6E07D9E0A40E4DD462EE54`
and is not a release asset. These are offline results, not additional gameplay acceptance.

### Reproduce public-source checks

Requirements: Windows x64, Visual Studio 2022 C++ tools/Windows SDK, PowerShell 7, and xmake.
The JSON header is vendored; xmake obtains MinHook 1.3.4. Source licensing is not otherwise
granted. See the third-party notices.

```powershell
pwsh -File tools/verify_release.ps1

# Optional: legal, locally supplied files in this exact order.
$images = @(
    'C:/GameImages/45/original.exe', 'C:/GameImages/45/loader.exe',
    'C:/GameImages/51/original.exe', 'C:/GameImages/51/loader.exe',
    'C:/GameImages/52/original.exe', 'C:/GameImages/52/loader.exe'
)
./tools/verify_release.ps1 -GameImages $images -PackageZip './dist/AutoRestock_0.3.1_MC26.45-26.51-26.52.zip'
```

These paths are examples, not distributed game files. The verifier performs fresh builds unless
`-SkipBuild` is specified. It never controls the game, installs plugins, changes supplied images,
or replaces/repackages the official DLL. `-PackageZip` checks the archive and all seven member
hashes against [release-artifacts-0.3.1.json](release-artifacts-0.3.1.json), plus loader metadata
and default JSON.

Production files preserve the tested development source bytes. Public build paths and test
selection are portable adaptations. A rebuilt DLL is not automatically gameplay-tested;
only the retained tested DLL is uploaded. Raw game-code laboratories, research models, IDA
databases, decompilation and private tooling are excluded. Earlier 0.3.0 release notes remain
historical documents, not current claims.

## 繁體中文

### 正式套件與人工驗收

0.3.1 沿用使用者已實測的 0.3.1-test4 原 DLL。使用者已確認精確版本 1.26.45.01、
1.26.51.01、1.26.52.03 在單人世界的工具損壞替換與正常使用，並測過最初金鏟以外的工具。
套件 DLL 的 SHA-256 列於上方，不以重新編譯的結果替換。

驗收限實測情境，不代表所有物品資料、服務端、網路、恢復分支、重連持久性或長時間負載。
伺服器工具補貨尚未單獨驗收；原生接受結果與診斷資訊不能代替人工遊戲回報。

### 離線測試

凍結候選版記錄 123 項開發驗證，其中私人分析／原生葉函式實驗不隨公開倉庫提供。
公開副本每模式 29 個一般測試、三版各 6 個 hook 場景及 1 個 legacy ABI 場景、
以及 1 個不支援程序拒絕，共 51 次；Debug／Release 合計 102 次。
合法自備六份遊戲映像後，每模式再加 2 次呼叫，合計 106 次。

工具測試包含不可堆疊物品比對、自有副本耐久正規化、延後破損套用、備品耐久保留，
以及身分／介面／快捷格／逾時／手動移除等拒絕情境。外部原生函式由測試 ABI 替身提供，
執行的是 production 邏輯，不等於在遊戲內實測。

三個精確版本各有 98 個映射鍵、67 處載入保護及 23 個 hook。公開實驗在隔離 fixture
映像中測試轉發、例外及實際 MinHook 安裝／移除／位元組還原，不附加 Minecraft。

2026-10-18 發佈前重新建置 Debug／Release，搭配唯讀六份遊戲映像，106 次驗證全部通過；
正式 ZIP 與七個成員的雜湊核對通過。沿用的正式 DLL 與公開副本新建置 DLL 均通過
AMD64、匯出及執行階段依賴檢查，正式 DLL 另通過不支援測試程序拒絕檢查。
公開副本新建置的雜湊列於英文區塊，不作為 Release 附件，也不視為新增的遊戲內驗收。

### 自行驗證

需要 Windows x64、Visual Studio 2022 C++ 工具／Windows SDK、PowerShell 7 及 xmake。
指令見上方英文區塊。驗證器預設重新建置；`-SkipBuild` 只用於已備好的對應輸出。
`-PackageZip` 依同目錄的 release-artifacts JSON 核對正式 ZIP 及七個成員，另檢查 metadata 與 JSON。

驗證器不控制遊戲、不安裝插件、不改遊戲映像，也不重包或替換正式 DLL。
公開 production 檔案保持開發來源位元組，只有建置路徑與測試選擇作可攜式適配。
新編譯 DLL 不會自動繼承人工驗收。公開倉庫不含遊戲碼抽取實驗、研究模型、IDA／反編譯資料
或私人分析工具；舊 0.3.0 版本說明保留為歷史紀錄。
