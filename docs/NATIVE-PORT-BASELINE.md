# Base do port nativo para PS5

O trabalho do jogo real parte de `khdays-port` 0.1.1, commit
`a7ee5a2d6c423ad69df7405a30f63e858f8e2acc`, com `khdays-decomp` fixado no
commit `a2b0478b87e3102732094c34f6ca89595f98ab93`. A versão e a ROM suportada
estão registradas em `native/upstream-lock.json`.

O núcleo anterior deste repositório é um protótipo útil da plataforma, mas sua
lógica de combate e missão não é o jogo decompilado. O novo caminho reaproveita
dele o backend Vulkan/VideoOut, áudio, empacotamento e diagnóstico, enquanto o
gameplay passa a vir do `khdays-port`.

## Primeiro componente portado

`native/ps5/context_ps5.c` e `context_ps5_x64.S` implementam o contrato de
threads cooperativas do runtime. O backend Windows original altera também o
estado TEB do Windows; no PS5, o código usa o ABI System V x86-64 e preserva os
registradores não voláteis antes de trocar a pilha.

Compile e valide o arquivo Prospero com:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/prepare_native_source.ps1
python scripts/prepare_native_bounded.py `
  --source .cache/khdays-native-v0.1.1 `
  --out .cache/khdays-native-v0.1.1/build/ps5-gen --jobs 1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_native_context.ps1
```

O resultado fica em `build-native-context-ps5/`, incluindo
`validation.json`. O teste upstream de 200 contextos é compilado como contrato,
mas sua execução depende de um runner no PS5. A mesma compilação valida
`memory_ps5.c`: a janela virtual de 2 GiB, os espelhos da RAM principal, as
regiões do DS e os heaps anônimos de alocação tardia.

`files_ps5.c` mantém o save inteiro ao gravar primeiro um arquivo temporário,
sincronizá-lo e renomeá-lo sobre o anterior. `clock_ps5.c` usa o relógio
monotônico e conserva as pausas e o despertar antecipado esperados pelo jogo.

## Dependência de dados

A versão 0.1.1 aceita a ROM europeia `YKGP`, SHA-256
`1ecf5e7a41a2ae48e7c5fd3f678e5a79e854d843f1a1fa3a952a41b14e51ec4f`.
A ROM americana `YKGE` encontrada no PC não pode gerar os dados desta revisão.
Nenhuma ROM ou dado extraído entra no Git.

Quando a cópia europeia estiver disponível, a verificação e extração ficam em
um único comando. O script recusa qualquer hash diferente do suportado:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts/prepare_native_game_data.ps1 -RomPath "E:\caminho\days-europe.nds"
```

Para uma revisão YKGP não registrada, como a cópia M5 fornecida nesta sessão,
é possível fazer somente uma extração de desenvolvimento explícita:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts/prepare_native_game_data.ps1 -AllowUnknown `
  -RomPath "E:\caminho\days-europe-m5.nds"
```

Esse modo não altera o hash canônico nem autoriza afirmar compatibilidade do
runtime decompilado.

## Próximas substituições

1. validar em hardware o mapa de memória e a troca de contexto;
2. host SDL3/GPU para o backend Vulkan/VideoOut já existente;
3. áudio, input e vídeos;
4. execução da rota de regressão até a primeira missão e round-trip do save.

## Auditoria de portabilidade da versão 0.1.1

- **Já substituído e compilado para Prospero:** contexto cooperativo, relógio,
  arquivos/save atômico, hora local e mapa virtual da memória do DS.
- **Reutilizável:** `host/net/net_posix.c`; não é necessário transportar o
  backend Winsock para o console.
- **Ainda precisa de backend PS5:** apresentação SDL/GPU, input, saída de áudio,
  threads/temporização usadas pelos decodificadores e vídeo HD.
- **Remover ou isolar no console:** atualizador WinHTTP, Media Foundation,
  diagnóstico DbgHelp e integração de instância do Windows.

Essa divisão evita misturar o protótipo anterior com o gameplay real: cada
substituição deve satisfazer o contrato do `khdays-port` e compilar junto aos
fontes decompilados antes de entrar no título de teste.
