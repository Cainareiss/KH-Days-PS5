[CmdletBinding()]
param(
    [string]$DataRoot = "..\build-full-game-data\data",
    [string]$OutputPath = "..\build-full-game-data\data\mi\mission-catalog.json"
)

$ErrorActionPreference = "Stop"
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot $DataRoot))
$output = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot $OutputPath))
$missionRoot = Join-Path $root "mi\mi"
$worldRoot = Join-Path $root "mi\wd"

if (-not (Test-Path -LiteralPath $missionRoot -PathType Container)) {
    throw "Diretório de missões não encontrado: $missionRoot"
}

$missions = @(Get-ChildItem -LiteralPath $missionRoot -File |
    Where-Object { $_.Name -match '^\d{4}$' } |
    Sort-Object { [int]$_.Name } |
    ForEach-Object {
        [pscustomobject]@{
            id = [int]$_.Name
            path = "mi/mi/$($_.Name)"
            bytes = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    })

$worlds = @(Get-ChildItem -LiteralPath $worldRoot -File |
    Where-Object { $_.Name -like 'wd_*' } |
    Sort-Object Name |
    ForEach-Object {
        [pscustomobject]@{
            code = $_.Name.Substring(3)
            path = "mi/wd/$($_.Name)"
            bytes = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    })

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $output) | Out-Null
$catalog = [ordered]@{
    format = "khdays-ps5-mission-catalog-v1"
    source = $root
    generatedUtc = [DateTime]::UtcNow.ToString("o")
    missionCount = $missions.Count
    worldCount = $worlds.Count
    missions = $missions
    worlds = $worlds
}
$catalog | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $output -Encoding utf8
Write-Host "Missões:  $($missions.Count)"
Write-Host "Mundos:   $($worlds.Count)"
Write-Host "Catálogo: $output"
