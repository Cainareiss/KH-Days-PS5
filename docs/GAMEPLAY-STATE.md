# Estado da fatia de gameplay

`core/include/khdays_ps5/gameplay_core.h` e `core/src/gameplay_core.cpp` agora
fornecem uma fatia independente do renderer para validar o fluxo que o port
precisa preservar:

- `PlayerStats`: nível, experiência, HP e munny;
- `CombatState`: encontro ativo, HP do inimigo, combo e guarda;
- `MissionState`: ID, objetivo, progresso e conclusão;
- `HudState`: snapshot único para o renderer desenhar HUD e barras;
- `SaveData`: formato binário pequeno, versionado e validado por magic/versão.

`platform/ps5/save_store.*` grava esses bytes em slots locais. Isso é um
adaptador de desenvolvimento; ainda não usa a API oficial de Savedata do PS5.

O alvo `khdays-ps5-runtime` executa uma missão curta, um ataque, a conclusão do
objetivo e um round-trip de save. Ele comprova a ligação do núcleo ao ELF, mas
não significa que o gameplay completo de 358/2 Days já foi reimplementado:
faltam mapas, scripts, IA, animações, colisões, decodificação de assets e a
ponte do renderer Vulkan.
