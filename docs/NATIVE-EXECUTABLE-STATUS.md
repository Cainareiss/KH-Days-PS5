# Executável nativo PS5: artefatos e limites

Estado verificado em 18/10/2026. O alvo usa `khdays-port` 0.1.1 conforme
`native/upstream-lock.json`. A pasta do título em preparação é
`build-native-link/PPSA35820/`.

## Disponível

- `sce_module/libc.prx`: reconstruído duas vezes com resultado idêntico;
  inspeção FSELF confirmou `integrity: valid`. Relatório completo em
  `build-native-libc/validation.json`.
- `sce_sys/icon0.png`: capa escolhida pelo usuário, 512×512, copiada sem
  conversão da preparação anterior.
- `sce_sys/pic0.png`: fundo correspondente, 1920×1080, copiado sem conversão.
  Imagens decodificadas e hashes conferidos em
  `build-native-link/PPSA35820/artwork.validation.json`.
- Bibliotecas de contexto, relógio/arquivos, tabelas ELF, GPU de referência,
  composição/geometria e som, compiladas para `x86_64-sie-ps5`.
  As seis bibliotecas contêm 33 objetos ELF64 relocáveis e os símbolos
  necessários à próxima integração. Evidência em
  `build-native-host-ps5/archives.validation.json`.
- Teste das tabelas ELF com coleta de seções habilitada: registros de dados
  com 24 bytes, registros de funções com 16 bytes, ambos alinhados a 8 bytes
  e preservados pelo linker. Resultado em
  `build-native-elf-tables-test/validation.json`.

As bibliotecas gráficas acima são componentes do runtime upstream; esse
resultado não comprova apresentação Vulkan ou funcionamento no console.

## Geração e compilação do gameplay

Na retomada de 20/10/2026, o alvo inteiro do gameplay terminou com Ninja
incremental e produziu `build-native-game-ps5/libkhdays-native-ps5-game.a`.
O hash SHA-256 verificado é
`10F92F0FD77F1CDA7AF32B29BB4F4C79917C79CAC3ACAAE5514D0F2B4C8EE648`.
Esse é um arquivo estático relocável, não um `eboot.bin`; ele confirma a
compilação dos fontes decompilados, mas não a execução no PS5.

A primeira execução falhou em dois arquivos com MSVC C1060. Ambos passaram
isoladamente com o compilador HostX64. A retomada usa
`scripts/prepare_native_bounded.py`: um worker no próprio processo, sem fila
antecipada de tarefas nem cópias do índice do programa em subprocessos. Os
textos intermediários já gravados são liberados antes da reescrita. Um
checkpoint permite repetir essa fase sem recalcular toda a preparação.

Os testes com duas unidades reais verificaram a geração completa e a
reutilização do cache sem chamar novamente o pré-processador. O teste do
checkpoint confirmou a preservação dos símbolos e a liberação dos textos.
Resultados em `build-native-generator-check/validation.json` e
`.cache/khdays-native-v0.1.1/build/ps5-gen/preflight-low-memory.json`.

Para iniciar ou retomar o fluxo usando essa correção:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_native_game.ps1 `
  -Prepare -PrepareJobs 1 -Jobs 1
```

`generator-state.json`, na pasta `build/ps5-gen`, registra a etapa e os
contadores de cada fase. Um percentual de fase não é o percentual do port
inteiro. O monitor identifica o novo processo automaticamente, sem um PID fixo.

`scripts/build_native_game.ps1` pode aguardar o gerador já em execução e,
quando ele concluir, adaptar os fontes e compilar a biblioteca inteira do
gameplay. Essa etapa preserva `long` com 32 bits no código do jogo, mantém
ponteiros nativos com 64 bits e troca os registros COFF pelas tabelas ELF.
Os fontes upstream permanecem intactos.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_native_game.ps1 `
  -WaitForPrepareProcessId 4752 -Jobs 2
```

O número do processo é específico desta execução. Consulte os arquivos antes
de iniciar outro processo:

- `build-native-game-ps5/status.json`: etapa e eventual erro;
- `build-native-game.log` e `build-native-game.err.log`: compilação;
- `build-native-prepare.log` e `build-native-prepare.err.log`: geração;
- `scripts/monitor_native_prepare.ps1`: geração, compilação e arquivos do título.

Não é necessário iniciar outro gerador enquanto o atual está ativo. O
script de contexto também verifica essa condição antes de gerar novamente.

## Ainda necessário para o eboot

A compilação atual ainda não define um alvo executável que reúna o jogo e
todo o host. Além da geração/compilação do gameplay, faltam a integração das
interfaces SDL3/GPU usadas pelo front-end, os serviços Windows ainda presentes
no runtime e a ligação dessas interfaces ao VideoOut, controles e áudio do
PS5. Depois vêm o link completo, inspeção dos imports e conversão para FSELF.

O `libc.prx` isolado foi validado, mas a cobertura dos imports do jogo só pode
ser confirmada após o link completo. Não existe `eboot.bin` nativo final nem
imagem exFAT nativa pronta para teste nesta etapa.

## Empacotamento com as imagens

`scripts/prepare_native_artwork.ps1` prepara e valida os dois PNGs na pasta
nativa. `scripts/build_native_exfat.ps1` os usa por padrão e inclui o nome do
jogo em `localizedParameters` para inglês e português. O relatório do exFAT
também registrará os hashes da capa e do fundo.

O fluxo do pacote está em [NATIVE-EXFAT-WORKFLOW.md](NATIVE-EXFAT-WORKFLOW.md).
