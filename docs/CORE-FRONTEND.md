# Núcleo e front-end portátil

O diretório `core/` agora contém a primeira camada executável independente de
SDL e das APIs do PS5:

- IDs de cena compatíveis com o runtime PC (`1`, `7`, `11`, `19`, `100` e os
  IDs de gameplay/transição);
- resolução virtual `256x192` e cadência original `59.8261 Hz`;
- seleção de idioma com os mesmos caminhos de recursos do manifesto;
- fluxo BootLogo → Title → SaveFile/MainMenu → Gameplay;
- atalho por START/Confirm e navegação Up/Down/Cancel;
- validação de recursos obrigatórios antes de entrar em cada cena;
- interfaces `ResourceProvider` e `Renderer` para o backend PC ou PS5.
- `ResourceProvider::read` e `Frame::assets`, que fazem o staging dos bytes
  brutos de cada recurso para o renderer sem embutir dados do jogo;
- `FrontendSession`, que conecta input, render, apresentação e clock ao
  `FrontendAdapter` sem vazar APIs do SDK para o núcleo.

## Integração PS5 real

`platform/ps5/vulkan_videoout_adapter.*` fornece a ponte para um swapchain
Vulkan/VideoOut real. O título PS5 injeta callbacks para inicializar a janela,
ler o DualSense, registrar o frame, apresentar e esperar o próximo vblank. Isso
mantém `VkDevice`/`VkSwapchainKHR` dentro do renderer PS5, sem contaminar o
núcleo portátil.

`platform/ps5/audio_output.*` segue o fluxo do template PS5: `sceAudioOutInit`,
`sceAudioOutOpen` em 48 kHz estéreo, callback em grains de 256 frames e
`sceAudioOutClose` no encerramento. A implementação só é ativada ao compilar o
executável final com `KHDAYS_PS5_NATIVE_BACKEND` e ligar o `platform.c` do
template; no host ela retorna falso deliberadamente.

## Saída 4K

O padrão do núcleo agora solicita saída `3840x2160` ao adaptador. O conteúdo
continua sendo renderizado no framebuffer virtual `256x192`, com escala inteira
`11x` e viewport `2816x2112` centralizado (barras laterais e superior/inferior
para preservar a proporção 4:3). O backend Vulkan poderá trocar para filtragem
linear, mas não deve esticar a imagem nem alterar a resolução virtual do jogo.

## Validação local

```powershell
cmake -S . -B build-core -G "Visual Studio 17 2022" -A x64
cmake --build build-core --config Release --target khdays-ps5-core-smoke -- /m
ctest --test-dir build-core -C Release --output-on-failure
```

O smoke test percorre as transições do título, salvamento, seleção de
personagem/menu e entrada no gameplay usando um provedor de dados falso. Ele
valida a lógica do núcleo, não a execução no console nem a presença de assets
proprietários.

O mesmo alvo foi configurado com `toolchain/prospero.cmake` e compilado como
`build-ps5-core/libkhdays-ps5-core.a`. O objeto interno foi reconhecido pelo
LLVM como ELF relocatable `x86_64`; essa etapa comprova a compilação cruzada do
núcleo, mas ainda não liga VideoOut/Vulkan nem produz um executável carregável.
