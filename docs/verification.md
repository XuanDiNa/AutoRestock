# 驗證範圍 / Verification scope

## 中文

正式套件是使用者已驗收的原二進位，不以公開原始碼的新編譯結果取代。
套件版本 0.3.0；DLL／日誌內部識別仍為 0.3.0-test3。

| 證據 | 範圍與限制 |
| --- | --- |
| 人工驗收 | 使用者回報 26.45、26.51、26.52 正常使用，對應精確版本 1.26.45.01、1.26.51.01、1.26.52.03 的實測環境 |
| 原 DLL 歷史離線證據 | 原建置記錄 115 次測試執行；包含私有分析及原生葉函式實驗，不全部隨公開原始碼提供 |
| 公開原始碼驗證器 | 每模式 25 個一般測試、三版各 6 個 hook 場景及 1 個 legacy ABI 場景、不支援程序拒絕，共 47 次；Debug＋Release 共 94 次 |
| 可選六映像檢查 | 每模式另外 2 次呼叫，包含六映像的身分／載入保護／篡改拒絕與 52 原始／載入器映像；兩模式總數為 98 次 |
| 套件檢查 | 固定 ZIP SHA256、7 個唯一根目錄檔案、逐檔雜湊、metadata 與 JSON schema |

不包含遊戲啟動、注入、安裝或人工操作。新增編譯的 DLL 不自動繼承人工驗收。
不把 Ready、queued／deferred 或本地重新規劃當作伺服器 ACK。
所有服務端、重連持久性／總量核對、所有恢復分支、網路狀態及長時間穩定性不由這次回報保證。

```powershell
# 一般公開原始碼測試，不需要遊戲映像
pwsh -File tools/verify_release.ps1

# 如需唯讀的六映像測試，按版本與形式傳入合法自備檔案
$images = @(
    'C:/GameImages/45/original.exe', 'C:/GameImages/45/loader.exe',
    'C:/GameImages/51/original.exe', 'C:/GameImages/51/loader.exe',
    'C:/GameImages/52/original.exe', 'C:/GameImages/52/loader.exe'
)
./tools/verify_release.ps1 -GameImages $images -PackageZip './dist/AutoRestock_0.3.0_MC26.45-26.51-26.52.zip'
```

上例路徑只是範例，不是私人工作區路徑。遊戲檔案需自行合法取得，不應上傳 GitHub。
公開副本排除研究模型、抽取遊戲程式碼的葉函式實驗、IDA 匯出與私人分析腳本；
保留原生版本表及必要 entry guards，不以外部未知版本掃描取代精確匹配。

## English

The official package preserves the exact manually accepted binary. Delivery
version is 0.3.0; internal DLL/log identity remains 0.3.0-test3.

- Manual acceptance: the user reported normal use on exact 1.26.45.01,
  1.26.51.01 and 1.26.52.03 in their tested environments.
- Historical frozen-build evidence: 115 recorded offline executions, including
  private analysis/native-leaf laboratories not distributed in this repository.
- Public verifier: 25 plain tests, three versions × six hook scenarios plus one
  legacy ABI scenario, and unsupported-host refusal = 47 executions per mode,
  or 94 for Debug and Release.
- Optional locally supplied images add two executions per mode: a six-image
  identity/loaded-guard/tamper suite and original/loader 52 profile checks.
  Both modes with images therefore run 98 executions.
- Package checks verify the frozen ZIP hash, seven unique root entries, every
  entry's hash, loader metadata and JSON schema.

Commands are shown above. Six-image input order is 45 original,45 loader,
51 original,51 loader,52 original,52 loader. Obtain images legally yourself;
do not distribute them. Verification never starts/attaches to the game, installs
a plugin, rewrites supplied images or replaces/repackages the official DLL.
`-SkipBuild` is only for testing an already-built matching output directory;
the default performs fresh builds.

Public source retains exact native version profiles and necessary entry guards,
but excludes raw game-code leaf laboratories, research models, decompilation,
IDA databases and private analysis tooling. The public test count is not the
same suite as the historical 115 executions. A rebuilt DLL is not automatically
the user-accepted binary. See the release notes for publication-time results.

None of these checks proves every server brand, network condition, recovery
branch, reconnect-persistence/item-conservation case or long-duration workload.
Ready and queued/deferred diagnostics are not remote server acknowledgments.

## 本次公開前核對 / Publication-time verification — 2026-10-17

乾淨公開副本的 Debug／Release 建置成功，各 49 次離線測試通過（含合法自備六映像），
合計 **98 次**。40 個 production 原始碼／標頭檔與開發來源逐位元組一致；
Git 暫存區也保留其原始位元組。正式套件的 AMD64、必要匯出、依賴及 7 檔雜湊已核對。
新編譯 Release DLL 雜湊為 `4DD93EEA5B6BECC5192974CDE28053F65689006C023CC80B1ECF7823769EBBF0`，
與已驗收 DLL 不同，因此不作為附件、不安裝，也不宣稱通過人工實測。

Clean public-copy Debug and Release builds succeeded, each with 49 passing
offline executions including the six locally supplied images: **98 total**.
All 40 production source/header files and their staged Git blobs preserve the
original bytes. The official package's AMD64 architecture, required exports,
dependencies and seven entry hashes were checked separately. The newly built
Release DLL hash shown above differs from the accepted DLL; it is not uploaded
as a release asset, installed or claimed to have passed gameplay testing.
