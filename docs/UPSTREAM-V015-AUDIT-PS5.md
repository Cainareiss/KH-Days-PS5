# Auditoria upstream v0.1.5 para PS5

Auditoria feita em 27/11/2026. O upstream atual verificado foi
[`Yokimitsuro/khdays-port` v0.1.5](https://github.com/Yokimitsuro/khdays-port/releases/tag/v0.1.5), commit
`2037f79a171524eafcf40a1d641b16da14667a64`; a base deste port ainda é v0.1.1,
commit `a7ee5a2d6c423ad69df7405a30f63e858f8e2acc`. Entre essas revisões o
GitHub reporta 361 commits e 300 arquivos alterados. A v0.1.5 também atualiza o
submódulo `khdays-decomp` para `93535afd7ca082f3d856bbf1cadae65be16d44ba`.

## O que merece adaptação

- **Entrada e ícones:** a UI agora escolhe ícones pela família do controle SDL.
  A camada PS5 informa família PlayStation/DualSense e ID do controle, sem
  inicializar o SDL de desktop.
- **Runtime compartilhado:** a revisão nova usa a API de atomics, estado de
  inicialização, semáforos, relógio de alta resolução e identidade de thread do
  SDL3. A ponte PS5 converte esses contratos para C11 atomics/POSIX; a troca
  atômica conserva o retorno do valor anterior, usado pelo mailbox de quadros.
- **Vídeos preparados para PS5:** os caminhos novos de cenas multipartes são
  convertidos para MPEG em `/app0`, e os agendamentos de legenda não são
  desenhados uma segunda vez sobre vídeos cuja legenda já foi composta.
- **Renderer Vulkan:** continua usando os shaders SPIR-V e o backend Vulkan do
  PS5. A preparação remove apenas os wrappers de janela SDL acrescentados na
  versão de PC; não copia o renderer de janela/SDL para o console.
- **ROM norte-americana:** o upstream passa a registrar também a ROM YKGE. Isso
  é útil para ampliar compatibilidade, mas o fluxo PS5 continua validando a
  edição europeia YKGP até que os dados derivados da ROM americana sejam
  regenerados e testados separadamente.
- **Correções de jogo e assets:** correções de missões, salas, pausa, áudio,
  modelos e carregamento de recursos devem ser avaliadas conforme os arquivos
  que o runtime PS5 realmente compila. Recursos exclusivos de KH2 não são
  melhorias de KH 358/2 Days.

## O que não é um port direto

O instalador Windows, a janela SDL, os backends gráficos de desktop e plugins
nativos carregados dinamicamente não são apropriados para serem copiados como
estão. A ponte PS5 deixa plugins dinâmicos desativados: o carregador do console,
permissões, ABI e segurança desses módulos ainda não foram validados. O áudio,
VideoOut e controles permanecem sob os serviços nativos do PS5.

## Preparação implementada nesta auditoria

Os scripts de preparação aceitam os fontes v0.1.5 e adaptam a camada comum
necessária para continuar o port: atomics (inclusive valor anterior),
`SDL_InitState`, relógio, tipos de controle, semáforos do desligamento e
caminhos de filmes. Os dados upstream são lidos, não modificados. A preparação
do renderer produziu os 12 blocos de shader Vulkan originais.

Validações locais: 19 testes específicos da ponte passaram, com 6 skips; o
adaptador POSIX e um fonte real de estado de mods passaram em compilação de
sintaxe no host; `git diff --check` passou. O conjunto geral executou 74 testes,
mas não ficou verde: 4 erros dependem de um módulo `references` ausente e de
`clang.exe`/toolchain Windows ausente neste sistema; 22 testes foram ignorados.

## Limites e sequência de integração

Esta auditoria **não atualiza ainda** `native/upstream-lock.json`, o submódulo
decompilado ou a versão de dados/ROM. Também não compila o ELF Prospero, não
executa o jogo e não gera exFAT. O disco que contém o checkout persistente, a
ROM/dados locais e o SDK Prospero não estão montados neste ambiente. As
adaptações aqui preparadas são uma camada de compatibilidade, não uma build
testável no PS5.

Antes de trocar o pin do projeto para 0.1.5 ainda é necessário atualizar os
scripts de aquisição/build e o submódulo decompilado, compilar o conjunto real
com o SDK PS5, resolver os erros de integração e fazer regressão em hardware.
Somente então devem ser ativadas, uma a uma, as melhorias compatíveis com
358/2 Days — mantendo fora as features exclusivas de KH2 e os serviços de PC.

O README e a lista de ROMs suportadas do upstream descrevem as opções gráficas,
UI e gamepads da revisão corrente: <https://github.com/Yokimitsuro/khdays-port>
e <https://github.com/Yokimitsuro/khdays-port/blob/main/tools/supported_roms.json>.
