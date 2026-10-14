$ErrorActionPreference = 'SilentlyContinue'

Write-Output 'KH Days PS5 - host/toolchain check'
Write-Output "Host: $env:COMPUTERNAME"
Write-Output "OS: $([System.Environment]::OSVersion.VersionString)"

foreach ($name in @('cmake','git','clang','clang++','prospero-clang','make','wsl')) {
    $command = Get-Command $name
    if ($command) {
        Write-Output ("{0}: {1}" -f $name, $command.Source)
    } else {
        Write-Output ("{0}: NOT_FOUND" -f $name)
    }
}

Write-Output ''
Write-Output 'PS5 SDK candidates under the workspace:'
$candidates = Get-ChildItem -Path (Get-Location) -Directory -Force |
    Where-Object { $_.Name -match 'PS5|prospero|payload|vulkan' }
if ($candidates) {
    $candidates | ForEach-Object { Write-Output $_.FullName }
} else {
    Write-Output 'NONE'
}

