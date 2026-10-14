# Front-end do 358/2 Days no PS5

Este é o primeiro marco do port. Ele cobre somente a sequência que o usuário
vê antes de uma missão:

```text
BootLogoScene
  → OpeningScene / DayTransitionScene
  → TitleScene
  → MainMenuScene
  → SaveFileScene
  → Character Select (MainMenuScene)
```

O gameplay (`GameplayScene`) fica fora desta etapa.

## Mapa de cenas e recursos

| Cena | Recursos lidos | Entrada/saída |
|---|---|---|
| Logos | `ttl/ttl.p2`, pares Disney/SQUARE ENIX/MobiClip/Nintendo | temporização fixa; vai para título |
| Abertura | artwork de abertura, script de subtítulos e `MODS`/MobiClip | Start pode pular; vai para transição do dia |
| Transição | calendário e arte de dia | confirma o hand-off para o fluxo seguinte |
| Título | `ttl/ttl.p2`, modelo/animação do logo, sprites localizados, BGM | História ou Modo Missão; voltar com B |
| Menu principal | `UI/mlt/res.p2`, `UI/mlt/mlt_&.s.z`, fonte NFTR, BGM | navegação por grade; confirma seleção |
| Arquivo | layout/sprites de `UI/cm`, logo e texto localizado | selecionar slot ou voltar |

Os caminhos acima são resolvidos pelo VFS e continuam pertencendo aos dados do
usuário. Nenhum arquivo da ROM deve ser copiado para `KH-Days-PS5`.

## Adaptação visual

- A resolução virtual permanece 256×192 por tela, com duas telas DS.
- O host PS5 apresentará o resultado em uma superfície Vulkan; o layout vertical
  e horizontal existente continua sendo uma decisão da camada de jogo.
- O renderer PS5 deve aceitar RGBA8888, alpha, imagens dinâmicas (logo animado)
  e overlay de fade. Isso cobre as operações usadas por todas as cenas acima.
- O primeiro teste não usará modelos ou gameplay: deve desenhar uma tela preta,
  uma cor de teste e um frame de logo sem dados do jogo.

## Adaptação de controles

O adaptador converte DualSense para os botões neutros existentes:

```text
Cross   → A          Circle → B
Square  → X          Triangle → Y
D-pad   → Up/Down/Left/Right
Options → Start      Touch/Share → Select
L1/R1   → L/R
```

O mapeamento é uma convenção inicial; os nomes e a configuração final serão
validados no primeiro executável PS5.

## Critérios de aceitação do front-end

1. inicia, apresenta os três pares de logos e não trava;
2. chega ao título com a animação e a música corretas quando os dados existem;
3. permite abrir História, Modo Missão e voltar com B;
4. permite navegar a seleção de personagem e iniciar a transição;
5. mantém o comportamento headless/PC existente;
6. encerra sem deixar recursos Vulkan/áudio pendentes.

O contrato agnóstico de plataforma está em
[`platform/ps5/frontend_adapter.h`](../platform/ps5/frontend_adapter.h). A
implementação concreta depende do SDK público PS5 e será adicionada depois do
teste “Hello Vulkan”.

O inventário mínimo de arquivos está em
[`frontend-resources.json`](frontend-resources.json). Depois de extrair os dados
do próprio usuário, valide-os com:

```powershell
.\tools\check_frontend_data.ps1 -Root .\data\extracted -Language en
```

