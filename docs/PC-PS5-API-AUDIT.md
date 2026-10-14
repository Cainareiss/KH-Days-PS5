# Auditoria da fronteira PC/PS5

Esta auditoria foi feita antes de instalar a toolchain PS5 para evitar levar
dependências SDL diretamente para o código do jogo.

## Resultado

- `src/` e `include/` não possuem referências SDL;
- as chamadas SDL de janela, renderização, eventos, relógio e áudio estão
  concentradas em `platform/pc/sdl_runtime.cpp`;
- a lógica de cenas e os recursos continuam acessíveis por interfaces neutras
  (`Renderer`, `Input` e carregadores de dados);
- `platform/ps5/frontend_adapter.h` já define o ciclo mínimo de inicialização,
  input, apresentação, clock e encerramento sem incluir headers proprietários.

## Substituições previstas

| Fronteira PC | Serviço que o backend PS5 deverá fornecer |
|---|---|
| `SDL_CreateWindowAndRenderer` | VideoOut + superfície/swapchain Vulkan |
| `SDL_Render*` e texturas | renderer Vulkan que aceite o frame RGBA do port |
| `SDL_PollEvent` | leitura do controle e mapeamento para `game::Input` |
| `SDL_GetTicksNS`/`SDL_Delay` | relógio monotônico e espera por vblank |
| `SDL_AudioStream` | fila de áudio do ambiente PS5 ou adaptador validado |
| filesystem C++ | diretório de dados externo ao executável |

Nenhuma dessas substituições foi simulada como se fosse PS5. O runtime PC
continua sendo a referência executável até que um backend real possa ser
compilado e testado.
