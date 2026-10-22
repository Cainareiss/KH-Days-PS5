# ROM usada na preparação local

O arquivo fornecido para a preparação foi reconhecido como:

- título: `358/2 DAYS`;
- código: `YKGP` (Europa);
- revisão: `0`;
- tamanho: `268435456` bytes;
- SHA-256 do dump: `d9e103c6ccd39d5b91e44d865983badc5e2af1674db404b3768557712038a373`.

O código do jogo é o esperado, mas esse SHA-256 não está no banco canônico do
`khdays-port` 0.1.1 (`1ecf5e7a41a2ae48e7c5fd3f678e5a79e854d843f1a1fa3a952a41b14e51ec4f`).
Por isso a extração foi feita apenas com `--allow-unknown`, para desenvolvimento,
e não deve ser tratada como prova de compatibilidade binária com a revisão do
decompilado.

A extração foi concluída pelo script oficial e catalogou 1.898 arquivos
NitroFS. O resultado local está em `.cache/khdays-native-v0.1.1/data/extracted/`
e não é versionado.
