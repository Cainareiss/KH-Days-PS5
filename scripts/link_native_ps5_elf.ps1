[CmdletBinding()]
param(
    [string]$BuildDir = "build-native-game-ps5",
    [string]$OutputName = "khdays-days-runtime.elf",
    [string]$MainObject = ""
)

$ErrorActionPreference = "Stop"
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
function ProjectPath([string]$value) {
    if ([IO.Path]::IsPathRooted($value)) { return [IO.Path]::GetFullPath($value) }
    return [IO.Path]::GetFullPath((Join-Path $project $value))
}

$build = ProjectPath $BuildDir
$sdk = ProjectPath "toolchains/sources/PS5_Vulkan/.deps/native/ps5-payload-sdk/target/lib"
$sdkInclude = ProjectPath "toolchains/sources/PS5_Vulkan/.deps/native/ps5-payload-sdk/target/include"
$agcSources = ProjectPath "toolchains/sources/PS5_Vulkan/vendor/ps5/sdk/stubs"
$agcSource = Join-Path $agcSources 'agc_canary_link_stub.c'
$agcDriverSource = Join-Path $agcSources 'agc_driver_canary_link_stub.c'
$llvm = ProjectPath "toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc/bin"
$ld = Join-Path $llvm "ld.lld.exe"
$clang = Join-Path $llvm "clang.exe"
$clangxx = Join-Path $llvm "clang++.exe"
$readelf = Join-Path $llvm "llvm-readelf.exe"
$nm = Join-Path $llvm "llvm-nm.exe"
$nativeTool = ProjectPath "toolchains/sources/PS5_Vulkan/build/host/ps5-native-tool.exe"
$converted = Join-Path $build "khdays-days-runtime.ps5.elf"
$fself = Join-Path $build "eboot.bin"
$appCrtSource = ProjectPath "toolchains/sources/PS5_Vulkan/tooling/native/app_crt.cpp"
$appCrtObject = Join-Path $build "app_crt_ps5.o"
$fpSource = ProjectPath "native/ps5/fp_ieee_ps5.c"
$fpObject = Join-Path $build "fp_ieee_ps5.o"
$compatSource = ProjectPath 'native/ps5/title_compat_ps5.c'
$compatObject = Join-Path $build 'title_compat_ps5.o'
$syscallAudit = ProjectPath 'scripts/validate_native_title_syscalls.py'
$recordsAudit = ProjectPath 'scripts/validate_native_link_records.py'
$vulkanAudit = ProjectPath 'scripts/validate_native_vulkan_link.py'
$objdump = Join-Path $llvm 'llvm-objdump.exe'
$main = if ($MainObject -ne "") { ProjectPath $MainObject } else {
    Join-Path $build "CMakeFiles/khdays-native-ps5-elf.dir/ps5/main_ps5.c.o"
}
$out = Join-Path $build $OutputName
$script = ProjectPath "native/ps5/title_layout.ld"
$tables = ProjectPath "native/ps5/game_tables.ld"
$radv = ProjectPath "toolchains/sources/PS5_Vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
$archives = @(
    "libkhdays-native-ps5-runtime.a", "libkhdays-native-ps5-context.a",
    "libkhdays-native-ps5-host-contract.a", "libkhdays-native-ps5-game-tables.a",
    "libkhdays-native-ps5-game.a", "libkhdays-native-ps5-gpu.a",
    "libkhdays-native-ps5-gfx.a", "libkhdays-native-ps5-sound.a") |
    ForEach-Object { Join-Path $build $_ }
$archives += $radv
$sdkLibs = @(
    "libc++.a", "libc++abi.a", "libunwind.a", "libps5platform.a") |
    ForEach-Object { Join-Path $sdk $_ }
$stubs = @(Get-ChildItem -LiteralPath $sdk -Filter '*.so' | Where-Object {
    $_.Name -notin @('libkernel_sys.so', 'libkernel_web.so',
        'libkernel_stub_weak.so', 'libc_stub_weak.so', 'libScePosixForWebKit.so')
})

foreach ($required in @($ld, $clangxx, $readelf, $nm, $nativeTool, $main, $script, $tables, $radv,
                        $clang, $agcSource, $agcDriverSource, $appCrtSource, $fpSource, $compatSource,
                        $syscallAudit, $recordsAudit, $vulkanAudit, $objdump) + $archives + $sdkLibs) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing native link input: $required"
    }
}

# Match the PS5 title CRT and linker layout used by Melon Mix. This changes
# the process entry and segment layout; only a console retest can establish
# whether it addresses the observed startup crash.
& $clangxx --target=x86_64-sie-ps5 -isystem (Join-Path $sdkInclude "c++/v1") -isystem $sdkInclude -std=c++17 -fPIC `
    -fno-exceptions -fno-rtti -fno-stack-protector -c $appCrtSource -o $appCrtObject
if ($LASTEXITCODE -ne 0) { throw "PS5 app CRT compilation failed ($LASTEXITCODE)" }
& (Join-Path $llvm "clang.exe") --target=x86_64-sie-ps5 -isystem $sdkInclude -fPIC `
    -fno-stack-protector -c $fpSource -o $fpObject
if ($LASTEXITCODE -ne 0) { throw "PS5 FP setup compilation failed ($LASTEXITCODE)" }
& (Join-Path $llvm 'clang.exe') --target=x86_64-sie-ps5 -isystem $sdkInclude -fPIC `
    -fno-stack-protector -Wall -Wextra -Werror -c $compatSource -o $compatObject
if ($LASTEXITCODE -ne 0) { throw "PS5 native compatibility compilation failed ($LASTEXITCODE)" }

# RADV uses system AGC imports. Build export-only link descriptors; they let
# ld.lld retain the unresolved AGC names as dynamic imports, then
# ps5-native-tool rewrites those names to the real system NIDs before signing.
$agcObject = Join-Path $build 'libSceAgc_link_stub.o'
$agcDriverObject = Join-Path $build 'libSceAgcDriver_link_stub.o'
$agcStub = Join-Path $build 'libSceAgc.so'
$agcDriverStub = Join-Path $build 'libSceAgcDriver.so'
& $clang --target=x86_64-sie-ps5 -isystem $sdkInclude -fPIC -fvisibility=default -ffunction-sections -fdata-sections `
    -c $agcSource -o $agcObject
if ($LASTEXITCODE -ne 0) { throw "PS5 libSceAgc link stub compilation failed ($LASTEXITCODE)" }
& $clang --target=x86_64-sie-ps5 -isystem $sdkInclude -fPIC -fvisibility=default -ffunction-sections -fdata-sections `
    -c $agcDriverSource -o $agcDriverObject
if ($LASTEXITCODE -ne 0) { throw "PS5 libSceAgcDriver link stub compilation failed ($LASTEXITCODE)" }
& $ld --shared --no-pie -soname 'libSceAgc.prx' -o $agcStub $agcObject
if ($LASTEXITCODE -ne 0) { throw "PS5 libSceAgc import descriptor creation failed ($LASTEXITCODE)" }
& $ld --shared --no-pie -soname 'libSceAgcDriver.prx' -o $agcDriverStub $agcDriverObject
if ($LASTEXITCODE -ne 0) { throw "PS5 libSceAgcDriver import descriptor creation failed ($LASTEXITCODE)" }

# A native title must not link the payload SDK's libc/dl/ufs implementations:
# they embed raw syscall instructions (notably mmap and mprotect) in eboot.
# Resolve the title's POSIX calls via platform replacements and real modules,
# following Melon Mix's native-title link contract instead.
$platformSymbols = & $nm --defined-only (Join-Path $sdk 'libps5platform.a') | Out-String
$platformAliases = @'
qsort_r mkstemps openlog popen pclose open_memstream __xuname __assert __memset_chk
regcomp regexec regfree regerror localtime_r newlocale freelocale strtod_l strtof_l dladdr
utimensat localeconv_l strtoll_l strtoull_l strtold_l snprintf_l sscanf_l asprintf_l
strcoll_l strxfrm_l strftime_l wcscoll_l wcsxfrm_l btowc_l wctob_l iswctype_l
mbrlen_l mbrtowc_l mbsrtowcs_l mbsnrtowcs_l wcrtomb_l wcsnrtombs_l mbtowc_l
___mb_cur_max_l ___runetype_l ___tolower_l ___toupper_l __runes_for_locale
catopen catgets catclose backtrace backtrace_symbols_fd __cxa_thread_atexit_impl
arc4random arc4random_buf arc4random_uniform gmtime_r statvfs fstatvfs futimens
clock_nanosleep getaddrinfo freeaddrinfo if_nameindex if_freenameindex opendir
fdopendir readdir rewinddir dirfd closedir nl_langinfo nl_langinfo_l getpwuid_r
posix_fallocate access openat unlinkat fchmodat fstatat mkdirat renameat
memfd_create strcasestr mkstemp readlink link symlink localeconv realpath getcwd
'@ -split '\s+' | Where-Object { $_ }
$aliasArgs = @()
$localNames = @('_Zn*', '_Zd*', 'pathconf')
foreach ($name in $platformAliases) {
    if ($platformSymbols -match "\bT ps5_$([regex]::Escape($name))\s") {
        $aliasArgs += "--defsym=$name=ps5_$name"
        $localNames += $name
    }
}
foreach ($name in @('pthread_create', 'pthread_join', 'pthread_detach', 'fclose', 'fflush',
    'malloc', 'calloc', 'realloc', 'free', 'posix_memalign', 'aligned_alloc', 'memalign',
    'malloc_usable_size', 'reallocf', 'reallocarray', 'getline', 'getdelim')) {
    $aliasArgs += "--wrap=$name"
}
$symbolMap = Join-Path $build 'native-local-symbols.map'
$mapText = (@('{ local:') + @($localNames | ForEach-Object { "  $_;" }) + @('};')) -join "`n"
[IO.File]::WriteAllText($symbolMap, $mapText, [Text.Encoding]::ASCII)

# The upstream game target is OBJECT, not a lazily extracted static library.
# Rewritten references are numeric DS addresses, so no undefined native symbol
# pulls in a data-only object. KEEP preserves sections only AFTER extraction.
# Include the complete game archive, including every module's initializers and
# static-function registry. RADV must also be linked whole: Mesa's weak
# dispatch-table references do not extract archive members. The lazy link
# omitted 38 provided callbacks and crashed in wsi_device_init's properties2
# query. This is the same requirement as PS5_Vulkan/tools/radv-link.sh.
# Leave the SDK/runtime libraries lazy.
$archiveArgs = @()
foreach ($archive in $archives) {
    if ([IO.Path]::GetFileName($archive) -in @('libkhdays-native-ps5-game.a', 'libvulkan_radeon.ps5.a')) {
        $archiveArgs += @('--whole-archive', $archive, '--no-whole-archive')
    } else {
        $archiveArgs += $archive
    }
}
$args = @(
    "-pie", "-e", "_start", "--exclude-libs", "ALL",
    "-z", "max-page-size=0x4000", "--eh-frame-hdr", "-L", $sdk,
    "--version-script", $symbolMap,
    "-T", $script, "-T", $tables, "-o", $out,
    $main, $fpObject, $compatObject, $appCrtObject) + $aliasArgs + @("--start-group") +
    $archiveArgs + $sdkLibs + @("--end-group", "--as-needed") +
    @($stubs | ForEach-Object FullName) + @("--no-as-needed", $agcStub, $agcDriverStub)

& $ld @args
if ($LASTEXITCODE -ne 0) { throw "ld.lld failed ($LASTEXITCODE)" }
$vulkanReport = Join-Path $build 'native-vulkan-dispatch-validation.json'
& python $vulkanAudit --elf $out --archive $radv --report $vulkanReport
if ($LASTEXITCODE -ne 0) { throw 'Mesa dispatch callbacks were omitted; refusing to sign' }
$nativeSymbols = & $nm --defined-only $out | Out-String
foreach ($symbol in @('khdays_vk_device_open', 'khdays_native_vulkan_renderer_draw',
    'khdays_vk_present', 'vk_icdGetInstanceProcAddr')) {
    if ($nativeSymbols -notmatch [regex]::Escape($symbol)) {
        throw "Linked native title is missing required Vulkan symbol: $symbol"
    }
}
$recordsReport = Join-Path $build 'native-records-validation.json'
& python $recordsAudit --archive (Join-Path $build 'libkhdays-native-ps5-game.a') `
    --elf $out --report $recordsReport --check-game-bg-table
if ($LASTEXITCODE -ne 0) { throw 'Native game initializer/function registry is incomplete; refusing to sign' }

$header = & $readelf -h $out | Out-String
if ($header -notmatch "Type:\s+DYN" -or $header -notmatch "Machine:\s+Advanced Micro Devices X86-64") {
    throw "Linked output is not an ELF64 PIE for x86-64: $out"
}
$entryMatch = [regex]::Match($header, "Entry point address:\s*(0x[0-9a-fA-F]+)")
if (-not $entryMatch.Success -or $entryMatch.Groups[1].Value -eq "0x0") {
    throw "PS5 title ELF has no non-zero process entry point: $out"
}
$programHeaders = & $readelf -l $out | Out-String
$loadSegments = @([regex]::Matches($programHeaders, "(?m)^\s*LOAD\s"))
if ($loadSegments.Count -ne 4) {
    throw "Expected four title PT_LOAD segments (RX, R, RELRO, RW); got $($loadSegments.Count)"
}
$dynamic = & $readelf --dyn-syms --wide $out
$defined = @($dynamic | Where-Object { $_ -match "GLOBAL|WEAK" -and $_ -notmatch " UND " })
if ($defined.Count -ne 0) {
    throw "Application exports remain in .dynsym; use --exclude-libs ALL"
}
$needed = & $readelf -d $out | Out-String
if ($needed -notmatch "Shared library:\s+\[libkernel(?:\.so|\.sprx)\]" -or
    $needed -match "libkernel_web") {
    throw "PS5 title must depend on libkernel, not libkernel_web"
}
$syscallReport = Join-Path $build 'native-syscall-validation.json'
& python $syscallAudit --elf $out --objdump $objdump --report $syscallReport
if ($LASTEXITCODE -ne 0) { throw 'Direct syscall instruction found in native title; refusing to sign' }

# Use the same SDK metadata and SELF magic as the known-good Melon Mix title.
$convertArgs = @(
    "link", "--in", $out, "--out", $converted, "--stub-dir", $sdk,
    "--stub", $agcStub, "--stub", $agcDriverStub,
    "--module-sdk", "0x02000009", "--companion-sdk", "0x08050001",
    "--file-name", "eboot.elf")
& $nativeTool @convertArgs
if ($LASTEXITCODE -ne 0) { throw "PS5 NID/import conversion failed ($LASTEXITCODE)" }
$convertedDynamic = & $readelf -d $converted | Out-String
if ($convertedDynamic -notmatch "libSceAgc\.prx" -or
    $convertedDynamic -notmatch "libSceAgcDriver\.prx") {
    throw "RADV's libSceAgc/libSceAgcDriver imports are missing from the converted ELF"
}
$signArgs = @("self", "--sign", "--in", $converted, "--out", $fself, "--magic", "0x1D3D154F")
& $nativeTool @signArgs
if ($LASTEXITCODE -ne 0) { throw "PS5 FSELF signing failed ($LASTEXITCODE)" }
$inspection = & $nativeTool self --inspect --file $fself | Out-String
if ($LASTEXITCODE -ne 0 -or $inspection -notmatch "integrity:\s*valid") {
    throw "PS5 FSELF inspection failed: $inspection"
}

$report = [ordered]@{
    stage = "native_ps5_elf_linked"
    elf = $out
    bytes = (Get-Item -LiteralPath $out).Length
    sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $out).Hash
    entry = "_start"
    entryAddress = $entryMatch.Groups[1].Value
    loadSegments = $loadSegments.Count
    kernelDependency = "libkernel (non-web)"
    applicationExports = 0
    directSyscallInstructions = 0
    renderer = "RADV Vulkan compute + VK_KHR_display VideoOut swapchain"
    vulkanSymbols = @("khdays_vk_device_open", "khdays_native_vulkan_renderer_draw",
                     "khdays_vk_present", "vk_icdGetInstanceProcAddr")
    agcModules = @("libSceAgc.prx", "libSceAgcDriver.prx")
    syscallValidation = $syscallReport
    recordsValidation = $recordsReport
    vulkanDispatchValidation = $vulkanReport
    convertedElfSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $converted).Hash
    fself = $fself
    fselfSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $fself).Hash
    fselfIntegrity = "valid"
    next = "rebuild exFAT, then capture a fresh console log"
}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $build "native-elf-validation.json") -Encoding UTF8
$report | ConvertTo-Json
