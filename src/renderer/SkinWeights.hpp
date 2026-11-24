#pragma once
// Regra de limpeza dos pesos de skinning, em C++ puro (sem Vulkan nem GLM) para
// servir ao aviso da carga (ModelSkinning.cpp) e ao teste sem GPU
// (tests/test_skin_weights.cpp). PRECISA casar com shaders/compute/skin.comp:
// o shader é a garantia em tempo de execução; esta função só reproduz a mesma
// conta para contar os vértices com problema e para o teste provar a regra.
//
// Regra (a mesma do shader, na mesma ordem):
//   1. peso zerado quando o osso não existe na skin (joint >= jointCount), quando
//      não é positivo (!(w > 0) pega também NaN) ou quando é infinito;
//   2. wSum = w0 + w1 + w2 + w3, nessa ordem;
//   3. wSum < 1e-6 ou infinito: sem influência, o vértice fica na pose de bind
//      (todos os pesos saem 0);
//   4. senão cada peso é dividido por wSum (soma 0,5 vira 1; soma 1 exata não
//      muda nenhum bit, porque w / 1 == w).
#include <cmath>
#include <cstdint>

namespace eruption {

struct SkinInfluence {
    uint16_t joint[4];
    float weight[4];
};

// Abaixo disso a soma é tratada como "sem influência" (mesmo critério do
// protótipo da Entrega 5 e de skin.comp).
constexpr float kSkinMinWeightSum = 1e-6f;
// Desvio da soma em relação a 1 que já conta como problema do arquivo. A
// renormalização sempre acontece, mas somas como 0,99999994 são só
// arredondamento do exportador e não merecem aviso.
constexpr float kSkinWeightSumTolerance = 1e-3f;

// Limpa a influência no lugar. Retorna false quando teve que corrigir algo que
// vem do arquivo (peso NaN, infinito ou negativo, osso inexistente com peso
// diferente de zero, soma longe de 1) ou quando o vértice ficou sem nenhuma
// influência. Os índices não são alterados: com peso 0 o shader nem lê a matriz.
inline bool sanitizeSkinWeights(SkinInfluence& s, uint32_t jointCount) {
    bool ok = true;
    for (int k = 0; k < 4; ++k) {
        const float w = s.weight[k];
        const bool valid = s.joint[k] < jointCount && w > 0.0f && !std::isinf(w);
        if (valid) continue;
        // Peso exatamente 0 (ou -0) é o normal para os slots livres e não é erro,
        // mesmo com índice fora da faixa.
        if (!(w == 0.0f)) ok = false;
        s.weight[k] = 0.0f;
    }
    const float wSum = s.weight[0] + s.weight[1] + s.weight[2] + s.weight[3];
    if (wSum < kSkinMinWeightSum || std::isinf(wSum)) {
        for (float& w : s.weight) w = 0.0f;
        return false;
    }
    if (std::fabs(wSum - 1.0f) > kSkinWeightSumTolerance) ok = false;
    for (float& w : s.weight) w = w / wSum;
    return ok;
}

} // namespace eruption
