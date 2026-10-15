# Status do projeto

## Runtime PPSA12345 (14/10/2026)

- O smoke test foi substituído pelo título `PPSA12345`, com Vulkan/RADV,
  decodificadores do 358/2 Days, reprodução MODS, áudio e dados staged em
  `build-vulkan-title-4/dist/PPSA12345`.
- O executável agora também compila e instancia `gameplay_core` e
  `world_runtime` no mesmo loop Vulkan, incluindo a fatia inicial de mundo,
  cutscene, HUD de missão e comandos de ataque/pular cena.
- `eboot.bin` foi convertido e assinado; a inspeção reportou `integrity: valid`.
- Dados staged conferidos: 1.720 arquivos, 259.710.067 bytes.
- Limite atual: isto ainda é uma fatia jogável integrada, não a implementação
  completa de todas as missões, telas, transições e assets do jogo original.

## Toolchain RADV/CLC (14/10/2026)

- LLVMSPIRVLib, mesa_clc.exe, vtn_bindgen2.exe e glslangValidator 15.4.0
  foram compilados e validados no host Windows.
- RADV release foi compilado para PS5 em
  `.deps/native/radv-release/lib/libvulkan_radeon.ps5.a`, com
  `PROVENANCE.txt` registrando Mesa `47e9eb61ee7e` e SDK `b83202be...`.
- O smoke title foi linkado em ELF e convertido para FSELF; o resultado atual
  esta em `toolchains/sources/PS5_Vulkan/dist/PPSA99014/eboot.bin`.
- A camada de emulated TLS, o caminho RADV PS5 e o layout de parametros foram
  corrigidos sem stubs falsos; o `libc.prx` tambem foi reconstruido e validado.
- Ainda nao ha teste de boot no console e ainda nao existe PKG de distribuicao.

**WIP — adaptação em andamento; ainda não é uma build jogável completa para
PS5.**

## Concluído

- núcleo portátil de front-end, gameplay, combate, HUD, missão e save;
- configuração lógica 4K (`3840x2160`) com viewport 4:3;
- toolchain pública PS5 Payload SDK v0.44 e LLVM/Prospero instalada localmente;
- compilação cruzada do núcleo e das bibliotecas de mídia para Prospero;
- árvore local do jogo staged com 1.718 arquivos e hashes, sem versionar ROM ou
  dados proprietários;
- catálogo de 114 missões e 10 mundos;
- decodificação P2/LZ, CAKP/KAPH, NFTR, NSBMD/TEX0, SDAT e MODS/MobiClip;
- conversão das texturas para RGBA e das malhas para buffers neutros;
- backend real de recursos Vulkan com `VkImage`, sampler, buffers device-local,
  atualização da textura MODS e quad de vídeo;
- conversão/reamostragem para PCM16 estéreo a 48 kHz e ligação ao
  `audio_start()` nativo, cuja implementação do template usa `sceAudioOut`;
- agendamento de frames MODS pelo FPS do contêiner;
- programa final `VulkanExample` criado com renderização dinâmica Vulkan 1.3,
  pipelines de malha/vídeo, layouts, descriptors e gravação no command buffer;
- ligação do loop de frame ao upload de modelos, apresentação MODS e áudio
  `sceAudioOut`, usando `/app0/data` como raiz no PS5;
- gerador reproduzível do título `PPSA12345` sobre o
  `PS5_VulkanTemplate`, sem versionar SDK ou dados proprietários;
- smoke test com assets reais aprovado: 12 texturas, 13 malhas, dois frames de
  vídeo e áudio não silencioso;
- build Prospero de `khdays-ps5-native-media-runtime` aprovado;
- compilação integral do alvo `samples` gerado aprovada: 47/47 unidades,
  incluindo título, Vulkan Template, decodificadores e runtime de mídia.

Os detalhes e comandos reproduzíveis estão em
[`docs/RUNTIME-MEDIA-PIPELINE.md`](docs/RUNTIME-MEDIA-PIPELINE.md).

## Atualizacao da toolchain (14/10/2026)

- A revisao do Payload SDK fixada pelo template (`b83202be73e930050e00de0f1b4d9ec46c0391bf`)
  foi instalada em `D:\\KH-Days-PS5-toolchain\\ps5-payload-sdk-b832` (NTFS).
- O zlib 1.3.2 foi compilado e registrado no cache nativo.
- O link RADV ainda depende de `LLVMSPIRVLib`/Mesa CLC, ausentes no LLVM local;
  nenhuma biblioteca RADV incompleta foi incluida no titulo.

## Ainda pendente

- ligar o catálogo, scripts e transições ao loop completo de todas as missões;
- integrar fontes, HUD e todas as telas 2D ao renderer final;
- instalar a revisão do Payload SDK fixada pelo template em NTFS/WSL e concluir
  o link RADV do ELF/FSELF; o volume exFAT atual não preserva os symlinks Unix;
- testar Vulkan/VideoOut, DualSense e `sceAudioOut` no PS5 homebrew real;
- somente após o teste no console, reconstruir uma distribuição exFAT válida.

A compilação das 47 unidades para Prospero comprova compatibilidade do código
fonte, não funcionamento no console. O link final e o teste em hardware ainda
são obrigatórios; nenhum resultado de hardware é declarado sem teste real.
