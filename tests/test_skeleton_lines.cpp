// Esqueleto de depuração (src/renderer/SkeletonLines.hpp): pai de cada osso na
// skin pulando nós que não são ossos, segmentos pai -> filho e caixas nas
// juntas, e a cor igual à jointColor() de shaders/compute/skin.comp.
#include "renderer/SkeletonLines.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace eruption;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FALHOU: %s (linha %d)\n", #c, __LINE__); ++g_fail; } } while (0)

static bool near(const Vec3& a, const Vec3& b) { return glm::length(a - b) < 1e-5f; }

int main() {
    // Rig procedural: nó 0 = raiz da cena, 1 = armadura (não é osso),
    // 2 = quadril, 3 = auxiliar (não é osso), 4 = joelho. Skin = {2, 4}.
    {
        const std::vector<int> nodeParent = {-1, 0, 1, 2, 3};
        const std::vector<int> joints = {2, 4};
        std::vector<int> parents;
        skinJointParents(nodeParent, joints, parents);
        CHECK(parents.size() == 2);
        CHECK(parents[0] == -1); // raiz da cena e armadura não entram
        CHECK(parents[1] == 0);  // o auxiliar é pulado: joelho -> quadril

        std::vector<SkeletonJoint> js(2);
        js[0].pos = Vec3(0.0f, 1.0f, 0.0f);
        js[0].parent = parents[0];
        js[0].joint = 0;
        js[0].half = 0.01f;
        js[1].pos = Vec3(0.0f, 0.5f, 0.1f);
        js[1].parent = parents[1];
        js[1].joint = 1;
        js[1].half = 0.01f;
        std::vector<SkeletonSegment> lines, boxes;
        skeletonSegments(js, lines, boxes);
        CHECK(lines.size() == 1);
        CHECK(boxes.size() == 2);
        if (lines.size() == 1) {
            CHECK(near(lines[0].a, js[0].pos));
            CHECK(near(lines[0].b, js[1].pos));
            CHECK(near(lines[0].color, skeletonJointColor(1))); // cor do filho
        }
        if (boxes.size() == 2) {
            CHECK(near(boxes[1].a, js[1].pos - Vec3(0.01f)));
            CHECK(near(boxes[1].b, js[1].pos + Vec3(0.01f)));
        }
    }
    // Skin com a ordem dos ossos diferente da dos nós e pais diretos que são
    // ossos. Ciclo nos pais do arquivo não trava.
    {
        const std::vector<int> nodeParent = {-1, 0, 1, 3, 2};
        const std::vector<int> joints = {1, 0, 2};
        std::vector<int> parents;
        skinJointParents(nodeParent, joints, parents);
        CHECK(parents[0] == 1);  // nó 1 -> nó 0 (osso 1)
        CHECK(parents[1] == -1);
        CHECK(parents[2] == 0);  // nó 2 -> nó 1 (osso 0)
        const std::vector<int> cyc = {1, 0};
        const std::vector<int> cj = {0};
        skinJointParents(cyc, cj, parents);
        CHECK(parents.size() == 1 && parents[0] == 0); // 0 -> 1 -> 0: acha o próprio nó e para
        const std::vector<int> bad = {-1};
        const std::vector<int> bj = {5, 0};
        skinJointParents(bad, bj, parents);
        CHECK(parents[0] == -1 && parents[1] == -1); // osso fora do arquivo: sem pai
    }
    // Pai fora do vetor e meia aresta 0: nem linha nem caixa.
    {
        std::vector<SkeletonJoint> js(1);
        js[0].parent = 7;
        std::vector<SkeletonSegment> lines, boxes;
        skeletonSegments(js, lines, boxes);
        CHECK(lines.empty() && boxes.empty());
    }
    // Cor: a fórmula de skin.comp, conferida contra a conta em GLM.
    for (uint32_t j = 0; j < 64; ++j) {
        const float h = glm::fract(float(j) * 0.61803398875f + 0.15f);
        const Vec3 k = glm::clamp(glm::abs(glm::fract(h + Vec3(0.0f, 2.0f / 3.0f, 1.0f / 3.0f)) * 6.0f - 3.0f) - 1.0f,
                                  0.0f, 1.0f);
        const Vec3 ref = glm::mix(Vec3(1.0f), k, 0.85f);
        CHECK(near(skeletonJointColor(j), ref));
    }
    // Osso 0: h = 0,15 -> vermelho puxado para o amarelo (S = V = 1, 15% branco).
    CHECK(near(skeletonJointColor(0), Vec3(1.0f, 0.15f + 0.85f * 0.9f, 0.15f)));

    if (g_fail == 0) std::printf("test_skeleton_lines: OK\n");
    return g_fail == 0 ? 0 : 1;
}
