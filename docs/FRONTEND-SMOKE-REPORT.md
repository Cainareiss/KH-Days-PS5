# Smoke test do front-end

Resultado obtido com uma cópia local do jogo fornecida pelo usuário. Nenhum
asset ou ROM foi copiado para este repositório.

## Entrada

- revisão: USA, código `YKGE`;
- idioma disponível: inglês e francês;
- tamanho da imagem: 268.435.456 bytes;
- extração: 1.713 arquivos NitroFS.

## Processamento

- descompressão Nitro: 958 arquivos concluída;
- desempacotamento de containers de UI/abertura: 486 entradas, zero falhas;
- inventário: 162 arquivos reconhecidos pelo analisador, 157 candidatos LZ;
- SDAT encontrado: `snd/sound_data.sdat`;
- fontes encontradas: `font_eu_08.nftr` e `font_eu_10all.nftr`;
- movie de abertura encontrado: `mv/802.mods`.

## Validação das cenas

| Cena | Inglês | Francês |
|---|---:|---:|
| Logos | OK | OK |
| Abertura | OK | OK |
| Título | OK | OK |
| Menu/seleção | OK | OK |
| Arquivo | OK | OK |

O `ttl_en.p2` não existe na revisão inglesa porque o jogo usa o `ttl.p2`
base; isso é esperado e foi marcado como opcional no manifesto.

## Pendências não relacionadas ao front-end

Três arquivos `_unnamed` apresentaram cabeçalhos LZ10 inconsistentes durante a
descompressão. Eles não são necessários para as cenas acima. A compilação do
runtime PC também não foi concluída por falta de espaço no volume E:, não por
erro de código; a pasta de build temporária foi removida e os dados extraídos
foram preservados.

