# Release pública

Este repositório é público para compartilhar o código do runtime, os
adaptadores PS5, os scripts e os testes. A release não contém o jogo.

## O que está incluído

- código original/adaptador em `core/`, `platform/`, `native/` e `scripts/`;
- testes locais e fixtures em `tests/`;
- documentação de build, auditoria e diagnóstico em `docs/`;
- o lockfile que identifica as revisões públicas usadas no build.

## O que é deliberadamente excluído

Não são publicados ROMs `.nds`, dumps, árvores `data/` ou `extracted/`,
assets comerciais, `eboot.bin`, FSELF/PRX, imagens `.exfat`/`.pkg` nem o
pacote instalável. Esses arquivos contêm dados do jogo e devem permanecer na
máquina do usuário. O `.gitignore` bloqueia esses formatos e diretórios; a
release é um pacote de fontes, não uma cópia redistribuível de Kingdom Hearts.

## Estado da build

Os testes locais do backend Vulkan e dos contratos de frame passam no ambiente
de desenvolvimento documentado em `STATUS.md`. Isso não substitui a execução
no hardware: VideoOut, áudio, carregamento de dados e a entrada da primeira
missão continuam pendentes de confirmação no PS5.

## Reprodução local

1. Clone o repositório e instale as revisões públicas indicadas em
   `native/upstream-lock.json`.
2. Configure o SDK/LLVM e o template PS5 somente em diretórios locais; os
   caminhos de exemplo nos scripts são parâmetros, não dependências da
   release.
3. Forneça a sua própria extração compatível e valide o hash antes de compilar.
4. Execute os testes locais antes de montar qualquer imagem para o seu console.

Os comandos de empacotamento recusam ELF sem as auditorias correspondentes.
Uma imagem de teste só deve ser criada para uso privado e legal no console do
desenvolvedor.

## Licenças e atribuições

Os componentes derivados do port upstream permanecem sob a licença indicada
em `native/UPSTREAM-LICENSE`. Dependências externas mantêm suas próprias
licenças e avisos. A publicação do código não concede direitos sobre Kingdom
Hearts, Nintendo, Square Enix, Disney ou qualquer asset do jogo.
