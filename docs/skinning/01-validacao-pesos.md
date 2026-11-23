# Meta 1: validar pesos e ossos no `skin.comp`

**Branch:** `fix/skin-weight-validation` · **Tamanho:** pequeno · **Depende de:** meta 6 (para medir)

## Objetivo

Tornar o compute shader de skinning robusto a dados ruins de glTF, como faz o
`skinning.comp` do protótipo:

- índice de osso ≥ número de ossos da skin, que hoje lê a matriz de **outra**
  instância no anel, ou fora dele;
- peso negativo, NaN ou infinito;
- soma dos pesos diferente de 1 (exportadores que não normalizam);
- vértice sem nenhuma influência válida, que hoje vira a matriz zero e colapsa
  o vértice na origem.

## Situação atual

`shaders/compute/skin.comp:52-60` soma `joints[jointBase + j] * w` para os 4
ossos sem nenhuma verificação. O `jointCount` já chega no push constant, mas só
é usado para decidir se há ossos.

## Passos

1. **Shader.** Em `skin.comp`, antes da mistura:
   - descompactar os 4 índices (`j01 & 0xFFFF`, `j01 >> 16`, ...) e os 4 pesos;
   - zerar o peso quando `j >= p.jointCount`, `!(w > 0.0)` (isso já pega NaN) ou
     `isinf(w)`;
   - `wSum = w0+w1+w2+w3`; se `wSum < 1e-6`, manter a pose de bind (`sp = pos`,
     `sn = nrm`), que é o mesmo critério do protótipo;
   - senão, dividir cada peso por `wSum` e só então misturar as matrizes.
2. **Carga (CPU), primeira linha de defesa.** Em
   `ModelRenderer::registerSkinBase` (`ModelSkinning.cpp:37-121`), onde o
   `SkinVertexGpu` é montado, contar os vértices com problema e registrar um
   `ERUPTION_LOG_WARN` por malha ("N vértices com peso inválido em <asset>"). Não
   corrigir na CPU: o shader é a garantia, e o aviso serve para o artista.
3. **Comentário** no topo do shader explicando o porquê, com referência à
   equação de LBS e ao caso que motivou a mudança.

## Teste

- **Unitário (sem GPU)**, em `tests/test_skin_weights.cpp`, registrado em
  `tests/run_tests.sh` (`run_src`). Ele reproduz em C++ a mesma regra do
  shader (uma função `sanitizeWeights` num header compartilhado, por exemplo
  `src/renderer/SkinWeights.hpp`, que serve também ao aviso da carga) e cobre:
  índice fora da faixa, NaN, soma 0,5, soma 0 e o caso normal, que deve sair
  inalterado.
- **Imagem.** Num mapa com personagem animado e todos os `ERUPTION_TEST_*` de
  determinismo ligados, a captura antes e depois deve dar PSNR ≥ 60 dB. Com
  pesos bons, a renormalização não muda nada visível.
- **Desempenho.** Pela meta 6, `skinning_ms` antes e depois, na mesma cena. A
  expectativa é ficar dentro do ruído, porque o custo é limitado pela banda de
  memória e não pela aritmética.

## Riscos

- Divisão por `wSum` muda bits em vértices com soma 0,9999. Isso é aceitável
  dentro do critério de PSNR, mas vale anotar no PR.
- Malhas que **dependem** do bug (peso 0 em tudo, apoiadas na origem) passam a
  aparecer na pose de bind. Isso é o comportamento correto.

## Critério de pronto

Testes unitários passando, PSNR ≥ 60 dB, custo dentro do ruído e aviso de
carga funcionando com um glTF montado à mão com índice inválido.
