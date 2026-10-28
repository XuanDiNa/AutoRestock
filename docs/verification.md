# Verification and test coverage / 驗證與測試範圍

## English

### Release artifact and manual acceptance

Version 0.3.2 retains the exact 0.3.2-test2 DLL tested on Minecraft
**1.26.45.01 / 1.26.51.01 / 1.26.52.03**. The user confirmed that the
affected-server block-restocking issue is resolved on each version.
Earlier single-player refill and broken-tool replacement acceptance is retained;
multiplayer tool replacement was not separately accepted in this round.

Release DLL SHA-256:
`C0583EA639AF0BA9D30F85205A29BC6D517612AFDF25230E0E176D29D37F0C79`.

This is evidence for the tested server and cases, not every server implementation,
item metadata combination, reconnect scenario or long-duration workload.
Queued requests and local inventory predictions alone are not server acceptance.

### Offline suites

The frozen candidate has 147 development verification records. Those include
private analysis/native-code laboratories that are not distributed here.
The independent public-source verifier exercises:

- 29 plain test executables per mode.
- Three profiles, each with ten hook scenarios and one legacy ABI scenario.
- One unsupported-host test per mode: 126 Debug/Release executions in total.
- Six optional, locally supplied game images add two calls per mode, giving 130.

New fixtures cover kind3 identity variants 0/1/2, generic registration without a
RakNet receive frame, exact inner constructor/notification sites, modern/ordinary
StartGame, response authority, retirement/reuse and refusal of unsupported,
ambiguous, local-peer, missing-birth or foreign-thread inputs.
The original kind0 route and inventory/transaction regressions remain covered.
All three profiles retain 98 mapped keys, 67 loaded-byte guards and 23 hooks.

Publication verification: fresh Debug/Release builds passed all 130 executions
with six read-only images, and the official ZIP's seven member hashes matched.

These are isolated native fixtures, not gameplay. A public-source rebuild is
checked independently and never substituted for the accepted release DLL.
No game binaries, IDA databases, decompilation, private logs or credentials are
included. Previous release documents remain historical records.

### Reproduce public-source checks

Requirements: Windows x64, Visual Studio 2022 C++ tools/Windows SDK, PowerShell 7
and xmake. The JSON header is vendored; xmake obtains MinHook 1.3.4.

```powershell
pwsh -File tools/verify_release.ps1

# Optional: legal, locally supplied files, in this exact order.
$images = @(
    'C:/GameImages/45/original.exe', 'C:/GameImages/45/loader.exe',
    'C:/GameImages/51/original.exe', 'C:/GameImages/51/loader.exe',
    'C:/GameImages/52/original.exe', 'C:/GameImages/52/loader.exe'
)
./tools/verify_release.ps1 -GameImages $images -PackageZip './dist/AutoRestock_0.3.2_MC26.45-26.51-26.52.zip'
```

The verifier builds fresh unless `-SkipBuild` is specified. `-PackageZip` checks
the frozen ZIP and its seven members against
[release-artifacts-0.3.2.json](release-artifacts-0.3.2.json).
It does not control Minecraft, install plugins, modify supplied images or replace
the official DLL. Rebuilds do not inherit manual gameplay acceptance.

## 繁體中文

### 正式套件與人工驗收

0.3.2 沿用三個精確版本實測通過的 0.3.2-test2 原 DLL。使用者已分別確認
**1.26.45.01／1.26.51.01／1.26.52.03** 的伺服器方塊補貨問題已修復。
先前單人補貨與工具替換的驗收保留；本輪沒有單獨驗收伺服器工具替換。

DLL 雜湊列於英文區塊。驗收限實測服務器與情境，不代表所有服務端、物品資料、
重連或長時間負載。請求提交及本機預測本身，不是伺服器接受的證明。

### 離線測試

凍結候選版有 147 項開發驗證紀錄，包含不公開的分析／原生碼實驗。
公開驗證器每模式執行 29 個一般測試、三版各 10 個 hook 場景與 1 個 legacy ABI
場景，以及不支援程序拒絕測試；Debug／Release 共 126 次。
合法自備六份遊戲映像後共 130 次。

新增測試涵蓋 type 3 的三種已知格式、通用連線註冊、精確原生建立／通知流程、
StartGame、回應權限及會話退役／重用；未知格式、重複身分、本機內部連線、
缺少建立紀錄與非擁有執行緒均須拒絕。原 type 0 與補貨交易回歸測試保留。
三版各維持 98 個映射鍵、67 處載入保護與 23 個 hook。

發佈前獨立建置 Debug／Release，搭配唯讀六份映像，130 次驗證全部通過；
正式 ZIP 的七個成員雜湊亦已核對。

### 自行驗證

工具需求與指令見英文區塊。公開原始碼的獨立建置不會替換正式附件中的實測 DLL，
也不繼承人工驗收。驗證器不控制遊戲、不安裝插件、不改遊戲映像；
公開內容不含遊戲二進位、IDA／反編譯資料、私人日誌或憑證。
