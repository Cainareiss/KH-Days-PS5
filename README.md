# KH Days PS5 — plano de port do runtime

> **WIP / adaptação em andamento.** Este repositório ainda não contém uma
> build PS5 jogável nem promete compatibilidade com firmware específico.

Consulte [`STATUS.md`](STATUS.md) para o que já foi concluído e o que continua
pendente.

O procedimento para usar dados locais sem publicá-los está em
[`docs/LOCAL-DATA-WORKFLOW.md`](docs/LOCAL-DATA-WORKFLOW.md).

Este diretório é o espaço de trabalho para investigar uma compilação do
`khdays-port` (Kingdom Hearts 358/2 Days) como título homebrew para PS5. O
nome “374 Days” foi tratado como referência a **358/2 Days**.

## Estado real do ponto de partida

- `KH-ReCoded-PTBR/tools/khdays-port` é um runtime nativo experimental para
  PC. Ele já possui pipeline de assets e front-end, mas o gameplay completo
  ainda está em desenvolvimento.
- O runtime de PC usa SDL3, `SDL_Renderer` e `SDL_GPU`; essas chamadas não
  formam, por si só, um executável PS5.
- Este projeto não contém ROM, assets, dumps ou binários proprietários. O
  teste deve usar uma cópia legal fornecida pelo usuário.

## Decisão técnica inicial

O alvo inicial será um título homebrew PS5 usando a pilha pública experimental
`PS5_Vulkan`/`PS5_VulkanTemplate` e o PS5 Payload SDK. O backend será isolado em
`platform/ps5`, sem contaminar o backend PC.

SDL3 tem uma porta oficial para PS5, porém a própria documentação informa que
ela é destinada a desenvolvedores PlayStation sob NDA. Portanto, não vamos
presumir que essa implementação possa ser usada neste ambiente. Se a porta
comunitária SDL2 disponível no ecossistema PS5 atender ao input, janela e áudio,
ela pode ser usada como adaptador; caso contrário, o plano é uma camada mínima
direta para VideoOut, controles, áudio e Vulkan.

## Fases

1. **Host e contrato** — manter o build PC funcionando e definir as interfaces
   neutras de vídeo, input, áudio, tempo e filesystem.
2. **Hello Vulkan PS5** — compilar um executável mínimo com display, swapchain,
   sincronização e controle; validar no ambiente homebrew do usuário.
3. **Front-end** — ligar o recurso neutro do port (logos, título, menus e
   seleção de personagem) ao backend PS5.
4. **Dados do usuário** — apontar para a extração local do próprio jogo, sem
   colocar ROM/assets no repositório; validar fontes, textos, áudio e saves.
5. **Gameplay** — acompanhar o avanço de `khdays-port`/`khdays-decomp` até que
   missões, combate e HUD estejam implementados.
6. **Empacotamento** — somente depois dos testes, gerar o formato de instalação
   apropriado ao ambiente homebrew do proprietário do console.

## Bloqueios atuais

- A toolchain pública, o PS5 Payload SDK e o PS5 Vulkan Template já estão
  instalados localmente, e as bibliotecas de mídia compilam para Prospero.
- O `VulkanExample` final ainda precisa ligar pipelines/descriptors e os draws
  do backend de mídia ao command buffer do swapchain.
- Não há execução automatizada neste workspace contra um PS5 homebrew. Sem o
  teste no console não é possível afirmar que Vulkan/VideoOut ou `sceAudioOut`
  funcionam no hardware.
- O port de origem ainda não implementa o fluxo completo de todas as missões.

## Referências públicas

- [khdays-port](https://github.com/Yokimitsuro/khdays-port)
- [khdays-decomp](https://github.com/Yokimitsuro/khdays-decomp)
- [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan)
- [PS5_VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate)
- [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk)
- [documentação SDL3 para PS5](https://wiki.libsdl.org/SDL3/README-ps5)

