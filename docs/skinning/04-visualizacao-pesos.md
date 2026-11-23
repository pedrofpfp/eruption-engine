# Meta 4: visualização dos pesos de skinning

**Branch:** `feat/skin-weight-view` · **Tamanho:** pequeno · **Depende de:** meta 1

## Objetivo

Pintar cada vértice com a média das cores dos ossos que o influenciam,
ponderada pelos pesos, sobre a malha já deformada. As regiões de transição
aparecem como degradê, e vértice sem peso aparece magenta. É a tecla P do
protótipo (`weights.vert`).

## Por que não portar o shader do protótipo

O protótipo desenha a malha com um pipeline gráfico próprio. A engine é
diferida (G-buffer de 5 anexos), e um pipeline à parte teria que reproduzir
G-buffer, sombra e iluminação. É mais simples gerar a cor **no compute** e
deixar o G-buffer desenhar como sempre.

## Situação atual

- O vértice (`TerrainVertex`) tem `uint32_t color` (RGBA8), lido no
  `model_vert.glsl:700` como `outColor`.
- O compute de skinning escreve no buffer **da instância** (`s.out`), então
  mexer na cor ali não afeta outras instâncias do mesmo asset.

## Passos

1. **Investigar primeiro** como `model_frag.glsl` usa a cor do vértice: se
   multiplica o albedo, se é ignorada com textura, ou se ativa algum caminho
   especial. O resultado define se basta escrever a cor ou se é preciso
   também um modo de depuração sem textura. Anotar a conclusão no PR.
2. **Shader.** No `skin.comp`, com `p.debugView == 1u`:
   - `jointColor(j)` = matiz pela razão áurea (função do protótipo);
   - `c = Σ wᵢ·jointColor(jᵢ)` com os pesos já validados (meta 1); soma 0 dá
     magenta;
   - escrever `packUnorm4x8(vec4(c, 1))` na palavra `colWord` do vértice de
     saída.
   O push constant ganha `debugView` e `colWord`.
3. **Desligar.** Quando o modo for desligado, restaurar a cor original
   copiando de novo o vértice base (`needsInit = true`, que já existe).
4. **Albedo.** Se o passo 1 mostrar que a textura domina a cor, acrescentar um
   bit por instância que força albedo branco no G-buffer. Ver se já existe um
   modo de depuração de albedo no `Render.cpp` antes de criar um novo.
5. **Acesso.**
   - Editor: checkbox "Weights" no painel de animação (`EditorModelViewer.cpp`)
     para o preview, e uma opção global no menu de depuração para as
     instâncias do mapa.
   - Variável `ERUPTION_TEST_SKIN_WEIGHTS=1` para capturas automáticas,
     documentada onde é lida (regra do `AGENTS.md`).

## Teste

- Captura com o modo ligado no rig procedural de 2 ossos: metade de uma cor,
  metade de outra e degradê no meio.
- Desligar e capturar de novo: PSNR ≥ 60 dB contra a captura sem nunca ter
  ligado (prova que a cor foi restaurada).
- Validação sem erros.

## Critério de pronto

Modo ligável pelo editor e pela variável de teste, sem custo quando desligado
(o ramo é uniforme) e sem deixar resíduo ao desligar.
