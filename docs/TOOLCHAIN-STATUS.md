# Estado da toolchain PS5

## Componentes baixados localmente

- `ps5-payload-dev/sdk` release `v0.44`;
- `mihawk-99/PS5_VulkanTemplate` shallow clone em `91179df`;
- `mihawk-99/PS5_Vulkan` shallow clone em `252bf91`.

Os fontes e o SDK ficam em `toolchains/`, que é ignorado pelo Git. Nenhum
SDK proprietário da Sony, firmware, chave ou módulo do sistema foi baixado.

O ZIP do SDK foi obtido pela release oficial e conferido localmente:

```text
ps5-payload-sdk.zip
SHA-256: 4D06EAB777BB135705B5A6816688C9132EFB246093FE606AC488AB6CF65CA5B0
```

## Limite atual

O pacote contém wrappers `prospero-*`, bibliotecas e headers, além de
`prospero-lld.exe`/Ninja para Windows. O wrapper `prospero-clang.cmd` ainda
procura um host Clang/LLVM separado; portanto o download não é tratado como
uma compilação validada.

O template recomenda um bootstrap que baixa e compila Mesa/RADV e outras
dependências. Esse bootstrap pesado ainda não foi executado. A próxima etapa
é preparar o ambiente host e validar um exemplo mínimo, sem envolver ainda o
runtime completo do jogo.

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
