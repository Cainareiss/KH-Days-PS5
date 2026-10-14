[CmdletBinding()]
param(
    [string]$Elf = "..\build-ps5-runtime\khdays-days-runtime.elf",
    [string]$SdkRoot = "..\toolchains\ps5-payload-sdk-v0.44\ps5-payload-sdk",
    [string]$OutputRoot = "..\build-ps5-app",
    [string]$TitleId = "PPSA12345"
)

$ErrorActionPreference = "Stop"
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$elfPath = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot $Elf))
$sdkPath = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot $SdkRoot))
$outputPath = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot $OutputRoot))
$fselfTool = Join-Path $sdkPath "samples\install_app\make_fself.py"
$stagePath = Join-Path $outputPath $TitleId
$sceSysPath = Join-Path $stagePath "sce_sys"

if (-not (Test-Path -LiteralPath $elfPath -PathType Leaf)) {
    throw "ELF não encontrado: $elfPath"
}
if (-not (Test-Path -LiteralPath $fselfTool -PathType Leaf)) {
    throw "make_fself.py não encontrado no SDK: $fselfTool"
}

New-Item -ItemType Directory -Force -Path $sceSysPath | Out-Null
$ebootPath = Join-Path $stagePath "eboot.bin"

# This produces a fake-signed FSELF for a homebrew/debug workflow. It is not a
# retail Sony PKG and must not be renamed to .pkg.
python $fselfTool --ptype fake --paid 0x3800000000000022 `
    --app-version 0x07590001 --fw-version 0x07590001 `
    $elfPath $ebootPath

$param = @{
    titleId = $TitleId
    titleName = "KH Days PS5 (WIP)"
    version = "01.00"
    contentId = "UP0001-${TitleId}_00-KHDAYSWIP000001"
    applicationCategoryType = 1
    parentalLevel = 1
} | ConvertTo-Json
Set-Content -LiteralPath (Join-Path $sceSysPath "param.json") -Value $param -Encoding utf8

$zipPath = Join-Path $outputPath "$TitleId-homebrew.zip"
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -LiteralPath $stagePath -DestinationPath $zipPath

Write-Host "ELF:    $elfPath"
Write-Host "FSELF:  $ebootPath"
Write-Host "Bundle: $zipPath"
Write-Warning "O SDK público não gera PKG retail assinado; este bundle é apenas FSELF fake/homebrew."
