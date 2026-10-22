param(
    [string]$TitleRoot = "build-vulkan-title-4",
    [string]$BuildRoot = "build-vulkan-title-4/build/ps5-current",
    [switch]$Stage
)

$ErrorActionPreference = "Stop"
$project = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$llvmBin = Join-Path $project "toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc/bin"
$lld = Join-Path $llvmBin "ld.lld.exe"
$readelf = Join-Path $llvmBin "llvm-readelf.exe"
$nm = Join-Path $llvmBin "llvm-nm.exe"
$sdk = Join-Path $project "toolchains/sources/PS5_Vulkan/.deps/native/ps5-payload-sdk"
$obj = Join-Path $project "$BuildRoot/link/obj"
$oldRsp = Join-Path $project "$BuildRoot/link/link-title.rsp"
$work = Join-Path $project "$BuildRoot/link/bootfix"
New-Item -ItemType Directory -Force -Path $work | Out-Null

if (!(Test-Path $oldRsp)) { throw "Arquivo de link não encontrado: $oldRsp" }
if (!(Test-Path $lld)) { throw "ld.lld não encontrado: $lld" }

$agcObjects = @{
    "libSceAgc" = Join-Path $obj "libSceAgc_stub.o"
    "libSceAgcDriver" = Join-Path $obj "libSceAgcDriver_stub.o"
}
foreach ($library in $agcObjects.Keys) {
    $stubObject = $agcObjects[$library]
    if (!(Test-Path $stubObject)) { throw "Stub AGC não encontrado: $stubObject" }
    $stubSo = Join-Path $work ($library + ".so")
    & $lld -shared -soname ($library + ".prx") -o $stubSo $stubObject
    if ($LASTEXITCODE -ne 0) { throw "Falha ao criar import stub $library.so" }
    $header = (& $readelf -h $stubSo | Out-String)
    if ($header -notmatch "Type:\s+DYN") { throw "$stubSo não é DYN" }
}

$args = @()
$lines = Get-Content -LiteralPath $oldRsp
$skipNextOutput = $false
foreach ($line in $lines) {
    $arg = $line.Trim()
    if ($arg.Length -eq 0) { continue }
    if ($skipNextOutput) {
        $args += $work + "/llvm-pie.elf"
        $skipNextOutput = $false
        continue
    }
    if ($arg -eq "-o") {
        $args += $arg
        $skipNextOutput = $true
        continue
    }
    if ($arg -match "[/\\]obj[/\\](libSceAgc(?:Driver)?)_stub\.o$") {
        $args += ($work + "/" + $Matches[1] + ".so")
    } else {
        $args += $arg
    }
}
if ($skipNextOutput) { throw "Resposta de link termina após -o" }
$args = @("-pie", "-z", "max-page-size=0x4000") + $args
$newRsp = Join-Path $work "link.rsp"
Set-Content -LiteralPath $newRsp -Value ($args -join "`n") -Encoding ascii
if ((Select-String -LiteralPath $newRsp -Pattern "libSceAgc(?:Driver)?_stub\.o" -Quiet)) { throw "Stub AGC .o ainda ficou no link" }

& $lld "@$newRsp"
if ($LASTEXITCODE -ne 0) { throw "ld.lld falhou" }
$elf = Join-Path $work "llvm-pie.elf"
$defs = (& $nm --defined-only $elf | Select-String "\s[TtWw]\s+sceAgc")
if ($defs) { throw "O ELF ainda define símbolos AGC localmente: $($defs -join '; ')" }
$undef = (& $nm -D --undefined-only $elf | Out-String)
foreach ($symbol in @("sceAgcInit", "sceAgcDriverSubmitDcb")) {
    if ($undef -notmatch "\b$symbol\b") { throw "Importação dinâmica ausente: $symbol" }
}

$hostTool = Join-Path $project "toolchains/sources/PS5_Vulkan/build/host/ps5-native-tool"
$tool = Join-Path $work "ps5-native-tool.exe"
Copy-Item -LiteralPath $hostTool -Destination $tool -Force
$zlib = Join-Path (Split-Path $hostTool) "z.dll"
if (Test-Path $zlib) { Copy-Item -LiteralPath $zlib -Destination (Join-Path $work "z.dll") -Force }
$converted = Join-Path $work "eboot.elf"
$signed = Join-Path $work "eboot.bin"
& $tool link --in $elf --out $converted --stub-dir (Join-Path $sdk "target/lib") --stub (Join-Path $work "libSceAgc.so") --stub (Join-Path $work "libSceAgcDriver.so") --module-sdk 0x02000009 --companion-sdk 0x08050001 --file-name eboot.elf
if ($LASTEXITCODE -ne 0) { throw "Conversão para eboot.elf falhou" }
& $tool self --sign --in $converted --out $signed --magic 0x1D3D154F
if ($LASTEXITCODE -ne 0) { throw "Assinatura FSELF falhou" }
$inspect = (& $tool self --inspect --file $signed | Out-String)
if ($inspect -notmatch "integrity:\s*valid") { throw "Inspeção FSELF não confirmou integrity valid: $inspect" }

$dist = Join-Path $project "$TitleRoot/dist/PPSA12345/eboot.bin"
if ($Stage) {
    if (Test-Path $dist) { Copy-Item -LiteralPath $dist -Destination (Join-Path $work "eboot-before-agc-fix.bin") -Force }
    Copy-Item -LiteralPath $signed -Destination $dist -Force
}
$report = [ordered]@{ correction = "AGC import stubs linked as DYN, not executable placeholder functions"; elf = (Get-FileHash $elf -Algorithm SHA256).Hash; fself = (Get-FileHash $signed -Algorithm SHA256).Hash; agcCodeDefinitions = 0; consoleBoot = "Pending firmware 13.60 test" }
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $work "validation.json") -Encoding utf8
Write-Output ($report | ConvertTo-Json -Compress)
