# Meta 2: interpolação CUBICSPLINE

**Branch:** `feat/anim-cubicspline` · **Tamanho:** pequeno · **Depende de:** nada (só CPU)

## Objetivo

Amostrar canais glTF `CUBICSPLINE` com a spline de Hermite que a especificação
define, em vez de reduzi-los a LINEAR. O protótipo já faz isso
(`Animation.cpp:85`).

## Situação atual

- `GltfParser.cpp:1790-1793` lê o trio (tangente de entrada, valor, tangente de
  saída) e **guarda só o valor**. O mesmo acontece com os pesos de morph em
  `GltfParser.cpp:1774-1781`.
- `SkinRig::Channel` tem só `bool step`; o resto é tratado como LINEAR.
- `sample()` em `SkinRig.cpp:10-27` faz `mix` e `slerp`.

## Fórmula (glTF 2.0, apêndice C)

Com `td = t[k+1] - t[k]`, `s = (t - t[k]) / td`, valor `v`, tangente de saída
`b[k]` e tangente de entrada `a[k+1]`:

```
p(s) = (2s³ - 3s² + 1)·v[k] + (s³ - 2s² + s)·td·b[k]
     + (-2s³ + 3s²)·v[k+1]  + (s³ - s²)·td·a[k+1]
```

Rotação: aplicar a mesma fórmula nos 4 componentes e **normalizar** o
quaternion. A especificação manda fazer assim, e não usar slerp.

## Passos

1. **Dados.** Em `SkinRig::Channel`, trocar `bool step` por
   `enum class Interp : uint8_t { Linear, Step, Cubic } interp`, mantendo
   `step` como função de conveniência se houver outros usos (fazer grep), e
   acrescentar `std::vector<Vec4> inTan, outTan` (vazios fora da cúbica), mais
   `std::vector<float> weightsIn, weightsOut` para morph.
2. **Parser.** Em `GltfParser.cpp`, no caso `CUBICSPLINE`, separar o trio em
   `inTan`, `values` e `outTan`, e fazer o mesmo para os pesos de morph. Se o
   número de elementos não for `3 × chaves`, registrar o aviso e cair para
   LINEAR com os valores do meio, que é o comportamento de hoje.
3. **Amostragem.** Em `sample()`, um ramo `Cubic` com a fórmula acima.
   Rotação: Hermite nos 4 componentes, depois `glm::normalize`. O mesmo vale
   para `morphWeights()`.
4. **Bordas.** Antes da primeira chave e depois da última, continuar
   devolvendo o valor da ponta (sem extrapolar), como já é feito hoje.

## Teste

`tests/test_skinrig_sample.cpp` (sem GPU, em `run_tests.sh`), montando um
`SkinRig` à mão:

- canal cúbico com tangentes zero: deve dar `smoothstep` entre as chaves;
- canal cúbico com tangentes iguais à inclinação da reta: deve coincidir com
  LINEAR a menos de 1e-5;
- rotação cúbica: resultado sempre unitário;
- tempos exatos das chaves: devolve o valor da chave;
- comparação com valores de referência tirados do protótipo
  (`anim::sampleClip`) para 3 tempos.

## Verificação visual

Exportar do Blender uma animação curta com interpolação Bezier (o exportador
gera CUBICSPLINE), carregar no visualizador do editor e comparar com o
protótipo nos mesmos tempos. O asset fica fora do repositório.

## Critério de pronto

Testes passando. Mapas que não usam CUBICSPLINE ficam pixel-idênticos. A
memória a mais (2 vetores por canal cúbico) aparece só em assets cúbicos.
