param(
    [Parameter(Mandatory = $true)]
    [string] $TargetPath,
    [double] $RequiredGB = 60
)

$resolved = [System.IO.Path]::GetFullPath($TargetPath)
$root = [System.IO.Path]::GetPathRoot($resolved)
$drive = Get-PSDrive -Name $root.TrimEnd('\').TrimEnd(':') -ErrorAction Stop
$freeGB = [math]::Round($drive.Free / 1GB, 2)

Write-Host ("Destino: {0}" -f $resolved)
Write-Host ("Livre em {0}: {1} GB" -f $root, $freeGB)
Write-Host ("Reserva recomendada: {0} GB" -f $RequiredGB)

if ($freeGB -lt $RequiredGB) {
    Write-Warning "Espaço insuficiente. Este script não remove, move nem altera arquivos."
    exit 2
}

Write-Host "Espaço suficiente para iniciar o download e a extração local."
exit 0
