[CmdletBinding()]
param(
    [string]$TitleRoot = "build-vulkan-title-4",
    [string]$ImagePath = "build-vulkan-title-4/dist/PPSA12345-fixed.exfat",
    [Parameter(Mandatory = $true)]
    [string]$ImageBuilder,
    [string]$OsfMount = "C:\Program Files\OSFMount\OSFMount.com"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not [IO.Path]::IsPathRooted($TitleRoot)) {
    $TitleRoot = Join-Path $projectRoot $TitleRoot
}
if (-not [IO.Path]::IsPathRooted($ImagePath)) {
    $ImagePath = Join-Path $projectRoot $ImagePath
}
$TitleRoot = [IO.Path]::GetFullPath($TitleRoot)
$ImagePath = [IO.Path]::GetFullPath($ImagePath)
$bundle = Join-Path $TitleRoot "dist\PPSA12345"
foreach ($required in @($ImageBuilder, $OsfMount, (Join-Path $bundle "eboot.bin"))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required file missing: $required"
    }
}
if (Test-Path -LiteralPath $ImagePath) {
    throw "Output already exists; choose a new ImagePath to preserve it: $ImagePath"
}

# ShadowMountPlus sm_install.c aborts registration if copying icon0.png fails.
# Stage the template's own generated icon and the application's shader pair.
$iconSource = Join-Path $TitleRoot "ps5\sce_sys\icon0.png"
if (-not (Test-Path -LiteralPath $iconSource -PathType Leaf)) {
    throw "Title icon missing: $iconSource"
}
Copy-Item -LiteralPath $iconSource -Destination (Join-Path $bundle "sce_sys\icon0.png") -Force
$picSource = Join-Path $TitleRoot "ps5\sce_sys\pic0.png"
if (-not (Test-Path -LiteralPath $picSource -PathType Leaf)) {
    throw "Title background missing: $picSource"
}
Copy-Item -LiteralPath $picSource -Destination (Join-Path $bundle "sce_sys\pic0.png") -Force
$shaderTarget = Join-Path $bundle "shaders\glsl\khdaysps5"
New-Item -ItemType Directory -Path $shaderTarget -Force | Out-Null
foreach ($shader in @("model.vert.spv", "model.frag.spv")) {
    $shaderSource = Join-Path $TitleRoot "shaders\glsl\khdaysps5\$shader"
    if (-not (Test-Path -LiteralPath $shaderSource -PathType Leaf)) {
        throw "Compiled shader missing: $shaderSource"
    }
    Copy-Item -LiteralPath $shaderSource -Destination $shaderTarget -Force
}

$mandatory = @(
    "eboot.bin", "sce_sys\param.json", "sce_sys\icon0.png", "sce_sys\pic0.png",
    "sce_module\libc.prx", "shaders\glsl\khdaysps5\model.vert.spv",
    "shaders\glsl\khdaysps5\model.frag.spv",
    "shaders\glsl\base\uioverlay.vert.spv",
    "shaders\glsl\base\uioverlay.frag.spv",
    "data\ba\ch\ro\def.p.z", "data\mv\802.mods"
)
foreach ($relative in $mandatory) {
    $file = Get-Item -LiteralPath (Join-Path $bundle $relative)
    if ($file.Length -eq 0) { throw "Empty required file: $relative" }
}
$param = Get-Content -LiteralPath (Join-Path $bundle "sce_sys\param.json") -Raw | ConvertFrom-Json
if ($param.titleId -ne "PPSA12345") { throw "Unexpected titleId" }
if (-not $param.localizedParameters.'en-US'.titleName) { throw "Title name missing" }

# Validate PNG IHDR without installing an image editor or changing the image.
$iconBytes = [IO.File]::ReadAllBytes($iconSource)
$pngMagic = [BitConverter]::ToString($iconBytes[0..7])
if ($pngMagic -ne "89-50-4E-47-0D-0A-1A-0A") { throw "Invalid PNG signature" }
if ([BitConverter]::ToString($iconBytes[16..23]) -ne "00-00-02-00-00-00-02-00") {
    throw "Expected a 512x512 title icon"
}
$picBytes = [IO.File]::ReadAllBytes($picSource)
if ([BitConverter]::ToString($picBytes[0..7]) -ne "89-50-4E-47-0D-0A-1A-0A") { throw "Invalid PNG background signature" }
if ([BitConverter]::ToString($picBytes[16..23]) -ne "00-00-07-80-00-00-04-38") {
    throw "Expected a 1920x1080 title background"
}
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $ImageBuilder -ImagePath $ImagePath -SourceDir $bundle -Label "KHDAYSPS5"
if ($LASTEXITCODE -ne 0) { throw "Image builder failed: $LASTEXITCODE" }

# Inspect raw exFAT geometry before mounting. No physical disk is formatted.
$stream = [IO.File]::OpenRead($ImagePath)
try {
    $boot = New-Object byte[] 512
    if ($stream.Read($boot, 0, $boot.Length) -ne 512) { throw "Truncated boot sector" }
} finally { $stream.Dispose() }
if ([Text.Encoding]::ASCII.GetString($boot, 3, 8) -ne "EXFAT   ") {
    throw "Image is not a raw exFAT volume"
}
$sectorBytes = [int64]1 -shl $boot[108]
$clusterBytes = $sectorBytes * ([int64]1 -shl $boot[109])
if ($clusterBytes -ne 65536) { throw "Unexpected cluster size: $clusterBytes" }

$used = @(Get-PSDrive -PSProvider FileSystem | ForEach-Object Name)
$drive = $null
foreach ($code in 68..90) {
    $candidate = ([char]$code).ToString()
    if ($used -notcontains $candidate) { $drive = $candidate + ":"; break }
}
if (-not $drive) { throw "No free drive letter for verification" }
$mounted = $false
try {
    & $OsfMount -a -t file -m $drive -f $ImagePath -o ro
    if ($LASTEXITCODE -ne 0) { throw "Cannot mount image for verification" }
    $mounted = $true
    $root = $drive + "\"
    $sourceFiles = @(Get-ChildItem -LiteralPath $bundle -Recurse -File)
    $imageFiles = @(Get-ChildItem -LiteralPath $root -Recurse -File)
    if ($sourceFiles.Count -ne $imageFiles.Count) { throw "Image file count mismatch" }
    foreach ($sourceFile in $sourceFiles) {
        $relative = $sourceFile.FullName.Substring($bundle.Length + 1)
        $target = Get-Item -LiteralPath (Join-Path $root $relative)
        if ($sourceFile.Length -ne $target.Length) { throw "Size mismatch: $relative" }
    }
    foreach ($relative in $mandatory) {
        $expected = (Get-FileHash -LiteralPath (Join-Path $bundle $relative) -Algorithm SHA256).Hash
        $actual = (Get-FileHash -LiteralPath (Join-Path $root $relative) -Algorithm SHA256).Hash
        if ($expected -ne $actual) { throw "Hash mismatch: $relative" }
    }
} finally {
    if ($mounted) {
        & $OsfMount -d -m $drive
        if ($LASTEXITCODE -ne 0) { throw "Could not dismount verification volume: $drive" }
    }
}
$report = [ordered]@{
    image = $ImagePath
    imageBytes = (Get-Item -LiteralPath $ImagePath).Length
    sha256 = (Get-FileHash -LiteralPath $ImagePath -Algorithm SHA256).Hash
    sectorBytes = $sectorBytes
    clusterBytes = $clusterBytes
    titleId = $param.titleId
    files = $sourceFiles.Count
    mandatoryFilesHashVerified = $mandatory.Count
    iconDimensions = "512x512"
    hardwareTest = "Pending"
}
$report | ConvertTo-Json | Set-Content -LiteralPath ($ImagePath + ".validation.json") -Encoding UTF8
$report | ConvertTo-Json | Write-Output
