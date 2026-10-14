# Linha de base do front-end PC

Este documento registra o que foi validado localmente antes de instalar uma
toolchain específica de PS5. Nenhuma ROM, asset extraído ou binário é
publicado neste repositório.

## Fonte usada

- revisão USA `YKGE` fornecida localmente pelo proprietário;
- SHA-256: `3cbbe3f15b49fc26f43edaf0050899b3cc1091a25e7a496373409c7c8c6fd0cb`;
- 1.713 arquivos NitroFS extraídos;
- idiomas en/fr verificados pelo manifesto do front-end.

Os arquivos ficam somente na árvore local de dados do `khdays-port`.

## Compilação reproduzida

No Windows com Visual Studio 2022 e CMake:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DKHDAYS_ENABLE_UI=OFF -DBUILD_TESTING=OFF
cmake --build build --config Release --target khdays-port -- /m
```

O alvo `khdays-port.exe` foi gerado em `build/Release`. O executável é um
artefato local de teste e não faz parte do repositório público.

Durante a configuração foi necessário aceitar as duas localizações conhecidas
do `mobiclip_reference.cpp` no `khdays-decomp`; a revisão atual usa
`libs/mobiclip/video/portable/`. Essa compatibilidade está documentada no
checkout local do runtime, sem alterar o contrato do port PS5.

## Smoke tests de assets reais

Com os dados extraídos, o runtime gerou sem erro de processo:

- logos de inicialização;
- tela de título com `STORY MODE` e `MISSION MODE`;
- seleção de personagem com os retratos;
- tela de salvamento com os slots e `DAY 000 / BEGINNER`.

As imagens de teste permanecem fora do repositório. O resultado confirma que
o pipeline PC consegue carregar os recursos reais da ROM e fornece uma base
visual para o adaptador de plataforma.

## Limites conhecidos

- 958 arquivos comprimidos foram descomprimidos;
- 486 entradas de UI foram desempacotadas;
- três arquivos anônimos apresentaram falha LZ10 e permanecem rastreados como
  exceções de dados; eles não foram necessários para os smoke tests do
  front-end;
- o fluxo `--game-demo` é um teste de cenas placeholder, não uma prova de
  gameplay completo;
- ainda não existe ELF, FSELF ou PKG PS5.

O próximo passo de plataforma é instalar uma toolchain PS5 compatível e
implementar o backend de vídeo/input. Até lá, o build PC e a validação dos
assets são a referência funcional do projeto.
