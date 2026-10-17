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
    'AutoRestockCompatibilityBehaviorTests','AutoRestockVersionIsolationTests'
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
            foreach ($scenario in '--base','--unwind','--install','--lost-route','--admission','--state-one') {
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
if ($manifest.version -ne '0.3.0' -or $mod.version -ne $manifest.version -or $manifest.entry -ne 'AutoRestock.dll' -or $manifest.type -ne 'preload-native' -or $mod.dll -ne $manifest.entry) { throw 'Loader metadata mismatch.' }
$configText = Get-Content -LiteralPath (Join-Path $project 'config.json') -Raw
if (-not ($configText | Test-Json -SchemaFile (Join-Path $project 'config.schema.json'))) { throw 'Invalid default config.' }
$config = $configText | ConvertFrom-Json
if ($config.enabled -ne $true -or $config.preRestockThreshold -ne 17) { throw 'Unexpected packaged defaults.' }
if ($PackageZip) {
    $expectedZip = '90E19A615C1054CE72F123A52B31E3F3C71643937D1641F0EED1389DD424E5F8'
    if ((Get-FileHash -LiteralPath $PackageZip -Algorithm SHA256).Hash -ne $expectedZip) { throw 'Not the official frozen ZIP.' }
    $files = @{
        'AutoRestock.dll' = 'C2073234D4611FF6241340A1117B33815268D45C00C7075049B39D93E8C8FE6A'
        'manifest.json' = 'CCED2657FA58C737C14C41FE4E564A53630150DD08823291A1B106D8D1D0C6FF'
        'mod.json' = '4841C01E5FABC2F30BF64CA17B9383A04C2DE725D8FC774E45665F697610FD5A'
        'config.json' = '72BBC67DB753D28EA87D4DBBF04AFAB669E03178AAB88EF014DAC73D3D81D78B'
        'config.schema.json' = '3321F47493CB308479CC25BA1F527F7AFBBAC21629DCD30B0A9718CADA6F2EE1'
        'README.md' = 'AF8384B966B55B7602805DCA446D04681455281F579DDED2905EEBADDB292823'
        'THIRD_PARTY_NOTICES.txt' = '650DF50E50B53BA97B8B7BC216E4A89FBB910C77A650AC801469D8CB355DFE1B'
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
    Note = 'Fresh public-source offline checks; no game process control or release DLL replacement. Historical 115 tests and user gameplay acceptance are separate evidence.'
} | ConvertTo-Json
