// sanitizeSkinWeights: a mesma regra do shaders/compute/skin.comp (osso fora da
// skin, peso NaN/inf/negativo, soma diferente de 1, vertice sem influencia).
// O caso normal tem que sair igual bit a bit, para nao mudar a imagem de quem ja
// tinha pesos bons.
#include "renderer/SkinWeights.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

using eruption::SkinInfluence;
using eruption::sanitizeSkinWeights;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FALHOU: %s (linha %d)\n", #c, __LINE__); ++g_fail; } } while (0)

static SkinInfluence make(uint16_t j0, uint16_t j1, uint16_t j2, uint16_t j3, float w0, float w1, float w2, float w3) {
    return SkinInfluence{{j0, j1, j2, j3}, {w0, w1, w2, w3}};
}
static float sum(const SkinInfluence& s) { return s.weight[0] + s.weight[1] + s.weight[2] + s.weight[3]; }
static bool near(float a, float b) { return std::fabs(a - b) < 1e-6f; }
static bool allZero(const SkinInfluence& s) {
    return s.weight[0] == 0.0f && s.weight[1] == 0.0f && s.weight[2] == 0.0f && s.weight[3] == 0.0f;
}

int main() {
    const uint32_t kJoints = 10;
    const float kNan = std::numeric_limits<float>::quiet_NaN();
    const float kInf = std::numeric_limits<float>::infinity();

    // Caso normal, soma 1 exata: nada muda, nem um bit, e nao conta como problema.
    {
        SkinInfluence s = make(1, 2, 3, 0, 0.5f, 0.25f, 0.25f, 0.0f);
        const SkinInfluence before = s;
        CHECK(sanitizeSkinWeights(s, kJoints));
        CHECK(std::memcmp(&s, &before, sizeof(s)) == 0);
    }
    // Slot livre com indice fora da faixa e peso 0: normal, sem aviso.
    {
        SkinInfluence s = make(1, 2, 0xFFFF, 0xFFFF, 0.75f, 0.25f, 0.0f, 0.0f);
        const SkinInfluence before = s;
        CHECK(sanitizeSkinWeights(s, kJoints));
        CHECK(std::memcmp(&s, &before, sizeof(s)) == 0);
    }
    // Indice fora da faixa com peso: o peso some e o resto renormaliza.
    {
        SkinInfluence s = make(1, 12, 3, 0, 0.5f, 0.25f, 0.25f, 0.0f);
        CHECK(!sanitizeSkinWeights(s, kJoints));
        CHECK(s.weight[1] == 0.0f);
        CHECK(near(s.weight[0], 2.0f / 3.0f) && near(s.weight[2], 1.0f / 3.0f));
        CHECK(near(sum(s), 1.0f));
        CHECK(s.joint[1] == 12); // indice fica: com peso 0 o shader nem le a matriz
    }
    // Indice igual a jointCount (limite exato) tambem e' invalido.
    {
        SkinInfluence s = make(static_cast<uint16_t>(kJoints), 0, 0, 0, 1.0f, 0.0f, 0.0f, 0.0f);
        CHECK(!sanitizeSkinWeights(s, kJoints));
        CHECK(allZero(s));
    }
    // NaN.
    {
        SkinInfluence s = make(0, 1, 2, 3, kNan, 0.5f, 0.5f, 0.0f);
        CHECK(!sanitizeSkinWeights(s, kJoints));
        CHECK(s.weight[0] == 0.0f);
        CHECK(near(s.weight[1], 0.5f) && near(s.weight[2], 0.5f));
        for (float w : s.weight) CHECK(!std::isnan(w));
    }
    // Infinito (positivo e negativo).
    {
        SkinInfluence s = make(0, 1, 2, 3, kInf, 1.0f, -kInf, 0.0f);
        CHECK(!sanitizeSkinWeights(s, kJoints));
        CHECK(s.weight[0] == 0.0f && s.weight[2] == 0.0f);
        CHECK(s.weight[1] == 1.0f);
    }
    // Negativo.
    {
        SkinInfluence s = make(0, 1, 2, 3, 1.0f, -0.25f, 0.0f, 0.0f);
        CHECK(!sanitizeSkinWeights(s, kJoints));
        CHECK(s.weight[1] == 0.0f);
        CHECK(s.weight[0] == 1.0f);
    }
    // Soma 0,5: renormaliza para 1, proporcoes mantidas.
    {
        SkinInfluence s = make(0, 1, 2, 3, 0.25f, 0.125f, 0.125f, 0.0f);
        CHECK(!sanitizeSkinWeights(s, kJoints));
        CHECK(s.weight[0] == 0.5f && s.weight[1] == 0.25f && s.weight[2] == 0.25f && s.weight[3] == 0.0f);
        CHECK(sum(s) == 1.0f);
    }
    // Soma 0: sem influencia, pose de bind (todos os pesos 0).
    {
        SkinInfluence s = make(0, 1, 2, 3, 0.0f, 0.0f, 0.0f, 0.0f);
        CHECK(!sanitizeSkinWeights(s, kJoints));
        CHECK(allZero(s));
    }
    // Soma abaixo de 1e-6: tambem sem influencia.
    {
        SkinInfluence s = make(0, 1, 2, 3, 1e-7f, 1e-7f, 0.0f, 0.0f);
        CHECK(!sanitizeSkinWeights(s, kJoints));
        CHECK(allZero(s));
    }
    // Tudo invalido junto (NaN, inf, negativo, osso inexistente): pose de bind.
    {
        SkinInfluence s = make(0, 1, 2, 99, kNan, kInf, -1.0f, 1.0f);
        CHECK(!sanitizeSkinWeights(s, kJoints));
        CHECK(allZero(s));
    }
    // Soma finita que estoura para infinito: pose de bind, sem NaN.
    {
        const float big = std::numeric_limits<float>::max();
        SkinInfluence s = make(0, 1, 2, 3, big, big, 0.0f, 0.0f);
        CHECK(!sanitizeSkinWeights(s, kJoints));
        CHECK(allZero(s));
    }
    // Arredondamento de exportador (soma 0,99999994): renormaliza sem aviso.
    {
        SkinInfluence s = make(0, 1, 2, 3, 0.4f, 0.3f, 0.2f, 0.09999994f);
        CHECK(sanitizeSkinWeights(s, kJoints));
        CHECK(near(sum(s), 1.0f));
    }
    // Skin sem ossos: nenhum indice vale.
    {
        SkinInfluence s = make(0, 0, 0, 0, 1.0f, 0.0f, 0.0f, 0.0f);
        CHECK(!sanitizeSkinWeights(s, 0));
        CHECK(allZero(s));
    }
    std::printf(g_fail ? "HOUVE FALHA\n" : "tudo certo\n");
    return g_fail ? 1 : 0;
}
