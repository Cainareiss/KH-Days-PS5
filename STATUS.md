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

## Ainda pendente

- instalar e fixar `ps5-payload-dev/sdk` e o compilador Prospero;
- implementar VideoOut/swapchain Vulkan e o primeiro “Hello Vulkan”;
- implementar input e áudio PS5;
- ligar as cenas reais ao adaptador e testar no console homebrew;
- corrigir limitações atuais do `khdays-port` (gameplay, saves e multiplayer).

Nenhum ELF, FSELF, PKG ou executável PS5 é produzido por este repositório no
estado atual. Qualquer arquivo futuro desse tipo será gerado localmente para
testes do proprietário do console, não versionado aqui.

