[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot,
    [Parameter(Mandatory = $true)]
    [string]$SystemRoot,
    [string]$DestinationRoot = "..\build-full-game-data\data",
    [switch]$CleanDestination
)

$ErrorActionPreference = "Stop"
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$source = [System.IO.Path]::GetFullPath($SourceRoot)
$systemSource = [System.IO.Path]::GetFullPath($SystemRoot)
$destination = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot $DestinationRoot))

if (-not (Test-Path -LiteralPath $source -PathType Container)) {
    throw "Árvore NitroFS não encontrada: $source"
}
if (-not (Test-Path -LiteralPath $systemSource -PathType Container)) {
    throw "Diretório system não encontrado: $systemSource"
}
if ($CleanDestination -and (Test-Path -LiteralPath $destination)) {
    Remove-Item -LiteralPath $destination -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $destination | Out-Null

$files = Get-ChildItem -LiteralPath $source -Recurse -File
$records = @($files | ForEach-Object {
    $file = $_
    $relative = $file.FullName.Substring($source.Length).TrimStart('\', '/')
    $target = Join-Path $destination $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $target -Force
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    [pscustomobject]@{
        path = ($relative -replace '\\', '/')
        bytes = $file.Length
        sha256 = $hash
        extension = if ([string]::IsNullOrEmpty($file.Extension)) { "<none>" } else { $file.Extension.ToLowerInvariant() }
    }
})

$systemRecords = @(Get-ChildItem -LiteralPath $systemSource -Recurse -File |
    ForEach-Object {
        $file = $_
        $relative = $file.FullName.Substring($systemSource.Length).TrimStart('\', '/')
        $manifestPath = "system/$(($relative -replace '\\', '/'))"
        $target = Join-Path (Join-Path $destination "system") $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target -Force
        [pscustomobject]@{
            path = $manifestPath
            bytes = $file.Length
            sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            extension = if ([string]::IsNullOrEmpty($file.Extension)) { "<none>" } else { $file.Extension.ToLowerInvariant() }
        }
    })
$records = @($records) + @($systemRecords)

$requiredRoots = @(
    "ba", "db", "ev", "gameover", "mi", "mo", "mrslt", "Ms", "mv",
    "op", "pause", "snd", "system", "text", "ttl", "UI"
)
$rootReport = foreach ($root in $requiredRoots) {
    $matches = @($records | Where-Object { $_.path -eq $root -or $_.path.StartsWith("$root/") })
    [pscustomobject]@{
        root = $root
        files = $matches.Count
        bytes = [int64](($matches | Measure-Object -Property bytes -Sum).Sum)
        present = $matches.Count -gt 0
    }
}

$manifest = [ordered]@{
    format = "khdays-ps5-full-data-v1"
    source = $source
    generatedUtc = [DateTime]::UtcNow.ToString("o")
    fileCount = $records.Count
    totalBytes = [int64](($records | Measure-Object -Property bytes -Sum).Sum)
    files = $records
    roots = $rootReport
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $destination "asset-manifest.json") -Encoding utf8

Write-Host "Arquivos staged: $($records.Count)"
Write-Host "Bytes staged:    $($manifest.totalBytes)"
Write-Host "Manifesto:       $(Join-Path $destination 'asset-manifest.json')"
