# Meta 5: esqueleto e bind pose na tela

**Branch:** `feat/skeleton-debug` · **Tamanho:** pequeno · **Depende de:** nada

## Objetivo

- **Esqueleto** (tecla E do protótipo): desenhar uma linha de cada osso até o
  pai, por cima da malha.
- **Bind pose** (tecla B): congelar a instância na pose de repouso para
  conferir o rig.

## Situação atual

- `DebugLineRenderer` (`src/renderer/DebugLineRenderer.hpp`) já existe, com
  `addLine`, `addBox`, `clearLines` e `upload`, acessível por
  `Engine::debugLineRenderer()`. O `PlayerController.cpp:593-608` mostra o
  padrão de uso.
- `updateSkinning` já calcula os `globals` (espaço do modelo) de todos os nós a
  cada frame.
- `SkinRig::pose(-1, ...)` já devolve a pose de repouso.

## Passos

### Esqueleto

1. `ModelRenderer` ganha `bool m_showSkeleton` e
   `void collectSkeletonLines(DebugLineRenderer&) const`.
2. Em `updateSkinning`, quando `m_showSkeleton` estiver ligado, guardar por
   instância as posições de mundo dos ossos da skin:
   `inst.transform * globals[nó][3]`. Só os nós que estão em `skin.joints`,
   para não desenhar a raiz da cena.
3. `collectSkeletonLines`: para cada osso com pai também na skin, uma linha
   pai → filho. Cor por profundidade na hierarquia (ou a `jointColor` da meta
   4, para casar as duas visualizações). Juntas como uma `addBox` pequena.
4. Chamar `collectSkeletonLines` onde as linhas de depuração já são montadas
   no frame (procurar quem chama `clearLines`/`upload`), e desenhar **depois**
   do G-buffer, sem teste de profundidade, para o esqueleto aparecer através
   da malha. Conferir se o pipeline do `DebugLineRenderer` permite isso; se
   não, acrescentar a variante sem profundidade.
5. Sem custo quando desligado: nenhum vetor é preenchido.

### Bind pose

6. `SkinnedInstance` ganha `bool bindPose`. Em `updateSkinning`, se ligado,
   usar `rig.pose(-1, 0, globals)` e não avançar o tempo, para que a animação
   continue de onde parou ao desligar.

### Acesso

7. Editor: checkboxes "Skeleton" e "Bind pose" no painel de animação
   (`EditorModelViewer.cpp:154`) e no menu de depuração do mapa.
8. Script: `models.debug(id, { skeleton = true, bindPose = true })`, com o tipo
   atualizado nos dois `eruption.d.luau`.
9. Strings em `data/lang/*.json`.

## Teste

- Rig procedural de 2 ossos: 1 linha e 2 juntas nas posições esperadas
  (teste unitário de `collectSkeletonLines` com um `DebugLineRenderer` falso,
  ou só conferindo os vetores de posição).
- Bind pose: captura com o modo ligado igual à captura com `models.play(id, "")`
  (pose de repouso), com PSNR ≥ 60 dB.
- Validação sem erros, ligando e desligando com o jogo rodando.

## Critério de pronto

Esqueleto visível e alinhado à malha durante a animação, bind pose congelando
e retomando sem salto, e nada desenhado nem calculado com os modos desligados.
