param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$Output,
    [ValidateSet('de', 'en', 'es', 'fr', 'it')]
    [string]$Language = 'en'
)

$ErrorActionPreference = 'Stop'
$manifestPath = Join-Path $PSScriptRoot '..\docs\frontend-resources.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Manifesto não encontrado: $manifestPath"
}
if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
    throw "Diretório de extração não encontrado: $Root"
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$rootFull = (Resolve-Path -LiteralPath $Root).Path
$outputFull = [IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force -Path $outputFull | Out-Null

$staged = [System.Collections.Generic.List[object]]::new()
$missingRequired = [System.Collections.Generic.List[string]]::new()

foreach ($scene in $manifest.scenes) {
    foreach ($resource in $scene.resources) {
        $relative = $resource.path.Replace('&', $Language)
        $source = Join-Path $rootFull $relative
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            if ($resource.required) { $missingRequired.Add($relative) }
            continue
        }

        $destination = Join-Path $outputFull $relative
        $destinationDirectory = Split-Path -Parent $destination
        New-Item -ItemType Directory -Force -Path $destinationDirectory | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
        $staged.Add([pscustomobject]@{
            scene = $scene.name
            path = $relative
            kind = $resource.kind
            bytes = (Get-Item -LiteralPath $source).Length
        })
    }
}

if ($missingRequired.Count -gt 0) {
    throw ("Recursos obrigatórios ausentes: " + ($missingRequired -join ', '))
}

$metadata = [pscustomobject]@{
    schema = 1
    language = $Language
    source_root = $rootFull
    resources = $staged
}
$metadata | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $outputFull 'frontend-stage.json') -Encoding utf8
Write-Output ("Staged {0} front-end resources for language {1} into {2}" -f $staged.Count, $Language, $outputFull)
