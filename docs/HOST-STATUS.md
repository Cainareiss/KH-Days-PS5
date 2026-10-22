# Diagnóstico inicial do host

Executado em 13/10/2026 no workspace local do desenvolvedor.

| Componente | Resultado |
|---|---|
| CMake | encontrado (`C:\Program Files\CMake\bin\cmake.exe`) |
| Git | encontrado (`C:\Program Files\Git\cmd\git.exe`) |
| Clang/Clang++ | não encontrado no `PATH` |
| `prospero-clang` | não encontrado no `PATH` |
| `make` | não encontrado no `PATH` |
| WSL | não encontrado no `PATH` |
| PS5 SDK no workspace | não encontrado |

Conclusão: o host consegue continuar a auditoria e o desenvolvimento PC, mas
ainda não consegue produzir um ELF/FSELF/PKG PS5. O próximo bloqueio técnico é
preparar, em ambiente Linux ou WSL, a versão fixada do `ps5-payload-dev/sdk`
junto da pilha Vulkan escolhida. A instalação deve ser feita somente a partir
dos repositórios públicos dos mantenedores, nunca de executáveis aleatórios.

