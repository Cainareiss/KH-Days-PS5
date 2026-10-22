# KH Days PS5 — runtime nativo experimental

> **WIP / adaptação em andamento.** Esta é uma build experimental de
> desenvolvimento. O funcionamento no PS5, a primeira missão e a
> compatibilidade com firmware específico ainda não estão garantidos.

O repositório é público para permitir auditoria e reprodução do trabalho. A
release contém somente o código e os testes. ROMs, dumps, assets, `eboot.bin`,
imagens exFAT e outros dados comerciais não são distribuídos aqui: use apenas
dados que você tenha direito de usar e forneça-os localmente seguindo
[`docs/LOCAL-DATA-WORKFLOW.md`](docs/LOCAL-DATA-WORKFLOW.md).

Consulte [`STATUS.md`](STATUS.md) para o que já foi concluído e o que continua
pendente.

O estado verificado do executável nativo, das bibliotecas e da capa/fundo está
em [`docs/NATIVE-EXECUTABLE-STATUS.md`](docs/NATIVE-EXECUTABLE-STATUS.md).

A retomada sobre o código decompilado real está documentada em
[`docs/NATIVE-PORT-BASELINE.md`](docs/NATIVE-PORT-BASELINE.md).

O resultado da verificação da ROM europeia fornecida está em
[`docs/NATIVE-PORT-ROM-REPORT.md`](docs/NATIVE-PORT-ROM-REPORT.md).

Para reproduzir uma release pública e conferir exatamente o que foi enviado,
veja [`docs/PUBLIC-RELEASE.md`](docs/PUBLIC-RELEASE.md).

O procedimento para usar dados locais sem publicá-los está em
[`docs/LOCAL-DATA-WORKFLOW.md`](docs/LOCAL-DATA-WORKFLOW.md).

Este diretório é o espaço de trabalho para investigar uma compilação do
`khdays-port` (Kingdom Hearts 358/2 Days) como título homebrew para PS5. O
nome “374 Days” foi tratado como referência a **358/2 Days**.

## Estado real do ponto de partida

- O caminho nativo agora usa `khdays-port` 0.1.1 fixado por commit. Essa versão
  executa o jogo inteiro no PC, embora o projeto upstream ainda seja WIP; no
  PS5, a adaptação das interfaces de plataforma continua em andamento.
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
  instalados localmente. O `VulkanExample`, pipelines, descriptors, backend de
  mídia e decodificadores compilam juntos para Prospero (47/47 unidades).
- O link RADV final depende da revisão do Payload SDK fixada pelo template. A
  instalação dessa revisão precisa de NTFS/WSL porque o volume exFAT atual não
  consegue materializar os symlinks Unix do SDK.
- Não há execução automatizada neste workspace contra um PS5 homebrew. Sem o
  teste no console não é possível afirmar que Vulkan/VideoOut ou `sceAudioOut`
  funcionam no hardware.
- O port de origem 0.1.1 executa o jogo completo no Windows; o bloqueio atual é
  portar e validar no PS5 as interfaces de host usadas por esse código real.

## Referências públicas

- [khdays-port](https://github.com/Yokimitsuro/khdays-port)
- [khdays-decomp](https://github.com/Yokimitsuro/khdays-decomp)
- [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan)
- [PS5_VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate)
- [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk)
- [documentação SDL3 para PS5](https://wiki.libsdl.org/SDL3/README-ps5)

