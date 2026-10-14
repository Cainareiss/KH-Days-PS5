# Base completa de dados

O PS5 não recebe mais apenas os arquivos da primeira missão. O script
`scripts/stage_full_game_data.ps1` copia a árvore NitroFS extraída do projeto
PC para uma área de trabalho local:

```text
build-full-game-data/data/
└── asset-manifest.json
```

O manifesto registra caminho, tamanho e SHA-256 de cada arquivo. A extração
atual contém 1.713 arquivos (258.781.400 bytes), incluindo:

- os arquivos de mundo `mi/wd/`;
- as missões `mi/mi/`;
- personagens e modelos `ba/` e `mi/ch/`;
- mensagens `db/`, fontes em `text/`, UI, áudio `snd/` e vídeos `mv/`.

Executar novamente:

```powershell
./scripts/stage_full_game_data.ps1
```

`-CleanDestination` só deve ser usado quando for desejado recriar a área de
trabalho inteira. A saída não é um pacote exFAT e não é copiada para o USB.

## Estado da adaptação

O staging completo está pronto, mas o runtime PS5 ainda precisa decodificar
P2/CAKP/Z, NFTR, NSBMD/NSBTX, SDAT e MODS, além de ligar o catálogo de missões
ao renderer. Portanto, a presença dos 1.713 arquivos não significa que todos
os mundos já sejam jogáveis; o runtime executável continua sendo uma fatia
vertical até essas etapas serem integradas e testadas.
