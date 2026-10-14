# Pipeline de mídia do runtime PS5

Este marco conecta os dados decodificados do jogo aos recursos usados pelo
backend nativo. Ele não substitui os decodificadores do port PC: eles continuam
isolados em `khdays-pc-asset-decoders`, enquanto o código PS5 recebe somente
RGBA, vértices, índices e PCM já normalizados.

## Fluxo implementado

```text
KAPH/NSBMD/TEX0 -> RuntimeMediaPipeline -> VulkanMediaBackend
                                      -> VkImage + sampler
                                      -> vertex/index buffers device-local

SDAT/MODS PCM -> reamostragem 48 kHz estéreo -> audio_start
                                              -> sceAudioOut

MODS/MobiClip -> RGBA + FPS do contêiner -> textura Vulkan atualizável
                                        -> descriptor + quad de vídeo
```

`NativeMediaRuntime` é o proprietário dessa ligação. Ele deve ser criado depois
de `VulkanExampleBase` preparar `VulkanDevice` e a fila de transferência, e
destruído antes desses objetos. O caminho dos dados no título é `/app0/data`.

O renderer do título usa:

- `record_meshes()` dentro de um command buffer com pipeline de malha ativo;
- `video_descriptor()` para atualizar/vincular o descriptor set do vídeo;
- `record_video_quad()` para desenhar o frame MODS em tela cheia;
- `tick_video(elapsed_ns)` uma vez por frame. O método respeita o FPS 8.24 do
  contêiner e recupera frames quando o render loop atrasa.

O áudio é entregue como PCM assinado de 16 bits, estéreo, 48 kHz e em blocos de
256 frames. Com `KHDAYS_PS5_NATIVE_BACKEND=ON`, `TemplatePs5AudioOutput` chama
`audio_start()` do PS5 Vulkan Template; a implementação do template abre,
alimenta e fecha a porta por `sceAudioOutInit`, `sceAudioOutOpen`,
`sceAudioOutOutput` e `sceAudioOutClose`.

## Build Prospero validado

```powershell
$sdk = "E:\DOCUMENTOS\KH-Days-PS5\toolchains\ps5-payload-sdk-v0.44\ps5-payload-sdk"
$env:PS5_PAYLOAD_SDK = $sdk
cmake -S . -B build-ps5-media -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="$sdk\toolchain\prospero.cmake" `
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY `
  -DKHDAYS_PS5_BUILD_TESTS=OFF `
  -DKHDAYS_PS5_NATIVE_BACKEND=ON `
  -DKHDAYS_ENABLE_PC_ASSET_DECODERS=ON `
  -DKHDAYS_PC_PORT_ROOT="E:/DOCUMENTOS/KH-ReCoded-PTBR/tools/khdays-port" `
  -DKHDAYS_PS5_BUILD_VULKAN_MEDIA_BACKEND=ON `
  -DKHDAYS_PS5_VULKAN_TEMPLATE_ROOT="E:/DOCUMENTOS/KH-Days-PS5/toolchains/sources/PS5_VulkanTemplate"
cmake --build build-ps5-media --target khdays-ps5-native-media-runtime -j 4
```

Foram produzidas bibliotecas Prospero para os decodificadores, a ponte de
assets, o backend de recursos Vulkan e o runtime nativo combinado.

## Evidência no host

O smoke test usa os assets reais staged em `build-full-game-data/data` e valida:

- 12 texturas TEX0 enviadas ao callback de GPU;
- 13 malhas com vértices e índices válidos;
- dois frames de `mv/802.mods`, incluindo o intervalo correto entre eles;
- 4.260.285 frames de áudio enfileirados;
- 87.655 amostras não silenciosas no primeiro segundo puxado pelo sink.

Comando:

```powershell
./build-asset-bridge-vs/Release/khdays-ps5-media-smoke.exe `
  ./build-full-game-data/data
```

## Limite desta validação

O upload e os comandos de desenho estão implementados e compilam para
Prospero, mas não foram executados em um PS5 nesta máquina. O `VulkanExample`
final ainda precisa criar os pipelines/descriptor sets compatíveis, chamar os
métodos de gravação no command buffer do swapchain e ser validado no console.
Esse teste de console é necessário antes de afirmar que a renderização ou o
áudio funcionam no hardware.
