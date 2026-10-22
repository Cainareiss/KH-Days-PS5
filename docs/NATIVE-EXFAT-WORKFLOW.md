# Fluxo exFAT nativo (WIP)

Atualizacao 21/10/2026: a imagem de teste fica somente na máquina local. O
empacotador valida
o manifesto da extracao nativa completa antes de tocar no staging. Um caminho
pai com uma unica extracao de hash e resolvido automaticamente. O layout deve
conter `system/header.bin`, os demais cinco arquivos de sistema e `nitrofs`
com os 1.898 arquivos da ROM europeia. `-ReuseStage` tambem valida esses dados;
`-RefreshData` substitui somente o subtree de dados gerados no staging quando
for preciso sincronizar a extracao. A ROM e a extracao original ficam intactas.

Para refazer o pacote com o novo executavel:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_native_exfat.ps1 `
  -EbootPath build-native-game-ps5/eboot.bin `
  -LibcPrxPath build-native-link/PPSA35820/sce_module/libc.prx `
  -ImagePath build-native-exfat/dist/PPSA35820-native-bootfix.exfat `
  -ReuseStage -RefreshData
```

O eboot precisa do relatorio correspondente de `link_native_ps5_elf.ps1`, com
integridade FSELF e auditoria sem instrucoes diretas de syscall. A arvore de
1.720 arquivos do prototipo anterior nao satisfaz o contrato do leitor nativo.
As instrucoes abaixo registram o planejamento anterior.

O projeto agora possui um empacotador separado para a futura build nativa:
`scripts/build_native_exfat.ps1`.

Ele não usa o `PPSA12345` do protótipo Vulkan. O título nativo fica isolado em
`PPSA35820` e só aceita um `eboot.bin` produzido pelo port nativo, além do
`libc.prx` correspondente. O script também valida o boot sector exFAT e exige
clusters de 64 KiB.

## Pré-requisitos reais

A preparação atual da capa e do fundo é feita com:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/prepare_native_artwork.ps1
```

Ela decodifica os PNGs, confere 512×512 para o ícone e 1920×1080 para o fundo,
e os copia sem alteração para `build-native-link/PPSA35820/sce_sys/`.
O empacotador usa esses caminhos por padrão e inclui o nome do jogo nos
metadados do título.

Ainda são necessários, no mesmo build compatível:

- `eboot.bin` nativo final, linkado e convertido para FSELF;
- `sce_module/libc.prx` da mesma toolchain/revisão;
- dados extraídos em `.cache/khdays-native-v0.1.1/data/extracted`;
- uma ferramenta de criação de imagem exFAT compatível com ShadowMountPlus.

Sem os dois primeiros arquivos, não é correto gerar uma imagem testável: uma
imagem contendo o executável do protótipo apenas reproduziria os crashes
anteriores.

## Comando planejado

Depois de o link nativo existir, executar na raiz do repositório:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build_native_exfat.ps1 `
  -EbootPath .\build-native-link\PPSA35820\eboot.bin `
  -LibcPrxPath .\build-native-link\PPSA35820\sce_module\libc.prx
```

O resultado será:

```text
build-native-exfat/dist/PPSA35820-native.exfat
build-native-exfat/dist/PPSA35820-native.exfat.validation.json
```

O relatório registra os hashes do exFAT e do `eboot`, a origem dos dados e o
status `hardwareTest: Pending`. O teste no PS5 continua sendo obrigatório.

## Estado atual

A extração da ROM europeia fornecida já está disponível, mas o gerador de
fontes ainda está concluindo os artefatos derivados. Portanto, esta etapa
prepara o caminho de empacotamento; ela não declara uma build PS5 jogável nem
gera uma imagem vazia ou baseada no protótipo.
