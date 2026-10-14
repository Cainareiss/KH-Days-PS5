# Fluxo de dados local

Os arquivos do jogo não fazem parte deste repositório. Para testar o front-end,
use uma ROM fornecida pelo próprio usuário e mantenha a saída fora do GitHub.

## 1. Verificar a ROM

No diretório do `khdays-port`:

```powershell
py .\tools\verify_rom\verify_rom.py "E:\path\game.nds" --json --print-entry
```

Se a revisão for válida, mas ainda não estiver cadastrada na base local, o
verificador imprime o SHA-256 candidato. A extração de uma revisão não
cadastrada pode ser feita explicitamente com `--allow-unknown`; não se deve
registrar hashes sem confirmar a origem da imagem.

## 2. Extrair e descompactar

```powershell
py .\tools\extract_data\extract_data.py "E:\path\game.nds" --allow-unknown
py .\tools\decompress_data\decompress_data.py `
  ".\data\extracted\<HASH16>"
py .\tools\unpack_containers\unpack_containers.py `
  ".\data\extracted\<HASH16>\decompressed" `
  --path "UI/" --path "ttl/"
```

Esses comandos geram apenas dados locais em `data/`, que é ignorado pelo
`.gitignore`.

## 3. Validar o front-end

O verificador do repositório PS5 aceita a pasta `nitrofs` da extração:

```powershell
.\tools\check_frontend_data.ps1 `
  -Root "E:\path\khdays-port\data\extracted\<HASH16>\nitrofs" `
  -Language en
```

Repita com `-Language fr` quando a imagem possuir francês. Um resultado com
`Required missing: 0` significa que os recursos mínimos de logos, abertura,
fontes, título, menus, save e áudio estão presentes; não significa ainda que o
backend PS5 esteja compilado.

## Resultado desta imagem

A revisão USA `YKGE` fornecida localmente passou a validação do front-end para
`en` e `fr`. A imagem permanece fora do repositório público.

