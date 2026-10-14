# Núcleo e front-end portátil

O diretório `core/` agora contém a primeira camada executável independente de
SDL e das APIs do PS5:

- IDs de cena compatíveis com o runtime PC (`1`, `7`, `11`, `19`, `100` e os
  IDs de gameplay/transição);
- resolução virtual `256x192` e cadência original `59.8261 Hz`;
- seleção de idioma com os mesmos caminhos de recursos do manifesto;
- fluxo BootLogo → Title → SaveFile/MainMenu → Gameplay;
- atalho por START/Confirm e navegação Up/Down/Cancel;
- validação de recursos obrigatórios antes de entrar em cada cena;
- interfaces `ResourceProvider` e `Renderer` para o backend PC ou PS5.

## Validação local

```powershell
cmake -S . -B build-core -G "Visual Studio 17 2022" -A x64
cmake --build build-core --config Release --target khdays-ps5-core-smoke -- /m
ctest --test-dir build-core -C Release --output-on-failure
```

O smoke test percorre as transições do título, salvamento, seleção de
personagem/menu e entrada no gameplay usando um provedor de dados falso. Ele
valida a lógica do núcleo, não a execução no console nem a presença de assets
proprietários.

O mesmo alvo foi configurado com `toolchain/prospero.cmake` e compilado como
`build-ps5-core/libkhdays-ps5-core.a`. O objeto interno foi reconhecido pelo
LLVM como ELF relocatable `x86_64`; essa etapa comprova a compilação cruzada do
núcleo, mas ainda não liga VideoOut/Vulkan nem produz um executável carregável.
