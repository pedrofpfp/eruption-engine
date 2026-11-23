# Skinning: metas

Este diretório reúne as metas de trazer para a engine o que foi feito no
protótipo da Entrega 5 ("Skeletal Animation e Skinning", Vulkan/C++17) e um
plano de implementação para cada uma.

## Ponto de partida

A engine já tem skinning por esqueleto completo, e nada dele será reescrito:

| O que já existe | Onde |
|---|---|
| LBS na GPU (compute), com morph targets e posição do frame anterior | `shaders/compute/skin.comp` |
| Amostragem de keyframes, slerp, FK e paleta `global * bind inversa` | `src/renderer/SkinRig.cpp` |
| Transição entre animações (smoothstep) | `ModelRenderer::playAnimation`, `updateSkinning` |
| Vários personagens num anel de matrizes por frame | `m_jointRing`, `kJointsPerFrame = 16384` |
| Limites por osso a cada frame | `updateSkinning` (`jointRadius`) |
| Leitura do glTF (skins, animações) | `src/formats/GltfParser.cpp` |
| Script `models.play` e painel de animação no editor | `src/script/ScriptApi.cpp`, `src/editor/EditorModelViewer.cpp` |

O protótipo acrescenta o que a engine **não** tem: DQS, a interpolação
CUBICSPLINE de verdade, validação de pesos no shader, visualização de pesos,
desenho do esqueleto e medição do tempo de GPU do skinning.

## Metas

| # | Meta | Por quê | Tamanho | Plano |
|---|---|---|---|---|
| 1 | Validar pesos e ossos no `skin.comp` | Índice de osso inválido lê fora do anel; peso NaN ou soma ≠ 1 deforma a malha | P | [01-validacao-pesos.md](01-validacao-pesos.md) |
| 2 | Interpolação CUBICSPLINE | Hoje vira LINEAR: as tangentes são descartadas e a curva do animador se perde | P | [02-cubicspline.md](02-cubicspline.md) |
| 3 | Dual Quaternion Skinning | O LBS colapsa o volume em torções (cotovelo, pulso, ombro) | M | [03-dqs.md](03-dqs.md) |
| 4 | Visualização dos pesos | Depurar rigs importados: ver a influência de cada osso | P | [04-visualizacao-pesos.md](04-visualizacao-pesos.md) |
| 5 | Esqueleto e bind pose na tela | Ver os ossos e a pose de repouso sem sair da engine | P | [05-esqueleto-bind-pose.md](05-esqueleto-bind-pose.md) |
| 6 | Tempo de GPU do skinning | O skinning não aparece no Profiler; é preciso medir antes e depois | P | [06-tempo-gpu.md](06-tempo-gpu.md) |

P = pequeno (1 PR curto), M = médio.

## Ordem

```
6 (medir) ──► 1 (robustez) ──► 2 (cúbica) ──► 3 (DQS) ──► 4 e 5 (depuração)
```

- **6 primeiro**: o `AGENTS.md` exige medir antes e depois; sem o timestamp
  do skinning, as metas 1 e 3 não têm como provar que não pioraram o frame.
- **1 antes de 3**: o DQS herda a mesma validação de pesos.
- **2 é independente** (só CPU) e pode ir em paralelo com 1.
- **4 e 5** são ferramentas de depuração; ajudam a validar o DQS visualmente,
  mas não bloqueiam.

Cada meta é um PR separado para `eruptionlabs/eruption-engine`, saindo de uma
branch própria deste fork.

## Regras que valem para todas (resumo do `AGENTS.md`)

- Zero warnings no GCC e no Clang do NDK (`tools/android/build_apk.sh`).
- Camada de validação do Vulkan sem nenhum `Validation Error`.
- Toda mudança de renderização: tempo de frame (telemetria) antes e depois e
  imagem comparada por PSNR. Mudança que deve ser invisível fica acima de 60 dB.
- Push constants até 128 bytes (o `SkinPush` hoje tem 36).
- Sem assets de terceiros no repositório. CesiumMan e Fox (CC-BY) servem só
  para teste local; os testes versionados usam um rig procedural (o cilindro
  com dois ossos do protótipo, `ProceduralModels.cpp`, é código próprio e
  pode ser portado).
- Código no estilo da engine: `namespace eruption`, membros `m_nome`,
  `ERUPTION_LOG_*`, comentários em português explicando o porquê.
- Commits no formato `Subsistema: descrição curta`, com a medição no corpo.

## Critério de pronto do conjunto

- Um personagem com torção de 180° no pulso mantém o volume com DQS e mostra
  o colapso com LBS, alternando em tempo real pelo editor e por script.
- Uma animação exportada com CUBICSPLINE bate com a referência (o protótipo)
  nos mesmos tempos.
- O Profiler mostra o tempo de GPU do skinning; com 12 personagens (~30 mil
  vértices) o DQS custa no máximo 20% a mais que o LBS.
- Nenhuma imagem muda quando nenhum recurso novo está ligado (PSNR ≥ 60 dB).
