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
