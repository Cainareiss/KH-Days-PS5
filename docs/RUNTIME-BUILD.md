# Runtime, ELF e empacotamento

## ELF Prospero

Com o SDK e LLVM configurados:

```powershell
$sdk = "E:\DOCUMENTOS\KH-Days-PS5\toolchains\ps5-payload-sdk-v0.44\ps5-payload-sdk"
$llvm = "E:\DOCUMENTOS\KH-Days-PS5\toolchains\llvm-23.1.3\clang+llvm-23.1.3-x86_64-pc-windows-msvc\bin"
$env:PS5_PAYLOAD_SDK = $sdk
$env:Path = "$llvm;$sdk\win;$env:Path"
cmake -S . -B build-ps5-runtime -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="$sdk\toolchain\prospero.cmake" `
  -DKHDAYS_PS5_BUILD_TESTS=OFF `
  -DKHDAYS_PS5_BUILD_RUNTIME=ON `
  -DKHDAYS_PS5_NATIVE_BACKEND=OFF
cmake --build build-ps5-runtime --target khdays-ps5-runtime -j 4
```

O resultado é `build-ps5-runtime/khdays-days-runtime.elf`, reconhecido pelo
LLVM como ELF64 x86-64/FreeBSD. Esse runtime já executa a fatia determinística
de gameplay: combate, atualização do HUD, conclusão de missão e round-trip do
save. Ele ainda não é o jogo completo nem instancia o renderer Vulkan.

## FSELF/bundle homebrew

```powershell
./scripts/package_ps5_homebrew.ps1
```

O script usa o `make_fself.py` público do SDK e cria `PPSA12345/eboot.bin`, `sce_sys` e
um ZIP de instalação/teste. O resultado é fake-signed para homebrew/debug.

Um PKG retail válido exige ferramentas e assinatura da cadeia oficial da
Sony, que não fazem parte do SDK público. Não é seguro gerar um arquivo com
extensão `.pkg` a partir do ZIP: o PS5 o rejeitaria como corrompido.
