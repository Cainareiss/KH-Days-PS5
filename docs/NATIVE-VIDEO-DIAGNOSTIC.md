# Imagem: correção e captura 01.000.011

## Resultado da 010 e mudanças na 011

O usuário confirmou jogo sem crash, porém com tons azuis, gameplay lento e
layout pequeno. O log 010 mostra 3D com pixels reais e CLEAR_DEPTH=7fff.
Seu PERF até a apresentação 1200 cobre principalmente abertura e menus;
não comprova nem refuta os aproximadamente 5 FPS relatados na fase.

### Canais de cor

O host entrega palavras `0x00RRGGBB`. A implementação pública do
[SDL PS5](https://github.com/ps5-payload-dev/SDL/blob/release-2.30.x-ps5/src/video/ps5/SDL_ps5video.c)
combina o atributo `0x8000000022000000` com `SDL_PIXELFORMAT_ABGR8888`.
Seu [tiler](https://github.com/ps5-payload-dev/SDL/blob/release-2.30.x-ps5/src/video/ps5/SDL_ps5tilemap.c)
não troca canais na cópia. Escrever RRGGBB diretamente nesse modo inverte R/B.
Na 011, vermelho `0x00ff0000` vira `0xff0000ff` (bytes R,G,B,A na memória).
A conversão ocorre nos 98.304 pixels DS, não nos milhões do upscale 4K.
O alias de formato do SDK não é usado para inferir a ordem dos bytes.

### Compositor e desempenho

O objeto PS5 anterior fazia consultas emutls dentro do loop de pixels.
`prepare_native_runtime_sources.py` produz uma instanciação C do mesmo
`compose.h`, com contexto por chamada, sem alterar o upstream nem a versão
HLSL. Mantém isolamento entre threads e reentrância. O teste executa a
versão original emulada e a adaptada, compara pixels, fades, retângulos,
sprites, 3D e descrição larga, e faz chamadas simultâneas com contextos
diferentes. Consultas TLS por quadro no cenário: 835.591 → 0. O objeto PS5
adaptado não tem referência a `__emutls_get_address`.

O ganho local (~26–33 ms → ~20–23 ms nesse cenário) não é um resultado de
FPS do PS5. O port ainda rasteriza na CPU e apresenta pelo VideoOut. A cópia
WC passa a usar stores não temporais e SFENCE, sem CLFLUSH por quadro. Os
novos registros `RENDER` separam os custos de 2D/UI, apresentação/captura e
3D, limitados a seis amostras em gameplay com pelo menos 16 polígonos.

### Tamanho da imagem

Saída 4K mantida, com cinco layouts selecionáveis por L3+R3:

1. Tela superior preenchendo 16:9 + inferior no canto (padrão).
2. Tela inferior preenchendo 16:9 + superior no canto.
3. Superior em proporção 4:3 + inferior no canto.
4. Inferior em proporção 4:3 + superior no canto.
5. Duas telas empilhadas, como antes.

Os modos preenchidos esticam 4:3 para 16:9: não aumentam o campo de visão.
A interface não foi rearranjada como no compositor de tela única do PC.
A preferência é salva em `/data/khdays-native/saves/video-layout.txt`;
`KHDAYS_PS5_LAYOUT=upper|lower|upper-fit|lower-fit|stacked` a sobrepõe.
As duas imagens são preservadas e podem ser ampliadas pelo combo. Só a
thread de apresentação aplica a mudança; o controle publica uma solicitação
atômica. Os testes verificam a escala, cores e todos os pixels de cada modo,
em 1080p/4K, além de limpar os dois buffers ao mudar para um viewport menor.

## Achado do log 009 e correção 010

O crash em cena 2/frame 4838 é uma leitura de BGnCNT em
`SetBGnControlToText`: PC `0x4d021a`, correspondente a ELF `0xd021a` com
base de carga `0x400000`. A tabela adaptada estava em um objeto somente de
dados que o archive não extraiu. A auditoria do executável anterior encontrou
232 dos 4.955 registros de inicialização. Após usar `--whole-archive` apenas
para o jogo, todos os 4.955 chegam ao ELF, incluindo a tabela em `0x02041ac0`
com os oito endereços corretos da view I/O. O inventário é requisito para
assinar e empacotar. `native-records-009-failure.json` preserva o resultado
da análise anterior, com o hash do ELF analisado.

Isso corrige uma falha concreta de construção, não certifica gráficos e
gameplay no hardware. O log também mostra polígonos válidos com raster 3D
vazio e os mesmos hashes antes/depois da interface. A 010 inclui os valores
de clear/depth nos checkpoints existentes e calcula a base de código com
um símbolo do linker. Não aumenta a duração nem o volume da coleta.

## Histórico da coleta

A 01.000.009 mantém a correção da 008 e acrescenta cópia de tiles sequenciais,
comparação antes/depois da UI, tempos de apresentação e registro persistente
de falhas. A 008 exibiu vídeo no console, mas apresentou tela superior branca,
lentidão e encerramento ao iniciar a primeira fase; esses problemas ainda
precisam de confirmação/correção com o novo resultado de hardware.

O defeito reproduzido no PC era a omissão da composição dos bitmaps. O runtime
ativa texto, HUD e pausa nativos por padrão. Isso acrescenta `KHDAYS_WANT_2D`
ao pedido do frame para coletar descrições da interface, mas não significa que
o presenter PS5 consiga desenhá-las na GPU. A decisão antiga confundia essas
duas condições e pulava o rasterizador; `ui_frame` também deixava os bitmaps
sem composição nesse caminho. O VideoOut recebia os buffers zerados.

O adaptador de `display.c` agora consulta a capacidade do host separadamente:
se `khdays_host_frame_wants()` não pede `KHDAYS_WANT_2D`, o runtime compõe os
bitmaps, mesmo com a interface ativada. A composição CPU dos elementos de UI
continua disponível. O renderer que realmente recebe descrições 2D ainda pode
omitir o bitmap de referência.

## Evidência local

`tests/native_reference_pixels_test.c` executa as decisões original e adaptada
com os padrões reais de `ui/parts.c` e o rasterizador real `gpu/gpu2d.c`. A
decisão original deixa os dois buffers pretos. A adaptada produz todos os
49.152 pixels brancos na tela A e azuis na B, conforme as paletas do cenário
controlado; também respeita um fade intencional para preto. Isso comprova o
defeito de decisão e sua correção local, sem afirmar que o jogo inteiro foi
validado no PS5.

## Uma captura com fim definido

O mesmo `/data/khdays-native/saves/boot.log` contém o boot e esta cadeia:

`cena + registradores + paletas/OAM/VRAM → raw2d → composição da UI → 3D do próximo frame → submit → flip confirmado`

Cada estágio DS tem oito checkpoints: 0, 1, 2, 3, 30, 120, 600 e 1200. Se um
VBlank for mesclado, o primeiro frame observado após o ponto serve de amostra.
Cada estágio escreve no máximo oito amostras fixas, com `PIPE_DONE` ao concluir,
além de até oito mudanças de cena, que podem ocorrer depois da abertura.
O VideoOut usa os mesmos pontos de contagem de apresentações. Erros continuam
sendo registrados imediatamente. Não há escrita periódica infinita.

| Campo | Evidência |
| --- | --- |
| `BUILD` | Identifica 01.000.011 no próprio log. |
| `stage=raw2d` / `stage=composed` | Compara o bitmap antes/depois dos elementos de interface. |
| `stage=composed`, `REF`, `WANT`, `HOST`, `UI` | Confirma se os bitmaps foram compostos apesar do pedido de descrições da UI. Neste presenter, o esperado é `REF=1 HOST=00`. |
| `at`, `ds`, `scene` | Checkpoint solicitado, frame realmente observado e cena do jogo. |
| `DISPCNT`, `BGCNT`, `MASTER`, `BLD`, `WIN` | Modos, camadas, fade global, mistura de cores e janelas das duas telas. |
| `PAL`, `OAM` | Bytes não nulos das paletas/sprites; `PAL` inclui as duas cores de fundo. |
| `VRAM`, `ABG`, `BGB`, `BOB` | Controles dos nove bancos e páginas mapeadas:bytes não nulos em BG A/B e sprites B. |
| `G3D`, `PRE` | Polígonos, vértices e flushes; geometria antes de clipping. Uma cena 2D pode legitimamente ter geometria zero. |
| `PIX3D` | Pixels 3D não nulos e pixels com RGB não preto, respectivamente. |
| `CLEAR` | CLEAR_COLOR / CLEAR_DEPTH usados no rasterizador de referência. |
| `SCREENS`, `PIX`, `HASH` | Disponibilidade/contagens dos bitmaps e hashes da camada 3D/telas. `n/a` no estágio 3D significa que ele não recebeu os bitmaps finais. |
| `frame submit`, `flip marker ... reported shown` | Conteúdo entregue ao presenter e confirmação do serviço VideoOut. |
| `PERF` | Tempo de trabalho da cópia/sincronização, espera de flip e médias, além do tempo decorrido. |
| `RENDER` | Até seis amostras em cena 2 com pelo menos 16 polígonos, separando 2D A, B/UI, apresentação/captura e CPU 3D. |
| `RUNTIME scene` | Mudança de cena e base do executável, até 16 eventos. |
| `RUNTIME_STOP` / `fatal runtime fault` | Saída detectada por rotina/ABI ou sinal fatal, com o frame e a cena. Sinais incluem endereço e offset do ELF quando a base está disponível. |

O teste `native_ps5_video_test.c` verifica também limites de tiling, todos os
pixels em 1080p/4K, falhas de dispositivo e o orçamento de captura: milhares
de chamadas têm no máximo oito pontos fixos e oito eventos por estágio.
O teste `native_diag_fault_test.c` verifica a escrita no descritor persistente
e os campos do registro de falha sem simular a execução da primeira fase.

Para obter a captura, iniciar a 01.000.011, repetir a entrada da primeira fase
e copiar o `boot.log` do diretório de saves após sair ou ocorrer a falha.
O arquivo pode ser colocado em
`<pasta-local-do-usuario>\Pictures\boot.log`, como nos testes anteriores.
