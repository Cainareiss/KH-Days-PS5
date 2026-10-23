[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$EbootPath,
    [string]$DataExtractedRoot = ".cache/khdays-native-v0.1.1/data/extracted",
    [Parameter(Mandatory = $true)]
    [string]$LibcPrxPath,
    [string]$ExecutableValidationPath = 'build-native-game-ps5/native-elf-validation.json',
    [string]$IconPath = "build-native-link/PPSA35820/sce_sys/icon0.png",
    [string]$BackgroundPath = "build-native-link/PPSA35820/sce_sys/pic0.png",
    [string]$TitleId = "PPSA35820",
    [ValidateNotNullOrEmpty()]
    [string]$TitleName = "Kingdom Hearts 358/2 Days - Native PS5 (WIP)",
    [string]$RomPath = "",
    [string]$MelonProjectRoot = "",
    [string]$ImagePath = "build-native-exfat/dist/PPSA35820-native.exfat",
    [string]$StageRoot = "build-native-exfat/stage",
    [string]$HdAssetsRoot = "",
    [Parameter(Mandatory = $true)]
    [string]$ImageBuilder,
    [switch]$ReuseStage,
    [switch]$RefreshData,
    [switch]$ForceImageOverwrite,
    # Recover report generation if OSFMount kept a completed image locked.
    # The caller must release that volume first; this mode never reformats it.
    [switch]$FinalizeExistingImage
)

$ErrorActionPreference = "Stop"
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
function Resolve-ProjectPath([string]$value) {
    if ([IO.Path]::IsPathRooted($value)) { return [IO.Path]::GetFullPath($value) }
    return [IO.Path]::GetFullPath((Join-Path $project $value))
}

$eboot = Resolve-ProjectPath $EbootPath
$data = Resolve-ProjectPath $DataExtractedRoot
$libc = Resolve-ProjectPath $LibcPrxPath
$executableValidation = Resolve-ProjectPath $ExecutableValidationPath
$icon = Resolve-ProjectPath $IconPath
$background = Resolve-ProjectPath $BackgroundPath
$rom = if ([IO.Path]::IsPathRooted($RomPath)) { [IO.Path]::GetFullPath($RomPath) } else { Resolve-ProjectPath $RomPath }
$melonRoot = if ([IO.Path]::IsPathRooted($MelonProjectRoot)) { [IO.Path]::GetFullPath($MelonProjectRoot) } else { Resolve-ProjectPath $MelonProjectRoot }
$image = Resolve-ProjectPath $ImagePath
$stageRoot = if ([IO.Path]::IsPathRooted($StageRoot)) { [IO.Path]::GetFullPath($StageRoot) } else { Resolve-ProjectPath $StageRoot }
$hdAssets = if ([string]::IsNullOrWhiteSpace($HdAssetsRoot)) { $null } elseif ([IO.Path]::IsPathRooted($HdAssetsRoot)) { [IO.Path]::GetFullPath($HdAssetsRoot) } else { Resolve-ProjectPath $HdAssetsRoot }
$builder = Resolve-ProjectPath $ImageBuilder
$stage = Join-Path $stageRoot $TitleId

if ($TitleId -notmatch '^PPSA\d{5}$') { throw "TitleId must use the PS5 PPSA##### form" }
foreach ($required in @($eboot, $data, $libc, $icon, $background, $builder, $executableValidation)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required native packaging input missing: $required" }
}
if ([string]::IsNullOrWhiteSpace($RomPath)) { $rom = $null }
if ([string]::IsNullOrWhiteSpace($MelonProjectRoot)) { $melonRoot = $null }
if ($null -ne $hdAssets -and -not (Test-Path -LiteralPath $hdAssets -PathType Container)) {
    throw "HD assets root does not exist: $hdAssets"
}
if ($eboot -like "*build-vulkan-title-4*") {
    throw "Refusing the previous prototype eboot; pass the native port's final eboot.bin"
}
if ((Test-Path -LiteralPath $image) -and -not $ForceImageOverwrite -and -not $FinalizeExistingImage) {
    throw "Output already exists; choose a new ImagePath or pass -ForceImageOverwrite: $image"
}
if ((Test-Path -LiteralPath $stage) -and -not $ReuseStage -and -not $FinalizeExistingImage) {
    throw "Staging directory already exists; pass -ReuseStage to rebuild the image from it: $stage"
}

$ebootFile = Get-Item -LiteralPath $eboot
if ($ebootFile.Length -lt 4096) { throw "Native eboot.bin is unexpectedly small" }
$nativeLink = Get-Content -LiteralPath $executableValidation -Raw | ConvertFrom-Json
$sourceEbootHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $eboot).Hash
if ($nativeLink.fselfSha256 -ne $sourceEbootHash -or
    $nativeLink.fselfIntegrity -ne 'valid' -or
    $null -eq $nativeLink.directSyscallInstructions -or
    $nativeLink.directSyscallInstructions -ne 0) {
    throw 'Eboot is stale or has not passed the native-title syscall audit; relink before packaging'
}
$syscallProof = Get-Content -LiteralPath $nativeLink.syscallValidation -Raw | ConvertFrom-Json
if (-not $syscallProof.passed -or $syscallProof.direct_syscall_count -ne 0 -or
    $syscallProof.decoded_instruction_count -le 0 -or $syscallProof.sha256 -ne $nativeLink.sha256) {
    throw 'Native-title syscall proof does not match the linked ELF'
}
if (-not $nativeLink.recordsValidation) { throw 'Relink with the complete game-record audit before packaging' }
$recordsProof = Get-Content -LiteralPath $nativeLink.recordsValidation -Raw | ConvertFrom-Json
if (-not $recordsProof.passed -or $recordsProof.elf_sha256 -ne $nativeLink.sha256) {
    throw 'Native game initializer inventory is incomplete or stale; refusing to package'
}
if (-not $nativeLink.vulkanDispatchValidation) { throw 'Relink with the Mesa dispatch-table audit before packaging' }
$vulkanProof = Get-Content -LiteralPath $nativeLink.vulkanDispatchValidation -Raw | ConvertFrom-Json
if (-not $vulkanProof.passed -or $vulkanProof.elf_sha256 -ne $nativeLink.sha256 -or
    -not $vulkanProof.properties2_common_dispatch_relocated) {
    throw 'Mesa dispatch callbacks are incomplete or the audit is stale; refusing to package'
}
$dataValidator = Join-Path $PSScriptRoot 'validate_native_game_data.py'
$sourceDataProof = Join-Path $project 'build-native-exfat/data-source.validation.json'
& python $dataValidator --root $data --report $sourceDataProof
if ($LASTEXITCODE -ne 0) { throw 'Native cartridge source data validation failed' }
$nativeData = Get-Content -LiteralPath $sourceDataProof -Raw | ConvertFrom-Json
$data = $nativeData.root

# Decode artwork and check the dimensions before creating the volume.
Add-Type -AssemblyName System.Drawing
foreach ($entry in @(@{file=$icon; width=512; height=512},
                     @{file=$background; width=1920; height=1080})) {
    $bytes = [IO.File]::ReadAllBytes($entry.file)
    if ($bytes.Length -lt 33 -or [BitConverter]::ToString($bytes[0..7]) -ne '89-50-4E-47-0D-0A-1A-0A') {
        throw "Invalid PNG artwork: $($entry.file)"
    }
    $picture = [Drawing.Image]::FromFile($entry.file)
    try {
        if ($picture.Width -ne $entry.width -or $picture.Height -ne $entry.height) {
            throw "Unexpected artwork dimensions: $($entry.file)"
        }
    } finally { $picture.Dispose() }
}

if (-not $FinalizeExistingImage) {
if ($RefreshData) {
    # This is generated staging output only, never the source extraction.
    $stagedData = [IO.Path]::GetFullPath((Join-Path $stage 'data\extracted'))
    $allowedData = [IO.Path]::GetFullPath((Join-Path $stageRoot "$TitleId\data\extracted"))
    if ($stagedData -ne $allowedData -or $data.StartsWith($stagedData, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to refresh a path outside the generated native data staging directory'
    }
    if (Test-Path -LiteralPath $stagedData) {
        Remove-Item -LiteralPath $stagedData -Recurse -Force
    }
}
New-Item -ItemType Directory -Path (Join-Path $stage "sce_sys") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stage "sce_module") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stage "data\extracted") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stage "roms") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stage "branding") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stage "licenses") -Force | Out-Null
Copy-Item -LiteralPath $eboot -Destination (Join-Path $stage "eboot.bin") -Force
Copy-Item -LiteralPath $libc -Destination (Join-Path $stage "sce_module\libc.prx") -Force
Copy-Item -LiteralPath $icon -Destination (Join-Path $stage "sce_sys\icon0.png") -Force
Copy-Item -LiteralPath $background -Destination (Join-Path $stage "sce_sys\pic0.png") -Force

# Keep the same auxiliary layout used by the known-good Melon Mix image.
if ($null -ne $rom -and (Test-Path -LiteralPath $rom)) {
    Copy-Item -LiteralPath $rom -Destination (Join-Path $stage "roms\kh-days.nds") -Force
} elseif ($null -ne $rom) {
    Write-Warning "EU ROM not found; continuing without roms/kh-days.nds: $rom"
}
if ($null -ne $melonRoot) {
    $melonLogo = Join-Path $melonRoot "logo.png"
    $melonBackground = Join-Path $melonRoot "src\frontend\qt_sdl\MainWindow\resources\settings_bg.png"
    $melonLicense = Join-Path $melonRoot "LICENSE"
    $sdlLicense = Join-Path $melonRoot "deps\SDL\LICENSE.txt"
    if (Test-Path -LiteralPath $melonLogo) { Copy-Item -LiteralPath $melonLogo -Destination (Join-Path $stage "branding\logo.png") -Force }
    if (Test-Path -LiteralPath $melonBackground) { Copy-Item -LiteralPath $melonBackground -Destination (Join-Path $stage "branding\background.png") -Force }
    if (Test-Path -LiteralPath $melonLicense) { Copy-Item -LiteralPath $melonLicense -Destination (Join-Path $stage "licenses\KHMelonMix-GPL-3.txt") -Force }
    if (Test-Path -LiteralPath $sdlLicense) { Copy-Item -LiteralPath $sdlLicense -Destination (Join-Path $stage "licenses\SDL2.txt") -Force }
}

if (-not $ReuseStage -or $RefreshData) {
    foreach ($entry in Get-ChildItem -LiteralPath $data -Force) {
        Copy-Item -LiteralPath $entry.FullName -Destination (Join-Path $stage "data\extracted") -Recurse
    }
} else {
    Write-Host "Reusing staging tree; native cartridge will be validated before packaging: $stage"
}

# Optional local HD output from the PC collection. It is copied only into the
# generated staging tree; the PC installation and source extraction stay
# untouched. The native PS5 decoder still has to be enabled separately for
# MP4 playback, but layouts and subtitles can be staged now.
if ($null -ne $hdAssets) {
    foreach ($relative in @('mods', 'data\cutscenes')) {
        $source = Join-Path $hdAssets $relative
        if (-not (Test-Path -LiteralPath $source -PathType Container)) {
            Write-Warning "HD asset subtree not found; skipping: $source"
            continue
        }
        $destination = Join-Path $stage $relative
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        foreach ($entry in Get-ChildItem -LiteralPath $source -Force) {
            Copy-Item -LiteralPath $entry.FullName -Destination $destination -Recurse -Force
        }
    }
}

$stageDataProof = Join-Path $project 'build-native-exfat/data-stage.validation.json'
& python $dataValidator --root (Join-Path $stage 'data\extracted') --report $stageDataProof
if ($LASTEXITCODE -ne 0) { throw 'Stale or invalid native staged data; rebuild with -RefreshData' }
$stagedNativeData = Get-Content -LiteralPath $stageDataProof -Raw | ConvertFrom-Json
if ($stagedNativeData.manifestSha256 -ne $nativeData.manifestSha256) {
    throw 'Staged cartridge differs from requested source extraction; rebuild with -RefreshData'
}

$conceptId = $TitleId.Substring(4)
$param = [ordered]@{
    ageLevel = @{ default = 0 }
    applicationCategoryType = 0
    applicationDrmType = "free"
    attribute = 0
    attribute2 = 0
    attribute3 = 524352
    conceptId = $conceptId
    contentBadgeType = 1
    contentId = "UP9000-${TitleId}_00-KHDAYSNATIVE0001"
    contentVersion = "01.000.019"
    localizedParameters = @{
        defaultLanguage = "en-US"
        'en-US' = @{ titleName = $TitleName }
    }
    masterVersion = "01.00"
    requiredSystemSoftwareVersion = "0x0000000000000000"
    sdkVersion = "0x0000000000000000"
    titleId = $TitleId
    versionFileUri = ""
}
$paramJson = $param | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText((Join-Path $stage "sce_sys\param.json"), $paramJson, (New-Object Text.UTF8Encoding($false)))

$readme = @"
KH-Days PS5 native test image

Title ID: $TitleId
Layout is intentionally compatible with the working KH Melon Mix package:
  /sce_sys/param.json
  /sce_sys/icon0.png
  /sce_sys/pic0.png
  /eboot.bin

This is an unofficial homebrew test build. It prefers the linked RADV Vulkan
compute renderer and VK_KHR_display VideoOut swapchain. The first console test
must confirm WSI startup and rendering; the CPU VideoOut path remains a fallback.
Build 019 links RADV's entire archive so Mesa's weak dispatch callbacks remain
present. A new ELF audit rejects omitted implementations and checks the exact
properties2 dispatch pointer that caused build 015's startup crash. Required
Vulkan commands and startup checkpoints remain validated as in build 015.
The first Vulkan frame may contain only the 2D package even when the display
registers select 3D; the runtime captures its first 3D list for the following
frame. The renderer presents the available 2D frame without reading missing
3D data and reports the package flags for the first presented frames.
GPU resources released during recording remain alive until that command has
completed. Failed recording commands cannot submit as successful frames.
The Vulkan 1.1 request matches the pinned SPIR-V 1.3 shaders. Startup command,
dispatch and WSI checkpoints are limited to the first submissions/frames.
The native one-screen compositor now follows the game's situation/layout data;
stacked and side-by-side compositions remain supported for scenes that require
both screens. The HD menus/HUD and HD video options from the PC installer are
not enabled in this native build: those PC front-end assets and MP4 playback
still need ports.
"@
[IO.File]::WriteAllText((Join-Path $stage "LEIA-ME-TESTE.txt"), $readme, (New-Object Text.UTF8Encoding($false)))

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $builder -ImagePath $image -SourceDir $stage -Label "KHDAYS3582" -ForceOverwrite
if ($LASTEXITCODE -ne 0) { throw "Native exFAT image builder failed: $LASTEXITCODE" }
}

$stream = [IO.File]::OpenRead($image)
try {
    $boot = New-Object byte[] 512
    if ($stream.Read($boot, 0, 512) -ne 512) { throw "Truncated exFAT boot sector" }
} finally { $stream.Dispose() }
if ([Text.Encoding]::ASCII.GetString($boot, 3, 8) -ne "EXFAT   ") { throw "Builder output is not an exFAT volume" }
$sectorBytes = [int64]1 -shl $boot[108]
$clusterBytes = $sectorBytes * ([int64]1 -shl $boot[109])
if ($clusterBytes -ne 65536) { throw "Expected a 64 KiB exFAT cluster, got $clusterBytes" }

$report = [ordered]@{
    image = $image
    imageBytes = (Get-Item -LiteralPath $image).Length
    sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $image).Hash
    titleId = $TitleId
    titleName = $TitleName
    ebootSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $stage "eboot.bin")).Hash
    iconSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $stage "sce_sys/icon0.png")).Hash
    backgroundSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $stage "sce_sys/pic0.png")).Hash
    dataSource = $data
    dataManifestSha256 = $nativeData.manifestSha256
    nitrofsFiles = $nativeData.nitrofsFiles
    extractedFiles = $nativeData.extractedFiles
    sectorBytes = $sectorBytes
    clusterBytes = $clusterBytes
    hardwareTest = "Pending"
    hdAssetsRoot = $hdAssets
    nativeLinkValidation = $executableValidation
    directSyscallInstructions = $nativeLink.directSyscallInstructions
}
$report | ConvertTo-Json | Set-Content -LiteralPath ($image + ".validation.json") -Encoding UTF8
$report | ConvertTo-Json
