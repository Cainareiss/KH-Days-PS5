# Matriz de portabilidade PS5

## API do port de PC e substituição prevista

| Serviço do PC | Implementação atual | Adaptador PS5 planejado |
|---|---|---|
| Janela/apresentação | SDL3 + `SDL_CreateWindowAndRenderer` | VideoOut + superfície Vulkan (ou SDL comunitário validado) |
| Renderização | `SDL_GPU` com shaders SPIR-V | Vulkan da pilha PS5, mantendo shaders SPIR-V quando compatíveis |
| Input | eventos SDL | leitura de controle PS5 e conversão para eventos neutros |
| Áudio | backend SDL3 | backend de áudio do SDK; SDL somente se a porta tiver suporte verificado |
| Arquivos | filesystem PC | diretórios de dados do título/homebrew, sem ROM embutida |
| Tempo | relógio SDL/host | relógio monotônico do SDK |

## Contrato mínimo de `platform/ps5`

O backend deverá expor, em termos neutros:

```text
init(config) -> status
poll_input() -> eventos
begin_frame() -> frame_target
submit_frame(frame_target)
queue_audio(samples)
read_user_file(path)
shutdown()
```

Nenhuma função do gameplay deve incluir headers do SDK PS5. A conversão fica
confinada ao backend, permitindo continuar testando todos os recursos no PC.

## Critérios do primeiro marco

- título inicializa e encerra sem crash;
- imagem Vulkan é apresentada em resolução conhecida;
- pelo menos um controle é recebido e exibido em um overlay de diagnóstico;
- um arquivo de configuração é lido do diretório de dados;
- não há ROM, chave, SDK proprietário ou asset do jogo no repositório.

## O que não será feito nesta fase

- não modificar executável comercial nem gerar um PKG para distribuição;
- não declarar suporte à SDL3 oficial do PS5 sem acesso autorizado ao SDK;
- não tratar o front-end experimental como jogo concluído;
- não apagar o projeto de tradução de Re:coded: ele fica pausado para retomada.

