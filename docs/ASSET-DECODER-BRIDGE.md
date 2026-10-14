# Ponte de decodificação de assets

O runtime PS5 agora possui uma integração opcional com o port PC. Ela mantém
os decodificadores em uma biblioteca neutra e expõe ao backend PS5 apenas
metadados decodificados (`AssetDecodeSummary`), sem colocar SDL ou lógica de
gameplay dentro deles.

Formatos cobertos pela ponte:

- P2 e LZ10/LZ11;
- CAKP/KAPH e scripts de ação;
- NCLR, NCGR e NSCR (paleta, tiles e tilemap 2D);
- NSBMD/TEX0 (malhas, bindings e texturas);
- NFTR (glifos e métricas de fonte);
- SDAT (sequências, streams e bancos de áudio);
- MODS/MobiClip (metadados de frames, resolução e áudio).

Como o port PC é mantido fora deste repositório, a ponte é ativada apontando o
CMake para a cópia local:

```powershell
cmake -S . -B build-asset-bridge-vs -G "Visual Studio 17 2022" -A x64 `
  -DKHDAYS_ENABLE_PC_ASSET_DECODERS=ON `
  -DKHDAYS_PC_PORT_ROOT="E:/DOCUMENTOS/KH-ReCoded-PTBR/tools/khdays-port" `
  -DKHDAYS_PS5_BUILD_TESTS=OFF `
  -DKHDAYS_PS5_BUILD_RUNTIME=OFF
cmake --build build-asset-bridge-vs --config Release --target khdays-ps5-asset-smoke
./build-asset-bridge-vs/Release/khdays-ps5-asset-smoke.exe `
  ./build-full-game-data/data
```

O smoke test atual valida, com os arquivos reais, mensagens P2, NFTR, SDAT,
MODS, KAPH/NSBMD, NCLR e NCGR. O catálogo gerado por
`build_mission_catalog.ps1` registra as 114 missões e 10 mundos disponíveis.

Essa ponte agora alimenta `RuntimeMediaPipeline`, `VulkanMediaBackend` e
`NativeMediaRuntime`. A integração de upload Vulkan, saída nativa de áudio e
frames MODS está descrita em `RUNTIME-MEDIA-PIPELINE.md`. O jogo ainda não é uma
build completa: faltam os pipelines do título final, a ligação de todas as
missões e a validação no console.
