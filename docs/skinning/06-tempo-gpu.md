# Meta 6: tempo de GPU do skinning

**Branch:** `feat/skinning-timestamp` · **Tamanho:** pequeno · **Depende de:** nada

## Objetivo

Medir o custo de GPU dos dispatches de skinning e mostrá-lo no Profiler e na
telemetria (`ERUPTION_TELEMETRY_OUT`). Sem essa medição, as metas 1 e 3 não
conseguem provar o custo, como o `AGENTS.md` exige.

## Situação atual

- O `VulkanContext` tem um pool de 64 timestamps por frame
  (`TIMESTAMP_QUERY_COUNT`) e lê os resultados com `getTimestampMs(begin, end)`.
- O mapa de ocupação em `src/renderer/PostProcessor.cpp:29` está desatualizado
  (é de 2026-09-02). Ele marca 18-19 e 62-63 como livres, mas um grep mostra
  `Render.cpp:2110/2113` usando 18-19 e `WaterReflection.cpp:184/220` usando 62-63.
- `ModelRenderer::updateSkinning(cmd, dt)` grava todos os dispatches num bloco
  só, entre duas barreiras (`ModelSkinning.cpp:582-619`).

## Passos

1. **Levantar os slots ocupados.** Fazer grep de `writeTimestamp(` e das
   constantes `kQ*` em `src/` e anotar os índices usados.
2. **Reservar dois slots.** Se não houver livres, subir
   `TIMESTAMP_QUERY_COUNT` de 64 para 66 em `VulkanContext.hpp:266`. O pool é
   recriado com o novo tamanho, e o reset e a leitura já usam a constante.
3. **Atualizar o mapa de ocupação** em `PostProcessor.cpp`, com a data e a
   nova faixa `64-65 ModelRenderer (skinning)`.
4. **Gravar os timestamps** em `updateSkinning`: `kQSkinBegin` depois da
   barreira `before` e `kQSkinEnd` depois da barreira `after`, com
   `VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT`, como no resto da engine. Quando
   `jobs.empty()`, não gravar nada. O leitor deve tratar a falta de valor
   como 0 (ver como os outros passes opcionais fazem, por exemplo VFX 12-13).
5. **Expor no Profiler e na telemetria.** Procurar onde `getTimestampMs` é
   lido para os outros passes (por exemplo "VFX sim") e acrescentar a linha
   "Skinning".
6. **Contadores ao lado do tempo.** Vértices deformados e ossos enviados no
   frame (`cursor`) entram na mesma linha. Assim o custo por vértice
   (ns/vértice) sai direto da telemetria.

## Verificação

- Build sem warnings (desktop e NDK).
- Validação sem erros, inclusive num mapa **sem** modelos com skin (o caminho
  de `jobs.empty()`).
- Num mapa com personagens animados, o Profiler mostra o passe "Skinning".
- A soma dos passes no Profiler continua batendo com o total. O aviso de
  divergência acima de 10% não deve disparar.

## Critério de pronto

A telemetria tem `skinning_ms`, `skin_vertices` e `skin_joints` por frame, e o
custo do LBS atual está anotado no PR. Essa é a linha de base das metas 1 e 3.
