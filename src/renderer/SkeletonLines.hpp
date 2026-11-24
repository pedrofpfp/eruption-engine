#pragma once
// Depuração do esqueleto (ModelRenderer::collectSkeletonLines): a conta pura
// que transforma juntas (posição de mundo + pai na skin) em segmentos, sem
// Vulkan, para o teste sem GPU (tests/test_skeleton_lines.cpp) provar a regra.
#include "math/Types.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace eruption {

// Uma junta de uma instância: `parent` é o índice (no mesmo vetor) do primeiro
// ancestral que também é osso da skin, -1 se nenhum. `joint` é o índice do osso
// na skin (dá a cor) e `half` a meia aresta da caixa da junta, em metros.
struct SkeletonJoint {
    Vec3 pos{0.0f};
    int parent = -1;
    uint32_t joint = 0;
    float half = 0.0f;
};

// Linha pai -> filho (a, b) ou caixa da junta (a = mínimo, b = máximo).
struct SkeletonSegment {
    Vec3 a{0.0f}, b{0.0f}, color{1.0f};
};

// Réplica em C++ de jointColor() de shaders/compute/skin.comp: a linha de um
// osso tem a mesma cor que os vértices dele na visualização dos pesos, então
// ligar as duas mostra qual osso puxa cada parte da malha. PRECISA casar com
// o shader (matiz pela razão áurea, HSV -> RGB com S = V = 1, 85% de cor).
inline Vec3 skeletonJointColor(uint32_t j) {
    const float x = static_cast<float>(j) * 0.61803398875f + 0.15f;
    const float h = x - std::floor(x);
    const float off[3] = {0.0f, 2.0f / 3.0f, 1.0f / 3.0f};
    Vec3 c(0.0f);
    for (int i = 0; i < 3; ++i) {
        const float f = (h + off[i]) - std::floor(h + off[i]);
        const float k = std::fabs(f * 6.0f - 3.0f) - 1.0f;
        c[i] = 1.0f + (std::fmin(std::fmax(k, 0.0f), 1.0f) - 1.0f) * 0.85f; // mix(1, k, 0.85)
    }
    return c;
}

// Pai de cada osso da skin, como índice na própria skin: sobe pelos pais do
// arquivo (`nodeParent`, por nó) até achar um nó que também é osso. Pular os
// nós que não são ossos evita desenhar a raiz da cena e o nó da armadura, e
// mantém ligado um osso cujo pai direto é um nó auxiliar (comum no Blender).
inline void skinJointParents(const std::vector<int>& nodeParent, const std::vector<int>& joints,
                             std::vector<int>& out) {
    std::vector<int> jointOfNode(nodeParent.size(), -1);
    for (size_t j = 0; j < joints.size(); ++j) {
        const int n = joints[j];
        if (n >= 0 && static_cast<size_t>(n) < nodeParent.size()) jointOfNode[static_cast<size_t>(n)] = static_cast<int>(j);
    }
    out.assign(joints.size(), -1);
    for (size_t j = 0; j < joints.size(); ++j) {
        int n = joints[j];
        if (n < 0 || static_cast<size_t>(n) >= nodeParent.size()) continue;
        // Limite de passos: um arquivo com ciclo nos pais não trava o frame.
        for (size_t step = 0; step < nodeParent.size(); ++step) {
            n = nodeParent[static_cast<size_t>(n)];
            if (n < 0 || static_cast<size_t>(n) >= nodeParent.size()) break;
            if (jointOfNode[static_cast<size_t>(n)] >= 0) {
                out[j] = jointOfNode[static_cast<size_t>(n)];
                break;
            }
        }
    }
}

// Segmentos do esqueleto: uma linha de cada junta com pai até o pai (cor do
// filho) e uma caixa em cada junta. Pais fora do vetor são ignorados.
inline void skeletonSegments(const std::vector<SkeletonJoint>& joints, std::vector<SkeletonSegment>& lines,
                             std::vector<SkeletonSegment>& boxes) {
    lines.clear();
    boxes.clear();
    for (const SkeletonJoint& j : joints) {
        const Vec3 color = skeletonJointColor(j.joint);
        if (j.parent >= 0 && static_cast<size_t>(j.parent) < joints.size())
            lines.push_back({joints[static_cast<size_t>(j.parent)].pos, j.pos, color});
        if (j.half > 0.0f) boxes.push_back({j.pos - Vec3(j.half), j.pos + Vec3(j.half), color});
    }
}

} // namespace eruption
