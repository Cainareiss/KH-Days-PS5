param(
    [string]$Root = (Join-Path (Get-Location) 'data/extracted'),
    [ValidateSet('de', 'en', 'es', 'fr', 'it')]
    [string]$Language = 'en'
)

$ErrorActionPreference = 'Stop'
$manifestPath = Join-Path $PSScriptRoot '..\docs\frontend-resources.json'
if (-not (Test-Path -LiteralPath $manifestPath)) {
    Write-Output "ERROR: manifesto não encontrado: $manifestPath"
    exit 2
}
if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
    Write-Output "ERROR: diretório de dados não encontrado: $Root"
    exit 2
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$missingRequired = 0
$missingOptional = 0
Write-Output ("Front-end data check | root={0} | language={1}" -f (Resolve-Path $Root), $Language)

foreach ($scene in $manifest.scenes) {
    Write-Output ("[{0}] {1}" -f $scene.id, $scene.name)
    foreach ($resource in $scene.resources) {
        $relative = $resource.path.Replace('&', $Language)
        $full = Join-Path $Root $relative
        if (Test-Path -LiteralPath $full -PathType Leaf) {
            Write-Output ("  FOUND   {0}" -f $relative)
        } elseif ($resource.required) {
            Write-Output ("  MISSING {0} (required)" -f $relative)
            $missingRequired++
        } else {
            Write-Output ("  MISSING {0} (optional)" -f $relative)
            $missingOptional++
        }
    }
}

Write-Output ''
Write-Output ("Required missing: {0}; optional missing: {1}" -f $missingRequired, $missingOptional)
if ($missingRequired -gt 0) { exit 1 }
exit 0

