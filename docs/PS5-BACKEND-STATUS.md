# Estado do backend PS5

Este marco adiciona as três interfaces necessárias para avançar do núcleo
portátil para um título executável:

1. `FileResourceProvider` lê arquivos extraídos de `/app0`, valida o caminho e
   disponibiliza os bytes em `Frame::assets`.
2. `TemplatePs5AudioOutput` usa o contrato do `PS5_VulkanTemplate`: PCM S16
   estéreo, 48 kHz, blocos de 256 frames, através de `audio_start/audio_stop`
   (que por sua vez chamam `sceAudioOutInit/Open/Output/Close`).
3. `VulkanVideoOutAdapter` recebe callbacks do executável PS5 para o
   swapchain Vulkan/VideoOut, input, gravação de comandos, present e espera do
   frame.

O núcleo foi compilado no host e no alvo Prospero como
`build-ps5-core/libkhdays-ps5-core.a`. O host não simula um dispositivo PS5:
áudio e callbacks nativos ficam desativados até o bootstrap do título fornecê-
los.

Ainda falta implementar os decodificadores dos formatos do jogo (`.p2`, `.z`,
NFTR e texturas), converter esses dados em imagens/fontes Vulkan e criar um
ELF/FSELF/PKG de runtime. Portanto esta etapa é uma base de integração real,
mas ainda não é uma build jogável instalável no PS5.
