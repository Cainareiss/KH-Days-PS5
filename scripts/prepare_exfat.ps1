[CmdletBinding()]
param(
    [string]$BundleRoot = "..\build-ps5-app\PPSA12345",
    [string]$OutputRoot = "..\build-ps5-exfat-final\homebrew\PPSA12345",
    [string]$UsbRoot = "",
    [Parameter(Mandatory = $true)]
    [string]$AssetRoot
)

$ErrorActionPreference = "Stop"
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$source = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot $BundleRoot))
$destination = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot $OutputRoot))
if (-not (Test-Path -LiteralPath (Join-Path $source "eboot.bin") -PathType Leaf)) {
    throw "FSELF não encontrado. Execute package_ps5_homebrew.ps1 primeiro."
}

New-Item -ItemType Directory -Force -Path $destination | Out-Null
Copy-Item -LiteralPath (Join-Path $source "eboot.bin") -Destination $destination -Force
Copy-Item -LiteralPath (Join-Path $source "sce_sys") -Destination $destination -Recurse -Force
$dataDestination = Join-Path $destination "data"
New-Item -ItemType Directory -Force -Path $dataDestination | Out-Null

# First-world data from the user's extracted USA ROM. Missing entries are
# reported but do not abort, because some releases store them in a container.
$assetPaths = @(
    "mi/wd/wd_tt",
    "mi/mi/0001",
    "UI/btl/main.p2",
    "UI/btl/face.p2",
    "UI/btl/mapchr.p2",
    "UI/btl/en/main.p2",
    "UI/btl/en/map.p2",
    "text/font_eu_08.nftr",
    "ba/ch/ro/def.p.z"
)
$assetReport = foreach ($relative in $assetPaths) {
    $input = Join-Path $AssetRoot ($relative -replace '/', '\\')
    $target = Join-Path $dataDestination ($relative -replace '/', '\\')
    if (Test-Path -LiteralPath $input -PathType Leaf) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item -LiteralPath $input -Destination $target -Force
        [pscustomobject]@{ path = $relative; copied = $true }
    } else {
        Write-Warning "Asset ausente no extrato: $relative"
        [pscustomobject]@{ path = $relative; copied = $false }
    }
}
$assetReport | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dataDestination "asset-manifest.json") -Encoding utf8
Set-Content -LiteralPath (Join-Path $destination "README-EXFAT.txt") -Encoding utf8 -Value @"
KH-Days-PS5 WIP - bundle homebrew

Copie a pasta homebrew inteira para a raiz de um dispositivo exFAT. Ela não
formata o dispositivo e não altera nenhum arquivo fora da pasta de saída.
O carregador homebrew deve apontar para eboot.bin; não renomeie este arquivo
para PKG. Os assets em data/ são dados extraídos locais e ainda dependem dos
decodificadores P2/.z do renderer Vulkan.
"@

if ($UsbRoot -ne "") {
    $usb = [System.IO.Path]::GetFullPath($UsbRoot)
    if (-not (Test-Path -LiteralPath $usb -PathType Container)) {
        throw "Destino USB não encontrado: $usb"
    }
    $usbTarget = Join-Path $usb "homebrew\PPSA12345"
    New-Item -ItemType Directory -Force -Path $usbTarget | Out-Null
    Copy-Item -LiteralPath (Join-Path $destination "*") -Destination $usbTarget -Recurse -Force
    Write-Host "Copiado para: $usbTarget"
}
Write-Host "Pasta exFAT pronta: $destination"
