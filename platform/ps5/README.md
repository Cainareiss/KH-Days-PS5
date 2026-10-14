# Backend PS5

O backend tem três pontos concretos de integração:

- `file_resource_provider.*` lê a árvore extraída do título (`/app0`) e bloqueia
  caminhos absolutos ou `..` que escapem da raiz;
- `audio_output.*` usa o contrato público `audio_start/audio_stop` do
  `PS5_VulkanTemplate` (PCM estéreo S16, 48 kHz, blocos de 256 frames);
- `vulkan_videoout_adapter.*` liga o núcleo aos callbacks do swapchain Vulkan e
  da apresentação VideoOut já criados pelo executável PS5.

O adaptador não copia headers proprietários nem cria símbolos Sony falsos. O
executável final deve fornecer os callbacks depois de inicializar o
`VulkanExampleBase`/swapchain do template; `present` só retorna sucesso quando
essa ponte realmente apresentou o frame. O build de Windows usa a mesma
interface, mas não finge ter dispositivo Vulkan/áudio PS5.

`Frame::assets` entrega os bytes brutos dos recursos requeridos ao renderer.
P2, NFTR, `.z` e outros formatos ainda precisam de decodificadores próprios
antes de virarem texturas/fontes; nenhum arquivo original é sobrescrito.
