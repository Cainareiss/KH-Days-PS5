# Status do projeto

**WIP — adaptação em andamento; ainda não é uma build jogável para PS5.**

## Concluído neste marco

- repositório local separado para o alvo PS5;
- mapa do front-end e das transições de cena;
- contrato agnóstico `FrontendAdapter` para vídeo, input, apresentação e clock;
- mapeamento inicial do DualSense para os botões neutros do jogo;
- lista de recursos necessários para logos, título, menus, save e seleção;
- regras para não versionar ROM, assets extraídos, SDK ou binários;
- diagnóstico do host e da ausência da toolchain PS5.
- manifesto dos recursos do front-end e verificador por idioma.
- extração local validada para a revisão USA `YKGE` em inglês e francês;
- descompressão de 958 arquivos e desempacotamento de 486 entradas de UI sem falhas.
- smoke test documentado em `docs/FRONTEND-SMOKE-REPORT.md`.
- baseline PC compilada com Visual Studio/CMake; logos, título, seleção de personagem e salvamento renderizados com os assets reais da revisão USA.
- detalhes e limites registrados em `docs/PC-FRONTEND-BASELINE.md`.
- auditoria da fronteira SDL/port neutro registrada em `docs/PC-PS5-API-AUDIT.md`.
- fontes públicas da toolchain baixadas localmente; estado registrado em `docs/TOOLCHAIN-STATUS.md`.
- núcleo portátil e fluxo de front-end PC/PS5 implementados em `core/`, com smoke test CMake passando.
- loop `FrontendSession` integrado ao adaptador PS5 e compilado também como biblioteca estática com `prospero.cmake`.
- saída 4K configurada no contrato (`3840x2160`) com viewport 4:3 e escala inteira do framebuffer original.

## Backend de plataforma implementado neste marco

- `FileResourceProvider` lê assets reais da raiz `/app0` com proteção contra
  traversal de caminhos;
- `TemplatePs5AudioOutput` segue o contrato PCM do template (`sceAudioOut`)
  quando `KHDAYS_PS5_NATIVE_BACKEND` for ativado;
- `VulkanVideoOutAdapter` fornece a ponte de inicialização, desenho, present e
  sincronização para o swapchain Vulkan/VideoOut real;
- o núcleo entrega bytes brutos em `Frame::assets` e a sessão inicializa o
  adaptador em `3840x2160`.

## Runtime e gameplay neste marco

- fatia de gameplay com combate, HUD, missão e save versionado em `core/`;
- alvo Prospero `khdays-days-runtime.elf` gerado como ELF executável;
- FSELF fake/homebrew e bundle ZIP reproduzíveis por `scripts/package_ps5_homebrew.ps1`.

## Ainda pendente

- instalar e fixar `ps5-payload-dev/sdk` e o compilador Prospero;
- implementar VideoOut/swapchain Vulkan e o primeiro “Hello Vulkan”;
- implementar input e áudio PS5;
- ligar as cenas reais ao adaptador e testar no console homebrew;
- corrigir limitações atuais do `khdays-port` (gameplay, saves e multiplayer).

Nenhum ELF, FSELF, PKG ou executável PS5 é produzido por este repositório no
estado atual. Qualquer arquivo futuro desse tipo será gerado localmente para
testes do proprietário do console, não versionado aqui.

## Marco de dados completo

- A árvore NitroFS inteira foi staged localmente em `build-full-game-data/data/`.
- O manifesto contém 1.713 arquivos com tamanho e SHA-256.
- Essa etapa não gera exFAT nem altera a saída de empacotamento.
- O runtime ainda é uma fatia vertical: decodificadores P2/CAKP/Z, NFTR,
  NSBMD/NSBTX, SDAT e MODS e a integração de todos os mundos continuam pendentes.
