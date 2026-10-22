[CmdletBinding()]
param(
    [string]$IconSource = "build-vulkan-title-4/ps5/sce_sys/icon0.png",
    [string]$BackgroundSource = "build-vulkan-title-4/ps5/sce_sys/pic0.png",
    [string]$OutputRoot = "build-native-link/PPSA35820"
)

$ErrorActionPreference = "Stop"
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
function ProjectPath([string]$path) {
    if ([IO.Path]::IsPathRooted($path)) { return [IO.Path]::GetFullPath($path) }
    return [IO.Path]::GetFullPath((Join-Path $project $path))
}
$icon = ProjectPath $IconSource
$background = ProjectPath $BackgroundSource
$output = ProjectPath $OutputRoot
Add-Type -AssemblyName System.Drawing
function InspectPng([string]$path, [int]$width, [int]$height) {
    $bytes = [IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 33 -or [BitConverter]::ToString($bytes[0..7]) -ne '89-50-4E-47-0D-0A-1A-0A') {
        throw "Not a PNG image: $path"
    }
    $image = [Drawing.Image]::FromFile($path)
    try {
        if ($image.Width -ne $width -or $image.Height -ne $height) {
            throw "Expected ${width}x${height}, got $($image.Width)x$($image.Height): $path"
        }
    } finally { $image.Dispose() }
    return [ordered]@{ source = $path; width = $width; height = $height;
        sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
}
$iconInfo = InspectPng $icon 512 512
$backgroundInfo = InspectPng $background 1920 1080
$sce = Join-Path $output "sce_sys"
New-Item -ItemType Directory -Path $sce -Force | Out-Null
foreach ($entry in @(@{ input=$icon; name="icon0.png"; hash=$iconInfo.sha256 },
                     @{ input=$background; name="pic0.png"; hash=$backgroundInfo.sha256 })) {
    $destination = Join-Path $sce $entry.name
    if ((Test-Path -LiteralPath $destination) -and
        (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $entry.hash) {
        throw "Preserving different artwork already present: $destination"
    }
    if (-not (Test-Path -LiteralPath $destination)) {
        Copy-Item -LiteralPath $entry.input -Destination $destination
    }
    if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $entry.hash) {
        throw "Artwork copy verification failed: $destination"
    }
}
$report = [ordered]@{ titleRoot=$output; icon=$iconInfo; background=$backgroundInfo;
    copiedWithoutConversion=$true; hardwarePresentation="pending native executable" }
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output "artwork.validation.json") -Encoding UTF8
$report | ConvertTo-Json -Depth 5
