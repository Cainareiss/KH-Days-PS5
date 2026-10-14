# Status do projeto

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
