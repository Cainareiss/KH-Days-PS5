# Estado da toolchain PS5

## Componentes baixados localmente

- `ps5-payload-dev/sdk` release `v0.44`;
- `mihawk-99/PS5_VulkanTemplate` shallow clone em `91179df`;
- `mihawk-99/PS5_Vulkan` shallow clone em `252bf91`.
- `mihawk-99/PS5_PayloadSDK` clone em `b5efad5`; o repositório contém a
  revisão `611893f` fixada pelo template.

Os fontes e o SDK ficam em `toolchains/`, que é ignorado pelo Git. Nenhum
SDK proprietário da Sony, firmware, chave ou módulo do sistema foi baixado.

O ZIP do SDK foi obtido pela release oficial e conferido localmente:

```text
ps5-payload-sdk.zip
SHA-256: 4D06EAB777BB135705B5A6816688C9132EFB246093FE606AC488AB6CF65CA5B0
```

## Limite atual

O SDK v0.44 e o LLVM 23 compilam o núcleo, os decodificadores e o backend de
mídia para Prospero. O provedor `platform.c` do Vulkan Template também foi
compilado e exporta `audio_start`, com referências aos quatro símbolos
`sceAudioOut` esperados.

O SDK fixado do template inclui uma camada `libps5platform` que não existe na
release v0.44 pura. A instalação automatizada dessa revisão usa links
simbólicos Unix; ela não pôde ser materializada no volume exFAT `E:`. Para o
link do título RADV final, essa instalação deve rodar em NTFS com suporte a
symlink ou em um filesystem Linux/WSL. Os fontes foram preservados e a
toolchain antiga não foi apagada.

O bootstrap completo que compila Mesa/RADV ainda não foi executado. Isso não
impede a compilação das bibliotecas estáticas feita neste marco, mas impede
afirmar que o título Vulkan final já está linkado e empacotado.

## Smoke test do compilador

O host LLVM 23.1.3 foi extraído localmente e o wrapper Windows retornou o alvo
`x86_64-sie-ps5`. O sample oficial `samples/hello_world/main.c` também gerou
um ELF em `toolchains/smoke/hello_world.elf` usando:

```powershell
prospero-clang.cmd -nostartfiles -Wall -Werror -g `
  -o hello_world.elf samples/hello_world/main.c target/lib/crt1.o
```

O `-nostartfiles` é necessário neste primeiro teste porque o wrapper v0.44 e
o Clang 23 encontraram o `crt1.o` automático e o manual duplicado. O ELF foi
inspecionado como ELF64 x86-64, ABI FreeBSD, tipo `SharedObject`. Isso valida o
host/compiler/linker local; ainda não valida execução no console.

Depois, o `core/` do projeto e `khdays-ps5-native-media-runtime` foram
compilados com `toolchain/prospero.cmake`. Os objetos são ELF relocatable
x86-64. A ligação do título Vulkan final ainda depende do SDK fixado, do RADV
e do `VulkanExample` que cria os pipelines e descriptor sets.
