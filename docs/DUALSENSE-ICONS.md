# Ícones do DualSense e coleta de assets HD

## O que foi corrigido no runtime PS5

O host nativo agora se identifica como `SDL_GAMEPAD_TYPE_PS5`. O compositor
de tela única recebe um perfil de controle PS5 e os nomes posicionais do
controle (`south`, `east`, `west`, `north`, `start`, `back`, `touchpad`, L3/R3
etc.). O botão `Options` é o atalho do overlay de configurações.

Essa alteração não muda a lógica dos botões do DS nem injeta arquivos
comerciais. Ela prepara a seleção da família visual PlayStation quando o atlas
de ícones estiver disponível.

## O que ainda depende dos assets do PC

Os ícones que aparecem dentro da HUD e dos menus do jogo continuam sendo os
recursos do título até que os assets HD sejam extraídos localmente da instalação
do KH 1.5+2.5. Os arquivos não são distribuídos pelo repositório. Depois que a
instalação estiver disponível, o trabalho será:

1. localizar os atlas/NSCR/NCGR ou equivalentes que contêm os prompts;
2. mapear `south/east/west/north` para Cross/Circle/Square/Triangle;
3. adaptar resolução, paleta, transparência e escala para o compositor PS5;
4. validar no jogo e manter os originais como fallback.

## Qualidade atual da imagem

O pacote `v0.19.0-wip` usa o compositor Vulkan para ampliar a imagem do DS,
mas a resolução interna do jogo continua sendo a do DS. Portanto “4x” aqui é
escala/antialiasing do compositor, não renderização interna 4K nativa; isso
explica a perda de nitidez observada no PS5. Um atlas HD e uma resolução interna
maior são etapas separadas.

## Espaço para a instalação do PC

Em 22/10/2026, o volume `E:` tinha aproximadamente **4,93 GB livres** e `C:`
aproximadamente **0,54 GB**. Isso não é suficiente para instalar a coleção PC,
que ocupa dezenas de gigabytes. Nenhum arquivo do projeto, build, tradução ou
exFAT foi removido para produzir esse relatório.

Baixe a coleção para outro volume/external drive ou libere espaço manualmente
antes de iniciar o download. Quando a pasta estiver pronta, ela será usada
somente como entrada local para extração; os assets comerciais não serão
publicados no GitHub.
