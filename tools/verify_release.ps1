#requires -Version 7.0
param(
    [ValidateSet('debug','release')][string[]]$Mode = @('debug','release'),
    [switch]$SkipBuild,
    [string[]]$GameImages = @(),
    [string]$PackageZip
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$project = Split-Path -Parent $PSScriptRoot
$passed = [Collections.Generic.List[string]]::new()
$beforeImages = @{}
if ($GameImages.Count -notin 0,6) { throw 'Supply zero or six images: 45 original/loader,51 original/loader,52 original/loader.' }
foreach ($image in $GameImages) {
    $resolved = (Resolve-Path -LiteralPath $image).Path
    $beforeImages[$resolved] = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash
}
$plainTests = @(
    'AutoRestockConfigTests','AutoRestockReloadTests','AutoRestockPolicyTests',
    'AutoRestockResponseTests','AutoRestockNativeStackTests','AutoRestockNativeTests',
    'AutoRestockTransactionTests','AutoRestockIdentityTests','AutoRestockWorldTests',
    'AutoRestockUseTests','AutoRestockRuntimeTests','AutoRestockHookTests',
    'AutoRestockIntegratedTests','AutoRestockLegacyTests','AutoRestockIntegratedRuntimeTests',
    'AutoRestockSessionTests','AutoRestockNativeSessionTests','AutoRestockRemoteSessionTests',
    'AutoRestockRemoteHookTests','AutoRestockPluginRuntimeTests','AutoRestockOrdinaryTests',
    'AutoRestockOrdinaryStabilityTests','AutoRestockOrdinaryRecoveryTests',
    'AutoRestockCompatibilityBehaviorTests','AutoRestockVersionIsolationTests',
    'AutoRestockToolDiagnosticTests','AutoRestockToolHookDiagnosticTests',
    'AutoRestockWorldDestroyTests','AutoRestockToolHurtTests'
)
function Run-Test([string]$Name,[string]$Executable,[string[]]$Arguments = @()) {
    & $Executable @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Failed: $Name" }
    $passed.Add($Name)
}
Push-Location $project
try {
    foreach ($buildMode in $Mode) {
        if (-not $SkipBuild) {
            & xmake f -P $project -m $buildMode -y | Out-Host
            if ($LASTEXITCODE -ne 0) { throw "$buildMode configure failed" }
            & xmake build -r -P $project -y | Out-Host
            if ($LASTEXITCODE -ne 0) { throw "$buildMode build failed" }
        }
        $output = Join-Path $project "build/windows/x64/$buildMode"
        foreach ($test in $plainTests) { Run-Test "$buildMode/$test" (Join-Path $output "$test.exe") }
        foreach ($version in 45,51,52) {
            foreach ($scenario in '--base','--unwind','--install','--lost-route','--admission','--state-one','--route-diagnostic','--type3','--type3-zero','--type3-one') {
                Run-Test "$buildMode/RemoteHook/$scenario/$version" (Join-Path $output 'AutoRestockRemoteHookTests.exe') @($scenario,"$version")
            }
            Run-Test "$buildMode/Legacy/$version" (Join-Path $output 'AutoRestockLegacyTests.exe') @("$version")
        }
        Run-Test "$buildMode/UnsupportedHost" (Join-Path $output 'AutoRestockUnsupportedHostTest.exe') @((Join-Path $output 'AutoRestock.dll'))
        if ($GameImages.Count -eq 6) {
            Run-Test "$buildMode/SixImageProfiles" (Join-Path $output 'AutoRestockCompatibilityProfileTests.exe') $GameImages
            Run-Test "$buildMode/OriginalLoaderProfiles52" (Join-Path $output 'AutoRestockProfileTests.exe') @($GameImages[4],$GameImages[5])
        }
    }
} finally { Pop-Location }
foreach ($image in $beforeImages.Keys) {
    if ((Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash -ne $beforeImages[$image]) { throw 'A supplied image changed during verification.' }
}
$manifest = Get-Content -LiteralPath (Join-Path $project 'manifest.json') -Raw | ConvertFrom-Json
$mod = Get-Content -LiteralPath (Join-Path $project 'mod.json') -Raw | ConvertFrom-Json
if ($manifest.version -ne '0.3.2' -or $mod.version -ne $manifest.version -or $manifest.entry -ne 'AutoRestock.dll' -or $manifest.type -ne 'preload-native' -or $mod.dll -ne $manifest.entry) { throw 'Loader metadata mismatch.' }
$configText = Get-Content -LiteralPath (Join-Path $project 'config.json') -Raw
if (-not ($configText | Test-Json -SchemaFile (Join-Path $project 'config.schema.json'))) { throw 'Invalid default config.' }
$config = $configText | ConvertFrom-Json
if ($config.enabled -ne $true -or $config.preRestockThreshold -ne 17) { throw 'Unexpected packaged defaults.' }
if ($PackageZip) {
    $artifacts = Get-Content -LiteralPath (Join-Path $project 'docs/release-artifacts-0.3.2.json') -Raw | ConvertFrom-Json
    if ($artifacts.version -ne $manifest.version -or $artifacts.files.'AutoRestock.dll' -ne 'C0583EA639AF0BA9D30F85205A29BC6D517612AFDF25230E0E176D29D37F0C79') { throw 'Wrong frozen release artifact manifest.' }
    $expectedZip = $artifacts.archiveSha256
    if ((Get-FileHash -LiteralPath $PackageZip -Algorithm SHA256).Hash -ne $expectedZip) { throw 'Not the official frozen ZIP.' }
    $files = @{}
    $expectedNames = @('AutoRestock.dll','manifest.json','mod.json','config.json','config.schema.json','README.md','THIRD_PARTY_NOTICES.txt')
    if (@($artifacts.files.PSObject.Properties).Count -ne 7) { throw 'Unexpected artifact whitelist.' }
    foreach ($name in $expectedNames) {
        if ($artifacts.files.$name -notmatch '^[0-9A-F]{64}$') { throw "Invalid artifact hash: $name" }
        $files[$name] = $artifacts.files.$name
    }
    $zip = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $PackageZip).Path)
    try {
        if ($zip.Entries.Count -ne 7 -or @($zip.Entries.FullName | Select-Object -Unique).Count -ne 7) { throw 'Unexpected ZIP entries.' }
        foreach ($name in $files.Keys) {
            $entry = $zip.GetEntry($name)
            if ($null -eq $entry) { throw "Missing entry: $name" }
            $stream = $entry.Open()
            $sha = [Security.Cryptography.SHA256]::Create()
            try { $actual = [Convert]::ToHexString($sha.ComputeHash($stream)) }
            finally { $stream.Dispose(); $sha.Dispose() }
            if ($actual -ne $files[$name]) { throw "ZIP hash mismatch: $name" }
        }
    } finally { $zip.Dispose() }
}
[ordered]@{
    Status = 'PASS'; TestExecutions = $passed.Count; Modes = $Mode
    SuppliedImagesReadOnlyVerified = $beforeImages.Count
    FrozenOfficialZipChecked = [bool]$PackageZip
    GameplayTested = $false; Installed = $false
    Note = 'Fresh public-source offline checks; no game process control or release DLL replacement. The 147 development records and three-version server gameplay acceptance are separate evidence.'
} | ConvertTo-Json
