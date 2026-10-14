# Backend PS5 (esqueleto)

Este diretório reserva o adaptador de plataforma. Ele ainda não contém código
compilável porque a máquina atual não possui o PS5 Payload SDK nem o compilador
Prospero. Adicionar `#ifdef PS5` ao backend PC antes de validar a toolchain
criaria um caminho impossível de testar.

Quando a toolchain estiver disponível, a implementação deve ser dividida em:

- `ps5_video_vulkan.*`: device, swapchain, barriers e apresentação;
- `ps5_input.*`: leitura de controle e mapeamento para eventos neutros;
- `ps5_audio.*`: fila de PCM e sincronização de áudio;
- `ps5_filesystem.*`: diretório de dados do título e saves;
- `ps5_main.cpp`: bootstrap mínimo do título.

O primeiro teste deve ser um “Hello Vulkan” sem dados do jogo. Depois o
front-end neutralizado do `khdays-port` será ligado gradualmente.

