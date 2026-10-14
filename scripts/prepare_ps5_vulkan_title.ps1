[CmdletBinding()]
param(
    [string]$TitleRoot = "..\build-vulkan-title",
    [string]$TemplateRoot = "..\toolchains\sources\PS5_VulkanTemplate",
    [string]$PcPortRoot = "E:\DOCUMENTOS\KH-ReCoded-PTBR\tools\khdays-port",
    [string]$DataRoot = "..\build-full-game-data\data",
    [switch]$StageData
)

$ErrorActionPreference = "Stop"
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot ".."))
$title = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot $TitleRoot))
$template = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot $TemplateRoot))
$pcPort = [System.IO.Path]::GetFullPath($PcPortRoot)
$data = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot $DataRoot))

if (-not (Test-Path -LiteralPath (Join-Path $template "ps5\tools\new-title.py"))) {
    throw "PS5_VulkanTemplate não encontrado: $template"
}
if (-not (Test-Path -LiteralPath (Join-Path $pcPort "include\khdays\assets\mods_video.h"))) {
    throw "Port PC/decodificadores não encontrado: $pcPort"
}

if (-not (Test-Path -LiteralPath $title)) {
    # Git for Windows treats repositories on exFAT as different-owner because
    # the filesystem has no ownership metadata. Trust only this exact generated
    # directory before new-title.py initializes and commits it.
    $titleGitPath = $title.Replace('\', '/')
    & git config --global --add safe.directory $titleGitPath
    if ($LASTEXITCODE -ne 0) {
        throw "Não foi possível registrar safe.directory para $title"
    }
    $python = (Get-Command python -ErrorAction Stop).Source
    $shim = Join-Path $projectRoot "build-python-shim"
    New-Item -ItemType Directory -Force -Path $shim | Out-Null
    # new-title.py invokes the Unix spelling `python3` as a child process.
    # Windows subprocess does not consistently resolve a .cmd shim, so keep a
    # small copy of the active launcher under the expected executable name.
    Copy-Item -LiteralPath $python -Destination (Join-Path $shim "python3.exe") -Force
    $oldPath = $env:Path
    try {
        $env:Path = "$shim;$oldPath"
        & $python (Join-Path $template "ps5\tools\new-title.py") $title `
            --title-id PPSA12345 --name "KH Days PS5" --refresh 60
        if ($LASTEXITCODE -ne 0) {
            throw "new-title.py falhou com código $LASTEXITCODE"
        }
    } finally {
        $env:Path = $oldPath
    }
}

$program = Join-Path $title "examples\khdaysps5\khdaysps5.cpp"
$prelude = Join-Path $title "ps5\src\prelude.h"
$cmake = Join-Path $title "ps5\CMakeLists.txt"
foreach ($required in @($program, $prelude, $cmake)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Título gerado incompatível; arquivo ausente: $required"
    }
}

Copy-Item -LiteralPath (Join-Path $projectRoot "platform\ps5\title\khdaysps5.cpp") `
    -Destination $program -Force

$preludeText = Get-Content -LiteralPath $prelude -Raw -Encoding UTF8
if (-not $preludeText.Contains('#include "native_media_runtime.h"')) {
    $anchor = '#include "VulkanFrameBuffer.hpp"'
    if (-not $preludeText.Contains($anchor)) {
        throw "Âncora de prelude.h não encontrada"
    }
    $preludeText = $preludeText.Replace(
        $anchor, "$anchor`n#include `"native_media_runtime.h`"")
    Set-Content -LiteralPath $prelude -Value $preludeText -Encoding UTF8 -NoNewline
}

$projectUnix = $projectRoot.Replace('\', '/')
$pcUnix = $pcPort.Replace('\', '/')
$integration = @'
# BEGIN KHDAYS_PS5_RUNTIME
target_sources(samples PRIVATE
    "@PROJECT@/platform/ps5/audio_output.cpp"
    "@PROJECT@/platform/ps5/runtime_media_pipeline.cpp"
    "@PROJECT@/platform/ps5/vulkan_media_backend.cpp"
    "@PROJECT@/platform/ps5/native_media_runtime.cpp"
    "@PC@/src/assets/animation.cpp"
    "@PC@/src/assets/audio.cpp"
    "@PC@/src/assets/cakp.cpp"
    "@PC@/src/assets/cell.cpp"
    "@PC@/src/assets/font.cpp"
    "@PC@/src/assets/graphics2d.cpp"
    "@PC@/src/assets/mdl0.cpp"
    "@PC@/src/assets/mesh.cpp"
    "@PC@/src/assets/message.cpp"
    "@PC@/src/assets/mobiclip.cpp"
    "@PC@/src/assets/mods.cpp"
    "@PC@/src/assets/mods_video.cpp"
    "@PC@/src/assets/sdat.cpp"
    "@PC@/src/assets/tex0.cpp"
    "@PC@/external/khdays-decomp/libs/mobiclip/video/portable/mobiclip_reference.cpp")
target_include_directories(samples PRIVATE
    "@PROJECT@/platform/ps5"
    "@PC@/include"
    "@PC@/external/khdays-decomp/libs/mobiclip/video/portable")
target_compile_definitions(samples PRIVATE KHDAYS_PS5_NATIVE_BACKEND=1)
# END KHDAYS_PS5_RUNTIME
'@
$integration = $integration.Replace('@PROJECT@', $projectUnix).Replace('@PC@', $pcUnix)
$cmakeText = Get-Content -LiteralPath $cmake -Raw -Encoding UTF8
$pattern = '(?s)# BEGIN KHDAYS_PS5_RUNTIME.*?# END KHDAYS_PS5_RUNTIME\r?\n?'
$cmakeText = [regex]::Replace($cmakeText, $pattern, '')
$anchor = '# The UI module, when the title has it:'
if (-not $cmakeText.Contains($anchor)) {
    throw "Âncora de integração não encontrada em ps5/CMakeLists.txt"
}
$cmakeText = $cmakeText.Replace($anchor, "$integration`n$anchor")
Set-Content -LiteralPath $cmake -Value $cmakeText -Encoding UTF8 -NoNewline

if ($StageData) {
    if (-not (Test-Path -LiteralPath $data -PathType Container)) {
        throw "Dados staged não encontrados: $data"
    }
    $destination = Join-Path $title "dist\PPSA12345\data"
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Copy-Item -Path (Join-Path $data "*") -Destination $destination `
        -Recurse -Force
}

Write-Host "Título Vulkan preparado em: $title"
Write-Host "Programa: $program"
Write-Host "Dados: use -StageData para copiar build-full-game-data/data"
