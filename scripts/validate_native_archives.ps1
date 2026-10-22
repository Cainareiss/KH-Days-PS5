[CmdletBinding()]
param([string]$BuildDir = "build-native-host-ps5")

$ErrorActionPreference = "Stop"
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$build = if ([IO.Path]::IsPathRooted($BuildDir)) { [IO.Path]::GetFullPath($BuildDir) }
         else { [IO.Path]::GetFullPath((Join-Path $project $BuildDir)) }
$llvm = Join-Path $project "toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc/bin"
$expected = [ordered]@{
    context = @("khdays_context_create", "khdays_context_switch", "khdays_context_jump")
    'host-contract' = @("khdays_clock_init", "khdays_clock_cycles")
    'game-tables' = @("khdays_data_init", "khdays_function_lookup", "khdays_reference")
    gpu = @("khdays_gpu2d_frame", "khdays_gpu3d_draw", "khdays_tex_decode")
    gfx = @("khdays_gfx_compose_cpu", "khdays_g3d_command")
    sound = @("khdays_sound_archive_open", "stb_vorbis_open_memory", "drflac_open_memory")
}
$records = @()
foreach ($component in $expected.Keys) {
    $archive = Join-Path $build "libkhdays-native-ps5-$component.a"
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) { throw "Archive missing: $archive" }
    $members = @(& (Join-Path $llvm "llvm-ar.exe") t $archive)
    if ($LASTEXITCODE -ne 0 -or $members.Count -eq 0) { throw "Cannot read archive: $archive" }
    $headers = (& (Join-Path $llvm "llvm-readelf.exe") -h $archive | Out-String)
    if ($LASTEXITCODE -ne 0) { throw "Invalid ELF object in $archive" }
    if ([regex]::Matches($headers, 'Class:\s+ELF64').Count -ne $members.Count -or
        [regex]::Matches($headers, 'Type:\s+REL').Count -ne $members.Count -or
        [regex]::Matches($headers, 'Machine:\s+Advanced Micro Devices X86-64').Count -ne $members.Count) {
        throw "Unexpected target object in $archive"
    }
    $symbols = (& (Join-Path $llvm "llvm-nm.exe") -g --defined-only $archive | Out-String)
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect symbols in $archive" }
    foreach ($name in $expected[$component]) {
        if ($symbols -notmatch ("(?m)\b" + [regex]::Escape($name) + '\s*$')) {
            throw "Required function $name is absent from $archive"
        }
    }
    $records += [ordered]@{ component=$component; archive=$archive; objects=$members.Count;
        sha256=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash;
        required_symbols=$expected[$component] }
}
$report = [ordered]@{ target="x86_64-sie-ps5"; archives=$records; result="pass";
    scope="platform, ELF tables, software graphics, geometry and sound compilation";
    final_eboot="not linked"; hardware="not tested" }
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $build "archives.validation.json") -Encoding UTF8
$report | ConvertTo-Json -Depth 6
