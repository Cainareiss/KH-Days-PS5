# Status do projeto

## Modo de desempenho da apresentação — 01.000.012 (21/10/2026)

- O novo `boot.log` confirmou o gargalo no presenter CPU: em 3840×2160 a
  apresentação gastava aproximadamente 22 ms de trabalho por quadro, e os
  1200 quadros iniciais levaram 56,8 s. O custo dominante era preencher o
  framebuffer tiled de 4K, não um crash do runtime.
- O padrão da build agora é 1920×1080. Isso reduz a área de cópia de 8,3 M
  para 2,1 M de pixels por quadro. `KHDAYS_PS5_VIDEO=4k` mantém o caminho
  3840×2160 para diagnóstico; o parâmetro não altera a lógica DS nem cria
  renderização interna 4×.
- A origem continua sendo 256×192 por tela. Portanto “4K/4×” é escala de
  saída, não uma versão HD do jogo; eliminar o aspecto pixelado exigirá um
  compositor GPU/Vulkan ou assets/texturas de resolução maior, ainda não
  ativados neste caminho de referência.
- A versão `01.000.012` foi recompilada em área temporária, relinkada com
  4.955 inicializadores do jogo, auditada com zero instruções de syscall direta
  e empacotada em uma imagem exFAT local.
  A imagem tem 753.926.144 bytes, SHA-256
  `B542144471E87BB5777E94BC93090A21A209DDA87744C250D7B8BD4B0A09F03C`.
- A imagem foi montada somente para leitura e conferida: título `PPSA35820`,
  versão `01.000.012`, `eboot.bin` SHA-256
  `C0BC79941A306B5B1E77D319C25FB8CF96F1E7C9D3AD97BBAB4096E5D93AB8AC`, capa e
  fundo preservados. O teste de FPS/cores no console ainda é necessário.

## Cores, custo do compositor e apresentação ampliada — 01.000.011 (21/10/2026)

- O usuário confirmou entrada na fase sem crash na 010. O novo log registra
  pixels 3D reais (`8384/7615` no checkpoint 600), validando o progresso após
  a inclusão dos inicializadores. Relato novo: tons azuis, aproximadamente
  5 FPS na fase e as duas telas pequenas no centro. O PERF antigo cobre a
  abertura; não mede a velocidade do gameplay relatado.
- O contrato do host é `0x00RRGGBB`, mas o atributo VideoOut utilizado tem
  palavras ABGR. O backend SDL PS5 usa `SDL_PIXELFORMAT_ABGR8888` com o mesmo
  valor `0x8000000022000000` e copia os bytes sem trocar canais. O presenter
  agora converte R/B no tamanho DS antes da ampliação. Nenhuma textura nem
  paleta do jogo é recolorida.
- Confirmadas chamadas `__emutls_get_address` nos helpers do compositor.
  A adaptação passa um contexto explícito por chamada, preservando a mesma
  aritmética compartilhada C/HLSL, sem variáveis globais de desenho. O teste
  de equivalência inclui camadas, sprites, 3D, fades, retângulos, descrição
  larga e chamadas concorrentes. No cenário medido, 835.591 consultas TLS por
  quadro viram zero; benchmark local de referência ~26–33 ms, adaptado
  ~20–23 ms. O teste usa um provedor emutls de controle no PC: não prevê FPS
  do console. O objeto PS5 compilado só importa memcpy/memset, sem emutls.
- VideoOut mantém saída 3840x2160. Padrão: principal preenchendo 16:9 e
  secundária no canto. É ampliação esticada, não widescreen com campo de
  visão novo. L3+R3 percorre principal superior/inferior preenchida, superior/
  inferior em proporção 4:3 e duas empilhadas. Escolha salva em
  `/data/khdays-native/saves/video-layout.txt`. O combo não vaza Select/lock-on
  ao jogo, e teclas separadas mantêm suas funções.
- A cópia usa stores não temporais alinhados e SFENCE sobre WC_GARLIC;
  removido o passeio CLFLUSH por megabytes a cada quadro, seguindo o caminho
  de cópia WC do SDL. Testes conferem todos os pixels em cinco layouts a
  1080p/4K, canais puros, troca de layout e limpeza dos dois buffers.
- Perfil adicional limitado a seis registros `RENDER` em cena 2 com pelo
  menos 16 polígonos: 2D A, 2D B/UI, apresentação/captura e CPU 3D. A
  renderização continua CPU/VideoOut; isto não ativa o backend Vulkan.
- Sem espaço para a cópia temporária de build.ninja, CMake estava repetindo
  configuração sem substituir o manifesto. Foram movidos apenas intermediários
  gerados para um diretório temporário local;
  são recuperáveis, e fontes, ROM, saves e o exFAT anterior foram preservados.
- ELF/FSELF 011 auditados: todos os 4.955 inicializadores presentes,
  3.131.550 instruções decodificadas, zero syscalls diretas. FSELF SHA-256
  `ED915434DB7205E93D2A4A9A03875352BF39E6EED50BE6E92F6F7B13BD333DE6`.
  Dezoito testes locais passaram (9 runtime, 8 adaptação ELF, 1 extração de
  registros). Imagem 01.000.011 gerada no Desktop, 719 MiB, SHA-256
  `F45EED4DB3E466A45E132B0B98EEF98BD415C40CB9BC65E9C5B9D6FC7ADF783C`.
  Montada somente para leitura: versão/título, hash do eboot e hashes da
  capa/fundo conferidos; volume desmontado. Cores/FPS/layout no console
  ainda dependem de reteste.

## Crash da primeira fase: inicializadores omitidos pelo linker (21/10/2026)

- O novo `Pictures/boot.log` é da 01.000.009 e finalmente registra a falha:
  frame 4838, cena 2, acesso de I/O não adaptado em `0x40421000e`, PC
  `0x4d021a`. Considerando a carga do título em `0x400000`, a instrução é
  `SetBGnControlToText+0x9a` (ELF `0xd021a`), uma leitura da tabela BGnCNT.
  O segundo SIGSYS ocorre na saída do tratador; não é a falha inicial.
- Causa comprovada no ELF 009: o arquivo do jogo tem 4.955 registros de
  inicialização, mas somente 232 chegaram ao executável. A tabela de oito
  registradores em `0x02041ac0` estava ausente, deixando os endereços de
  hardware originais da ROM. O upstream usa um alvo OBJECT; nosso arquivo
  estático era extraído sob demanda. `KEEP` não extrai membros de um archive.
- O link agora aplica `--whole-archive` somente ao jogo. SDK e bibliotecas
  de runtime continuam com extração normal. Auditoria compara todos os
  registros (endereço/tamanho/módulo e IDs de função) do archive com o ELF e
  resolve a relocação PIE para conferir os oito valores da tabela BGnCNT.
  Resultado 010: 4.955/4.955 inicializadores e 2/2 registros de função,
  nenhuma entrada ausente ou extra, oito endereços na view I/O modelada.
- A assinatura e o empacotamento recusam inventário ausente/incompleto ou
  prova de outro ELF. Teste com o linker PS5 real reproduz o descarte de um
  objeto só de dados e verifica sua preservação com a correção, incluindo
  relocação da tabela. Oito testes de adaptação ELF e sete do runtime também
  passaram. São testes locais, não execução da primeira fase no console.
- O log 009 mostra `raw2d == composed`: a UI não apagou o vídeo. Há polígonos
  válidos, mas `PIX3D=0/0`. A perda de milhares de inicializadores é uma
  correção necessária para o estado do jogo, mas ainda não prova a solução
  de todo defeito gráfico. A captura limitada inclui agora CLEAR_COLOR e
  CLEAR_DEPTH. A base de código do diagnóstico vem do linker, sem depender
  de `dladdr`, que não informou a base neste console.
- ELF 010: SHA-256
  `AED9DA20E2586C21800B55D9B5CFB71737DA0EA8CAB212AB825BCA94B67A0987`;
  3.128.712 instruções auditadas, zero syscalls diretas; FSELF íntegro
  `B239DFD6E52B681E78E3E8AD52DC9CD69DFBC2C95ECC39798AA61D0E2624311E`.
  Imagem 01.000.010 gerada e montada somente para leitura para conferir
  versão, título, hash do eboot, capa e fundo; volume desmontado. Arquivo:
  uma imagem exFAT local de 719 MiB, SHA-256
  `34294B3AE0F792A2DB5F532289B7207C37094BB669EF0721F726F36DB22314F0`.
  Confirmação de gráficos, performance e entrada da fase no PS5 pendente.

## Lentidão, tela superior branca e crash na primeira fase (21/10/2026)

- O usuário confirmou vídeo na 01.000.008, mas descreveu tela superior branca,
  avanço lento e encerramento na entrada da primeira fase. O log confirma
  `REF=1`, conteúdo e flips. O branco inicial coincide com `MASTER=4010`, um
  fade branco total; a tela superior continua branca em amostras posteriores.
  A captura terminou antes de registrar a causa do crash, portanto não há PC
  de falha nesse arquivo para corrigir o encerramento com precisão.
- A saída 3840x2160 fazia a conversão de tiling e divisões por pixel em milhões
  de escritas dispersas e invalidava o buffer inteiro. O presenter agora usa
  uma tabela inversa do tile e escreve blocos sequenciais, com os índices de
  escala calculados antes; só sincroniza os tiles alterados. A resolução de
  saída e a imagem escalada são preservadas.
- A captura compara `raw2d` (antes da UI) e `composed` (depois), acompanha até
  oito cenas por estágio e amostra a primeira geometria. `PERF` separa tempo de
  trabalho do presenter e espera do flip. A coleta continua limitada.
- Corrigida uma lacuna do diagnóstico: sinais fatais e saídas por rotina não
  implementada/ABI eram reportados fora do `boot.log`. Um descritor append é
  preparado antes do boot; o registro de sinal usa apenas escrita direta e
  acrescenta PC, offset do ELF, cena e frame. O relatório de cena persiste
  além dos checkpoints da abertura, com limite de 16 transições.
- Sete testes passaram. A nova cópia confere cada pixel da saída em 1080p e
  4K contra a escala de referência; o teste de falha verifica o descritor,
  offset/cena/frame e limite do registro. São testes locais com APIs simuladas,
  não validação da primeira fase no console.
- ELF auditado: 3.128.720 instruções, zero syscalls diretas, FSELF íntegro
  `76E5FA4AE2E944842F44D5F1CB14A5107EA964BC61479DE7C3BAF3C862483F15`.
  Versão 01.000.009 gerada e conferida em montagem somente para leitura:
  uma imagem exFAT local de 719 MiB,
  SHA-256 `8B6D786AD0378F3BFD1D6D1A6CDFC502CF069A670A60CC47603AFABAA5D4EA4B`.
  `param.json`, eboot, capa e fundo conferidos; volume desmontado. Performance
  no PS5, tela superior e crash ainda dependem de reteste.

## Correção da composição omitida e captura completa (21/10/2026)

- Causa reproduzida localmente: texto/HUD/pausa ativados por padrão fazem o
  runtime pedir descrições `KHDAYS_WANT_2D`. A decisão antiga pulava o desenho
  dos bitmaps mesmo quando o presenter PS5 só consumia esses bitmaps. O VideoOut
  recebia os buffers ainda zerados.
- O adaptador agora exige que o host realmente desenhe 2D antes de permitir
  essa omissão. O fallback CPU compõe as telas e a UI mesmo com seus padrões
  nativos ativados. Camada 3D vazia em uma cena 2D não é, por si só, uma falha.
- A captura encadeia estado/cena, registradores, paletas/OAM/VRAM, composição,
  geometria/rasterização 3D e apresentação. É limitada a oito checkpoints por
  estágio, encerrados por `PIPE_DONE`. Detalhes em
  `docs/NATIVE-VIDEO-DIAGNOSTIC.md`.
- Seis testes locais passaram. O teste de pixels usa o rasterizador e as
  opções padrão reais do upstream; reproduz o preto com a decisão antiga e
  produz as duas cores esperadas com a correção. O teste VideoOut confirma
  também o limite de amostras, falhas e cópia de pixels com dispositivo simulado.
- Runtime PS5 compilado e ELF relinkado: 3.127.683 instruções decodificadas,
  zero syscalls diretas, FSELF íntegro. Imagem 01.000.008 gerada no Desktop:
  uma imagem exFAT local de 719 MiB.
  SHA-256: `0140B1B708B6C7ECA6F18FB4A85FD7E57934E66BFFA7454FFA348875EE37AF40`.
- A imagem foi montada somente para leitura: versão/título conferidos, capa e
  fundo presentes, SHA-256 do `eboot.bin` interno idêntico ao FSELF auditado
  (`EAE7D12955F360EEA4B821EC0A8644F56A59C6FA41C4806A21B2F87DBDF12A70`).
  Volume desmontado após a verificação; reteste do jogo no console pendente.

## Diagnóstico adicional da imagem preta (21/10/2026)

- O `boot.log` às 02:23 é da versão 01.000.006. O retorno do splash segue 0 e
  os flips são confirmados. Em todos os pontos registrados, a camada 3D do DS
  tem zero pixels e as imagens já compostas das duas telas também são pretas.
- O DISPCNT A (`00010308`) habilita a saída 3D como BG0, portanto a tela A fica
  preta enquanto a camada 3D estiver vazia. A tela B habilita BG1/OBJ e a
  paleta de fundo muda para `7c00`; o log ainda não registrava master brightness
  nem conteúdo das páginas VRAM, então não é possível distinguir escurecimento,
  bancos vazios ou outro problema no compositor.
- A versão 01.000.007 acrescenta justamente esses dados: master brightness,
  blending/janelas e contagem de bytes não nulos/mapeados em ABG, BBG e BOBJ.
  Isso é diagnóstico; não afirma corrigir o renderizador.
- ELF/FSELF relinkados: 3.127.226 instruções, zero syscalls diretas, FSELF
  íntegro. Imagem validada após desmontagem:
  uma imagem exFAT local de 719 MiB,
  versão `01.000.007`, SHA-256
  `87A993C266BFFDEB0DADEB34FE2AD9E78B3CE3E95C7F38488B0EE0C83DB90C9F`.
- A imagem foi montada somente para leitura após a geração; `param.json` indica
  `PPSA35820` / `01.000.007` e o SHA-256 de `eboot.bin` dentro dela confere
  com o arquivo de staging. Volume desmontado após a verificação.
- Teste de hardware pendente. Enviar o próximo `boot.log` permitirá escolher
  uma correção concreta.

## Diagnóstico do framebuffer preto (21/10/2026)

- O `boot.log`, com horário 02:11, corresponde à
  build 01.000.005. `sceSystemServiceHideSplashScreen` retornou 0 e os flips
  1–3 foram marcados como exibidos; logo, o splash e a submissão de vídeo não
  explicam mais a tela preta.
- As duas telas de origem registraram zero pixels RGB não pretos em todos os
  checkpoints até o frame 720. A hipótese mais provável agora é um caminho de
  renderização do DS que gera preto, mas ainda falta separar estado do jogo,
  registradores/VRAM e framebuffer 3D.
- A build 01.000.006 grava, a cada 120 frames, DISPCNT/BGCNT das duas telas,
  paletas, mapeamento VRAM, DISP3DCNT e quantidade de pixels não pretos na
  camada 3D. A coleta é feita no ponto de composição, sem mudar o conteúdo dos
  frames enviados.
- ELF recompilado e auditado: 3.127.002 instruções decodificadas, zero syscalls
  diretas; FSELF íntegro. Imagem exFAT criada no Desktop:
  uma imagem exFAT local de 719 MiB,
  versão `01.000.006`, SHA-256
  `90749F6D033B0239508365DD2A4083F405E8A0950DDD38273A5F136EEE786CD1`.
- Ainda depende de teste no console. Copie o novo `boot.log` após iniciar a
  01.000.006.

## Correção da tela encoberta pelo splash do PS5 (21/10/2026)

- A evidência local para o sintoma de áudio sem imagem foi encontrada em
  `toolchains/sources/PS5_Vulkan/docs/HARDWARE_FINDINGS.md`: o splash de
  lançamento cobre os frames do VideoOut até `sceSystemServiceHideSplashScreen`.
  O renderer de referência chama esse serviço antes de abrir o VideoOut; o
  presenter de Days não chamava.
- O presenter chama `sceSystemServiceHideSplashScreen` antes de inicializar
  VideoOut. O primeiro log do console confirmou três chamadas de envio de
  frames, mas não mostrou o retorno do splash; por isso ainda não dá para
  afirmar que o serviço desobstruiu a tela nem que os frames chegaram a ser
  exibidos.
- Corrigi a gravação concorrente do `boot.log`: cada checkpoint abre e fecha
  o arquivo, evitando que dois fluxos `FILE*` sobrescrevam registros. O log
  agora registra o retorno do splash, estado dos primeiros flips e contagem de
  pixels não pretos por tela, além dos pixels centrais.
- Rebuild e link concluídos; FSELF íntegro, 3.126.857 instruções decodificadas
  e zero syscalls diretas. Versão do pacote `01.000.005`.
- Nova imagem exFAT local: 719 MiB.
  SHA-256: `08C82DBE68B3C902EB70AE9FF4533F08619E6477D533D94F1415A1093A832B82`.
- A imagem ainda precisa de reteste no PS5. O próximo `boot.log` permitirá
  distinguir entre framebuffer do jogo sem conteúdo e falha na saída/apresentação
  de vídeo. Copie o log novo para um `boot.log` local.

## Correção de vídeo sem imagem (21/10/2026)

- O `boot.log` enviado pelo usuário chegou até `entering NitroMain` e confirmou
  que dados, runtime, áudio e entrada inicializam; o sintoma era áudio com
  framebuffer sem imagem.
- O presenter PS5 foi alinhado ao atributo de framebuffer usado pelo renderer
  VideoOut funcional do SDK local: `0x8000000022000000` (RGBA8 sRGB), mantendo
  a tiling 64 KiB R_X e a conversão de pixels B,G,R,A.
- O teste local `native_ps5_video_test` passou novamente (buffers, pixels,
  flips, ciclo de vida e falhas simuladas). O ELF final foi relinkado com
  3.126.640 instruções auditadas e zero syscalls diretas; FSELF permanece
  íntegro.
- Nova imagem exFAT local, 719 MiB. SHA-256:
  `B7ACE7781D6E226E3CADA87176F658EBB9F09F69412AB3A9C4A1093831408340`.
  FSELF SHA-256: `A12FDB7F8A3064B48BB10FA0F2F21D8569B1844672AD651CDCBF652B35246494`.
- O arquivo exFAT anterior `native-bootfix` foi removido para liberar espaço
  temporário do C:; fontes, ROM, extração e staging continuam preservados.
- A imagem nova ainda precisa de confirmação no PS5. Se continuar sem vídeo,
  o próximo dado necessário é um novo `boot.log`/klog dessa imagem para saber
  se o loop chamou `sceVideoOutSubmitFlip`.

## Correcao de dados de boot e memoria direta (21/10/2026)

- O usuario confirmou que `native-syscallfix` abre e fecha imediatamente. A
  causa exata no console ainda depende de log, mas duas falhas foram localizadas.
- O staging reutilizado tinha dados do prototipo (1.720 arquivos, sem
  `system/header.bin` nem o layout `nitrofs`). O leitor nativo de producao
  rejeitou essa arvore. A extracao YKGP correta foi preservada e recolocada.
- Teste do leitor contra a ROM: 1.898 arquivos NitroFS, 261.842.549 bytes
  comparados e zero falhas. A mesma extracao foi validada por SHA-256 na origem,
  no staging e dentro da imagem montada somente para leitura.
- A janela DS e os heaps expandidos agora usam reserva virtual e memoria direta
  do SDK nativo, em vez de 1,5 GiB de mapas anonimos/flexiveis. Espelhos, pilhas,
  reset e retornos de erro passaram nos testes locais com APIs de dispositivo
  simuladas; compilacao PS5 e link do runtime tambem passaram.
- O boot grava checkpoints em `/data/khdays-native/saves/boot.log`, inclusive
  antes do mapa de memoria e da leitura de dados. Isso permite localizar um
  eventual encerramento sem depender somente da captura de klog.
- Nova imagem exFAT local de 719 MiB,
  titulo `PPSA35820`, versao `01.000.003`, capa, fundo e ROM preservados.
- FSELF: `6B0C3249C0CD4351138D448FCC63D9ED8D5DDFABAABC3CCE2E6CF0683FE5E4C7`.
- exFAT: `7BAD934D5A61F985FC24C9350AE10C8F4E05D3334EBB047EDBE3BD4F4A5A8F9B`.
- Auditoria: 3.126.640 instrucoes, zero syscalls diretas; FSELF valido. O
  empacotador valida os dados mesmo com `-ReuseStage`, recusando o staging antigo.
- A imagem e os dados temporarios do prototipo foram substituidos; fontes,
  extracao original e ROM foram preservados. Teste desta build no PS5 pendente.

## Correcao do link de payload no titulo nativo (21/10/2026)

- O usuario confirmou a repeticao de `System illegal function call, eboot.bin`
  com a ultima imagem diagnostica. A desmontagem do ELF revelou que `mmap` e
  `mprotect` eram implementacoes locais vindas das bibliotecas de payload e
  executavam `syscall` diretamente em eboot.
- O link retirou `libc.a`, `libpthread.a`, `libdl.a`, `libufs.a` e os shims
  opcionais do payload. Agora segue o contrato do Melon Mix: `libps5platform.a`,
  adaptadores de heap/thread e imports de modulos nativos. `mmap` e `mprotect`
  sao imports; nenhum stub que retorna sucesso substitui essas operacoes.
- Auditoria do novo ELF: 3.125.474 instrucoes decodificadas, zero instrucoes
  diretas de syscall. Tres testes de regressao do auditor passaram. O
  empacotador recusa provas ausentes/desatualizadas antes de alterar o staging.
- Nova imagem no Desktop: `PPSA35820-native-syscallfix.exfat`, 705 MiB. A imagem
  anterior foi substituida para reutilizar o espaco, preservando fontes/assets.
- FSELF SHA-256:
  `C17858BA5892A1A0CE4BAD5A453749322E8FFFE01343B9B14B1EC89FD7C34335`.
- exFAT SHA-256:
  `DB2FA2B1D545882AAE0DDED8706215D4AA4CE9D6127F148DF3906CAA5DE1045A`.
- Essa imagem passou na verificacao estrutural, mas seu reteste abriu e fechou
  imediatamente. A contagem de 1.720 arquivos nao validava o contrato do leitor
  nativo. Foi substituida pela build `native-bootfix` descrita acima.
- Falta o log atual para vincular o PC do crash a uma chamada especifica. O
  defeito de link foi corrigido; ainda nao ha confirmacao de boot no console.

## Diagnostico de "System illegal function call, eboot.bin" (20/10/2026)

- A mensagem foi vista na imagem anterior (FSELF `4F0932F7...824FC88B46`). A
  causa exata ainda nao esta identificada: nao temos klog ou captura da tela
  referente a essa execucao.
- Comparacao local com o Melon Mix: autoridade e tipo FSELF iguais; o `libc.prx`
  tem hash identico e os metadados do titulo seguem o mesmo esquema.
- A captura de `stderr` para klog agora inicia antes do primeiro mapeamento de
  memoria, e o diagnostico de falhas inclui `SIGSYS`.
- Runtime, link e conversao passaram; FSELF indica `integrity: valid`. Isso nao
  prova boot no console nem confirma a origem da mensagem.
- Imagem exFAT daquela etapa: `PPSA35820-native-melon-layout.exfat`, 705 MiB,
  validada em montagem somente para leitura; inclui o novo `eboot.bin`, capa,
  fundo, ROM, `libc.prx` e 1.720 arquivos de dados.
- FSELF SHA-256:
  `F51C8151EB0DE70CDBDC4FE018BEF873F2785BB0A08089ADDBCA91B088AB995E`;
  exFAT SHA-256:
  `C1DD6AA48CA09194AC308983B4E784C007FD32EF2C5D1C6125F0301A83828936`.
- O reteste dessa imagem repetiu o erro. Ela foi substituida pela correcao de
  link de 21/10/2026 descrita acima.

## Build nativa para teste PS5 (20/10/2026)

- O runtime foi ligado com `ld.lld` em ELF PIE e convertido para o layout PS5.
- O link usa o CRT/layout de título PS5 do Melon Mix. O entrypoint foi
  alterado de `0x0` para `0x1b0`; isso e uma correcao estrutural, ainda nao
  comprovada como causa unica do crash.
- FSELF gerado e inspecionado com `integrity: valid` em
  `build-native-game-ps5/eboot.bin`.
- Imagem exFAT compatível com o layout do Melon Mix pronta localmente, com cluster de
  64 KiB, `sce_sys/param.json` sem BOM na raiz, capa/fundo, ROM, `libc.prx` e
  1.720 arquivos de dados.
- Essa versao foi substituida pela rebuild diagnostica descrita acima. Build
  WIP, nao um PKG retail.

## Integracao do runtime PS5 (20/10/2026)

- O runtime C/C++ foi compilado em `build-native-game-ps5/libkhdays-native-ps5-runtime.a`.
- A linkagem relocavel que reune gameplay, runtime, host, GPU, grafico e som foi aprovada em
  `build-native-game-ps5/runtime-validation/khdays-native-runtime.o`.
- O relatorio `runtime-validation/validation.json` encontrou zero simbolos de projeto
  indefinidos; os 294 simbolos restantes pertencem a libc/libc++, imports PS5 e limites do
  linker final.
- SHA-256 do runtime nessa etapa anterior: `0154BD2632A37D0E6B4B8FFABD57A74304B3CA093DBADCC27609C3743638235F`.
- O marco de integracao produziu as bibliotecas; o link final/FSELF e a imagem
  exFAT foram gerados depois (consulte a secao de reteste acima). O boot real
  no hardware ainda depende de validar a nova imagem.

## Executável nativo e apresentação (18/10/2026)

### Atualização da compilação do gameplay (20/10/2026)

- A adaptação ELF compilou integralmente o alvo `khdays-native-ps5-game` com
  Ninja incremental (`-j8`), incluindo as correções de compatibilidade C
  aplicadas no adaptador. O arquivo estático gerado é
  `build-native-game-ps5/libkhdays-native-ps5-game.a` (SHA-256
  `10F92F0FD77F1CDA7AF32B29BB4F4C79917C79CAC3ACAAE5514D0F2B4C8EE648`).
- Essa biblioteca não é um executável: o link do host completo ainda precisa
  reunir o runtime, a camada VideoOut/Vulkan, entrada, áudio, dados e o
  entrypoint PS5. Portanto, nenhum `eboot.bin` ou exFAT jogável foi gerado.

- C1060 reproduzido como falha de memória em dois arquivos; ambos passaram
  isoladamente. Retomada aplicada com execução sequencial, cache preservado,
  liberação de textos intermediários e checkpoint antes da reescrita.
- Testes do gerador e do checkpoint aprovados; o pipeline de geração e
  compilação foi reiniciado em segundo plano. Monitor:
  `powershell -File scripts/monitor_native_prepare.ps1`.

- `libc.prx` reconstruído duas vezes, com hashes idênticos e integridade válida.
- Capa 512×512 e fundo 1920×1080 preparados na pasta `PPSA35820/sce_sys`.
- Bibliotecas upstream de GPU, composição/geometria e som compiladas para PS5,
  junto de contexto, relógio/arquivos e tabelas ELF: 33 objetos verificados.
- Compilação automática do gameplay preparada para continuar após o gerador;
  etapa real registrada em `build-native-game-ps5/status.json`.
- `eboot.bin` final e exFAT ainda pendentes da integração e do link do host.
  Evidências e comandos em
  [`docs/NATIVE-EXECUTABLE-STATUS.md`](docs/NATIVE-EXECUTABLE-STATUS.md).

## Retomada sobre o runtime nativo real (17/10/2026)

- `khdays-port` 0.1.1 e o `khdays-decomp` correspondente foram fixados por
  commit em `native/upstream-lock.json`; o protótipo anterior permanece apenas
  como backend e referência de plataforma.
- A troca de contexto cooperativa do Windows foi substituída por uma versão
  x86-64 System V para PS5. O arquivo Prospero ELF64 compilou sem warnings e o
  teste upstream de 200 contextos também compilou como contrato.
- Relógio monotônico, leitura de arquivos, save atômico e hora local foram
  implementados com APIs presentes no SDK público e compilados para Prospero.
- O mapa de memória PS5 usa `mmap`, `shm_open` e espelhos compartilhados para
  conservar a organização de RAM esperada pelo código do Nintendo DS.
- A ROM local americana é `YKGE`; a revisão nativa atual precisa da europeia
  `YKGP`, cujo hash esperado está registrado sem incluir qualquer dado do jogo.
- A ROM europeia fornecida pelo usuário é `YKGP`, mas pertence a uma revisão
  não registrada (`d9e103c6...a373`); a extração de desenvolvimento foi concluída
  com 1.898 arquivos NitroFS e permanece fora do Git.
- A preparação oficial dos fontes está rodando em processo independente; seu
  estado pode ser conferido com `scripts/monitor_native_prepare.ps1` (PID 4752).
- Evidências e reprodução: `docs/NATIVE-PORT-BASELINE.md` e
  `build-native-context-ps5/validation.json`.

## Correção do crash de inicialização AGC (15/10/2026)

- O link anterior incorporava `libSceAgc_stub.o` e `libSceAgcDriver_stub.o` como
  funções locais; esses stubs de compilação retornavam erro e não deveriam ser
  executados no console.
- `scripts/relink_ps5_title.ps1` agora cria import stubs DYN, relinka com PIE e
  páginas de 16 KiB, verifica que as funções AGC não são definições locais e
  converte/assina um novo FSELF.
- Novo `eboot.bin`: SHA-256
  `5C4F696DC183064605E04ADE447B76B479926EAAE1A353DE73BF1841E0602F81`;
  inspeção: `integrity: valid`.
- Nova imagem para teste: `build-vulkan-title-4/dist/PPSA12345-agcfix.exfat`;
  SHA-256 `7D28D18044F53487B41EE40458080B1575368F01C514B378C93B2371C6BA80C8`.
- O boot no firmware 13.60 ainda está pendente; o código `CE-108255-1` não é
  considerado resolvido até um teste no console e, se necessário, o log
  `/data/shadowmount/debug.log`.

## Correção Vulkan conservadora e imagem com arte (15/10/2026)

- O log atualizado confirmou que `PPSA12345-agcfix-2.exfat` montava e instalava
  corretamente, mas o processo caía em aproximadamente um segundo.
- O título foi ajustado para iniciar pela API Vulkan 1.0 e pelo caminho de
  render pass legado do template, desativando temporariamente dynamic rendering,
  stencil obrigatório e o `pNext` Vulkan 1.3 até haver prova no console.
- FSELF novo: 42.264.913 bytes, inspeção `integrity: valid`.
- A imagem inclui `sce_sys/icon0.png` (capa 512x512) e `sce_sys/pic0.png`
  (background 1920x1080), além dos shaders e dados obrigatórios.
- Nova imagem: `build-vulkan-title-4/dist/PPSA12345-vk-safe-capa.exfat`;
  498.073.600 bytes; SHA-256
  `36290AA0D0CF9BECFCD3CE47D472A8A7B384BB015B334CCEB93C0C0A3A2158D9`.
- Boot no PS5 13.60 continua pendente; esta é uma hipótese técnica verificável,
  não uma garantia de execução.

## ShadowMountPlus registration (15/10/2026)

- Found a packaging defect: the first exFAT image omitted sce_sys/icon0.png.
  ShadowMountPlus aborts staging if the icon copy fails, before registration.
- Added scripts/build_shadowmount_image.ps1 to include and validate the icon,
  both model shaders, and required runtime files before and after packaging.
- New output: build-vulkan-title-4/dist/PPSA12345-fixed.exfat.
- Console registration and execution still require a hardware test.
- Evidence and reproduction: docs/SHADOWMOUNT-INSTALL-DIAGNOSTIC.md.

## Runtime PPSA12345 (14/10/2026)

- O smoke test foi substituído pelo título `PPSA12345`, com Vulkan/RADV,
  decodificadores do 358/2 Days, reprodução MODS, áudio e dados staged em
  `build-vulkan-title-4/dist/PPSA12345`.
- O executável agora também compila e instancia `gameplay_core` e
  `world_runtime` no mesmo loop Vulkan, incluindo a fatia inicial de mundo,
  cutscene, HUD de missão e comandos de ataque/pular cena.
- `eboot.bin` foi convertido e assinado; a inspeção reportou `integrity: valid`.
- Dados staged conferidos: 1.720 arquivos, 259.710.067 bytes.
- Limite atual: isto ainda é uma fatia jogável integrada, não a implementação
  completa de todas as missões, telas, transições e assets do jogo original.
- Imagem exFAT criada e verificada em `build-vulkan-title-4/dist/PPSA12345.exfat`;
  usa cluster de 64 KiB e mantém `eboot.bin`/`sce_sys/param.json` na raiz.

## Toolchain RADV/CLC (14/10/2026)

- LLVMSPIRVLib, mesa_clc.exe, vtn_bindgen2.exe e glslangValidator 15.4.0
  foram compilados e validados no host Windows.
- RADV release foi compilado para PS5 em
  `.deps/native/radv-release/lib/libvulkan_radeon.ps5.a`, com
  `PROVENANCE.txt` registrando Mesa `47e9eb61ee7e` e SDK `b83202be...`.
- O smoke title foi linkado em ELF e convertido para FSELF; o resultado atual
  esta em `toolchains/sources/PS5_Vulkan/dist/PPSA99014/eboot.bin`.
- A camada de emulated TLS, o caminho RADV PS5 e o layout de parametros foram
  corrigidos sem stubs falsos; o `libc.prx` tambem foi reconstruido e validado.
- Ainda nao ha teste de boot no console e ainda nao existe PKG de distribuicao.

**WIP — adaptação em andamento; ainda não é uma build jogável completa para
PS5.**

## Concluído

- núcleo portátil de front-end, gameplay, combate, HUD, missão e save;
- configuração lógica 4K (`3840x2160`) com viewport 4:3;
- toolchain pública PS5 Payload SDK v0.44 e LLVM/Prospero instalada localmente;
- compilação cruzada do núcleo e das bibliotecas de mídia para Prospero;
- árvore local do jogo staged com 1.718 arquivos e hashes, sem versionar ROM ou
  dados proprietários;
- catálogo de 114 missões e 10 mundos;
- decodificação P2/LZ, CAKP/KAPH, NFTR, NSBMD/TEX0, SDAT e MODS/MobiClip;
- conversão das texturas para RGBA e das malhas para buffers neutros;
- backend real de recursos Vulkan com `VkImage`, sampler, buffers device-local,
  atualização da textura MODS e quad de vídeo;
- conversão/reamostragem para PCM16 estéreo a 48 kHz e ligação ao
  `audio_start()` nativo, cuja implementação do template usa `sceAudioOut`;
- agendamento de frames MODS pelo FPS do contêiner;
- programa final `VulkanExample` criado com renderização dinâmica Vulkan 1.3,
  pipelines de malha/vídeo, layouts, descriptors e gravação no command buffer;
- ligação do loop de frame ao upload de modelos, apresentação MODS e áudio
  `sceAudioOut`, usando `/app0/data` como raiz no PS5;
- gerador reproduzível do título `PPSA12345` sobre o
  `PS5_VulkanTemplate`, sem versionar SDK ou dados proprietários;
- smoke test com assets reais aprovado: 12 texturas, 13 malhas, dois frames de
  vídeo e áudio não silencioso;
- build Prospero de `khdays-ps5-native-media-runtime` aprovado;
- compilação integral do alvo `samples` gerado aprovada: 47/47 unidades,
  incluindo título, Vulkan Template, decodificadores e runtime de mídia.

Os detalhes e comandos reproduzíveis estão em
[`docs/RUNTIME-MEDIA-PIPELINE.md`](docs/RUNTIME-MEDIA-PIPELINE.md).

## Atualizacao da toolchain (14/10/2026)

- A revisao do Payload SDK fixada pelo template (`b83202be73e930050e00de0f1b4d9ec46c0391bf`)
  foi instalada em `D:\\KH-Days-PS5-toolchain\\ps5-payload-sdk-b832` (NTFS).
- O zlib 1.3.2 foi compilado e registrado no cache nativo.
- O link RADV ainda depende de `LLVMSPIRVLib`/Mesa CLC, ausentes no LLVM local;
  nenhuma biblioteca RADV incompleta foi incluida no titulo.

## Ainda pendente

- ligar o catálogo, scripts e transições ao loop completo de todas as missões;
- integrar fontes, HUD e todas as telas 2D ao renderer final;
- instalar a revisão do Payload SDK fixada pelo template em NTFS/WSL e concluir
  o link RADV do ELF/FSELF; o volume exFAT atual não preserva os symlinks Unix;
- testar Vulkan/VideoOut, DualSense e `sceAudioOut` no PS5 homebrew real;
- somente após o teste no console, reconstruir uma distribuição exFAT válida.

A compilação das 47 unidades para Prospero comprova compatibilidade do código
fonte, não funcionamento no console. O link final e o teste em hardware ainda
são obrigatórios; nenhum resultado de hardware é declarado sem teste real.
