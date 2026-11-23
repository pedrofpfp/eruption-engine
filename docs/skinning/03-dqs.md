# Meta 3: Dual Quaternion Skinning (DQS)

**Branch:** `feat/skinning-dqs` · **Tamanho:** médio · **Depende de:** metas 6 e 1

## Objetivo

Oferecer DQS como alternativa ao LBS, escolhida por instância, sem segundo
pipeline e sem aumentar o anel de ossos. O DQS mistura transformações
rígidas, então não colapsa o volume em torções.

Referência: Kavan et al. 2008. Implementação de referência:
`skinning_dqs.comp` e `anim::paletteToDualQuats` do protótipo.

## Decisões de projeto

| Decisão | Escolha | Motivo |
|---|---|---|
| Um ou dois pipelines | **Um**: `skin.comp` com ramo uniforme por `skinMode` no push constant | Ramo uniforme não diverge; evita duplicar layout, pool e descriptor sets |
| Onde guardar o quaternion duplo | **No mesmo `mat4` do anel**: coluna 0 = `q0`, coluna 1 = `qe`, coluna 2 = `(escala, 0, 0, 0)` | O anel, o `jointBase` e o flush não mudam |
| Escala | Separar a escala **uniforme** e aplicá-la antes da rotação | O protótipo descarta a escala; na engine isso quebra modelos com escala na bind inversa (exportados em cm) |
| Escala não uniforme ou espelhamento (det < 0) | **Cair para LBS** naquele frame, com aviso uma vez por asset | O DQS não representa isso; DQS com escala não uniforme completo seria outra meta |
| Granularidade | Por instância (`SkinnedInstance::skinMode`) | O mesmo asset pode ter personagens de destaque com DQS e multidão com LBS |

## Passos

### CPU

1. `SkinRig::jointDualQuats(int skin, const std::vector<glm::mat4>& globals,
   glm::mat4* out, bool& rigid) const` em `SkinRig.cpp`, ao lado de
   `jointMatrices`:
   - `S = global * inverseBind`;
   - comprimento das 3 colunas de `mat3(S)`; se diferirem mais de 1e-3
     relativo, ou se `determinant < 0`, faz `rigid = false`;
   - `R = mat3(S) / s`, `q0 = normalize(quat_cast(R))`, `t = S[3]`,
     `qe = 0.5 * quat(0, t) * q0`;
   - **alinhar o sinal** de `q0` e `qe` (`q0.w < 0` inverte os dois) para
     reduzir trabalho no shader. O shader ainda alinha ao pivô, porque a
     antipodalidade é entre ossos.
2. `SkinnedInstance`: `uint8_t skinMode = 0` (0 = LBS, 1 = DQS).
3. `updateSkinning` (`ModelSkinning.cpp:543`): se `skinMode == 1`, chamar
   `jointDualQuats`; se `!rigid`, chamar `jointMatrices` e mandar `mode = 0`
   naquele job. O `Job` ganha `mode`.
4. `SkinPush` ganha `uint32_t mode` (40 bytes, dentro do limite de 128).

### Shader (`skin.comp`)

5. Depois da validação da meta 1, `if (p.mode == 1u)`:
   - para cada osso válido: `q0 = joints[k][0]`, `qe = joints[k][1]`,
     `s = joints[k][2].x`;
   - pivô = primeiro `q0` válido; sinal `dot(q0, pivô) < 0 ? -1 : 1`;
   - `b0 += w·sinal·q0`, `be += w·sinal·qe`, `sc += w·s`;
   - `|b0|² < 1e-12` mantém a pose de bind; senão normaliza pelos dois;
   - `t = 2·(c0.w·ce.xyz − ce.w·c0.xyz + cross(c0.xyz, ce.xyz))`;
   - `sp = rot(c0, sc·pos) + t`, `sn = rot(c0, nrm)`.
   O código vem quase igual do `skinning_dqs.comp` do protótipo; só mudam a
   leitura (`uint`/16 bits) e a escala.
6. Morph targets continuam **antes** dos ossos, sem mudança. A posição anterior
   (`keepPrev`) também continua igual.

### API

7. Script: `models.skinning(id [, "lbs"|"dqs"]) -> string` em
   `src/script/ScriptApi.cpp`, ao lado de `models.play` (linha ~1286). Atualizar
   as definições de tipo em `assets/scripts/eruption.d.luau` e
   `projects/parana_field/assets/scripts/eruption.d.luau`.
8. `ModelRenderer::setSkinningMode(uint32_t instance, uint8_t mode)` e um
   equivalente para o preview (`setPreviewSkinMode(int id, ...)`).
9. Editor: um combo "LBS / DQS" no painel "Animation" de
   `EditorModelViewer.cpp:154`, com as strings traduzidas em
   `data/lang/pt-BR.json` (e nos outros idiomas).
10. Troca de modo: como a posição anterior muda de repente, zerar o motion blur
    daquela instância por 1 frame (copiar a saída atual para `prevOut`). Ver
    como `needsInit` já faz isso.

## Testes

- **Unitário** `tests/test_dual_quat.cpp`, sem GPU:
  - `jointDualQuats` de uma matriz rígida, reaplicada a pontos, reproduz a
    matriz com erro < 1e-5;
  - escala uniforme 0,01 (modelo em cm) é preservada;
  - escala não uniforme e espelhamento dão `rigid = false`;
  - mistura de 2 ossos com torção de 180°: o raio do anel de vértices no meio
    fica ≈ r com DQS e ≈ 0 com LBS (a equação de colapso do relatório).
- **Imagem, LBS sem mudança.** Mapa de demonstração com todas as instâncias em
  LBS: PSNR ≥ 60 dB contra o `main`.
- **Imagem, DQS.** Rig procedural do cilindro torcido (portado de
  `ProceduralModels.cpp` do protótipo, código próprio) em LBS e em DQS, com
  captura de cada um. O DQS deve manter o raio.
- **Validação.** Sem `Validation Error`, alternando o modo em tempo real.
- **Desempenho.** `skinning_ms` (meta 6) com 12 instâncias (~30 mil vértices)
  em LBS e em DQS. A meta é DQS ≤ 1,2 × LBS. O protótipo mediu 0,11 ms no total
  numa GPU integrada.

## Riscos

- **Inverse bind com escala do exportador**: é o caso mais comum de dar errado.
  Testar com um asset exportado em cm.
- **Rigs com escala animada** (squash and stretch) caem para LBS: documentar.
- **Mobile**: ramo e ALU a mais no compute. Medir com o preset `mobile_low` e,
  se passar do limite, deixar o DQS desligável pelo preset
  (`data/graphics.json`, chave `skinning_dqs_allowed`).

## Critério de pronto

Alternar LBS/DQS por script e pelo editor; cilindro torcido sem colapso em DQS;
LBS pixel-idêntico; custo dentro da meta; testes unitários passando.
