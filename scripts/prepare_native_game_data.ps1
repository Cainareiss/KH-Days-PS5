[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$RomPath,
    [string]$SourceRoot = ".cache/khdays-native-v0.1.1",
    [switch]$AllowUnknown
)

$ErrorActionPreference = "Stop"
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$rom = (Resolve-Path -LiteralPath $RomPath).Path
$source = if ([IO.Path]::IsPathRooted($SourceRoot)) {
    [IO.Path]::GetFullPath($SourceRoot)
} else {
    [IO.Path]::GetFullPath((Join-Path $project $SourceRoot))
}
$lock = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $project "native/upstream-lock.json") |
    ConvertFrom-Json
$verify = Join-Path $source "tools/verify_rom.py"
$extract = Join-Path $source "tools/extract_data.py"

foreach ($required in @($verify, $extract)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Prepare the pinned native source first: missing $required"
    }
}

$verificationText = (& python $verify $rom --json 2>&1 | Out-String).Trim()
$verificationExit = $LASTEXITCODE
$verification = $verificationText | ConvertFrom-Json
if ($verificationExit -ne 0 -and -not $AllowUnknown) {
    throw "The upstream verifier rejected the ROM. Use -AllowUnknown only for a development extraction of a matching game-code revision."
}
$extractArgs = @($extract, $rom)
if ($AllowUnknown) { $extractArgs += "--allow-unknown" }
& python @extractArgs
if ($LASTEXITCODE -ne 0) { throw "The native game-data extraction failed" }

[ordered]@{
    rom = $rom
    game_code = $lock.supported_rom.game_code
    source_sha256 = $verification.sha256
    supported_by_upstream = ($verificationExit -eq 0)
    supported_sha256 = $lock.supported_rom.sha256
    development_extraction = [bool]$AllowUnknown
    extracted_under = (Join-Path $source "data/extracted")
    copied_to_git = $false
} | ConvertTo-Json
