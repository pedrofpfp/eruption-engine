// Quaternion duplo dos ossos (renderer/SkinRig: dualQuat e jointDualQuats) e o
// ramo DQS de shaders/compute/skin.comp, replicado em C++ abaixo (dqsSkin e
// lbsSkin): mudou lá, muda aqui. Sem arquivo .glb nem GPU. Grupos:
//  1. matriz rígida: o quaternion duplo reaplicado reproduz a matriz, escala 1,
//     colunas livres do mat4 zeradas;
//  2. escala uniforme 0,01 (bind de exportador em centímetros): rígida, escala
//     preservada, pontos de ordem 100 reproduzidos;
//  3. escala não uniforme, espelho e cisalhamento não são rígidos; ruído de
//     1e-5 continua rígido;
//  4. escala 0 (osso escondido): aceita, s = 0, o vértice vai para t;
//  5. q0.w >= 0 em 4 eixos de 0 a 720 graus;
//  6. torção de 180 graus com pesos 0,5: DQS mantém o raio, LBS colapsa no eixo;
//  7. antipodalidade: o mesmo osso com q0 e qe negados, pesos 0,5, dá a pose de
//     um osso só;
//  8. jointDualQuats pela skin: bind inversa cancelada pelo global, nó fora do
//     vetor de globais vira identidade, osso esticado devolve falso.
#include "renderer/SkinRig.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace eruption;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FALHA", what);
    if (!ok) ++g_fail;
}

float maxDiff(const glm::mat4& a, const glm::mat4& b) {
    float m = 0.0f;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) m = std::fmax(m, std::fabs(a[c][r] - b[c][r]));
    return m;
}

float dist(const Vec3& a, const Vec3& b) { return glm::length(a - b); }

constexpr float kPi = 3.14159265358979f;

// Réplica de quatRotate de skin.comp: v + 2 q.xyz x (q.xyz x v + q.w v).
Vec3 quatRotate(const Vec4& q, const Vec3& v) {
    const Vec3 u(q);
    return v + 2.0f * glm::cross(u, glm::cross(u, v) + q.w * v);
}

// t = 2 qe conj(q0), parte vetorial (o mesmo do ramo DQS do shader).
Vec3 dqTranslation(const Vec4& c0, const Vec4& ce) {
    return 2.0f * (c0.w * Vec3(ce) - ce.w * Vec3(c0) + glm::cross(Vec3(c0), Vec3(ce)));
}

// Réplica do ramo DQS de skin.comp para um vértice. Pesos já limpos e
// normalizados (cleanWeight e a divisão por wSum ficam fora, como em
// tests/test_skin_weights.cpp). Pivô no primeiro peso > 0, sinal por
// dot(q0, pivô), escala somada sem sinal, normalização por |b0|, escala
// uniforme antes da rotação.
void dqsSkin(const glm::mat4* joints, const uint32_t j[4], const float w[4], const Vec3& pos, const Vec3& nrm,
             Vec3& sp, Vec3& sn) {
    sp = pos;
    sn = nrm;
    const uint32_t jp = w[0] > 0.0f ? j[0] : (w[1] > 0.0f ? j[1] : (w[2] > 0.0f ? j[2] : j[3]));
    const Vec4 pivot = joints[jp][0];
    Vec4 b0(0.0f), be(0.0f);
    float sc = 0.0f;
    for (int k = 0; k < 4; ++k) {
        if (w[k] <= 0.0f) continue;
        const Vec4 q0 = joints[j[k]][0];
        const float sw = glm::dot(q0, pivot) < -1e-3f ? -w[k] : w[k];
        b0 += sw * q0;
        be += sw * joints[j[k]][1];
        sc += w[k] * joints[j[k]][2].x;
    }
    const float n2 = glm::dot(b0, b0);
    if (n2 >= 1e-12f) {
        const float r = 1.0f / std::sqrt(n2);
        const Vec4 c0 = b0 * r;
        const Vec4 ce = be * r;
        sp = quatRotate(c0, sc * pos) + dqTranslation(c0, ce);
        sn = quatRotate(c0, nrm);
    }
    const float len = glm::length(sn);
    sn = len > 1e-8f ? sn / len : nrm;
}

// Réplica do ramo LBS de skin.comp (soma ponderada das matrizes).
Vec3 lbsSkin(const glm::mat4* joints, const uint32_t j[4], const float w[4], const Vec3& pos) {
    glm::mat4 m(0.0f);
    for (int k = 0; k < 4; ++k)
        if (w[k] > 0.0f) m += joints[j[k]] * w[k];
    return Vec3(m * Vec4(pos, 1.0f));
}

// Um osso só, peso 1.
Vec3 applyOne(const glm::mat4& d, const Vec3& p) {
    const uint32_t j[4] = {0, 0, 0, 0};
    const float w[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    Vec3 sp, sn;
    dqsSkin(&d, j, w, p, Vec3(0.0f, 0.0f, 1.0f), sp, sn);
    return sp;
}

// Matriz de volta a partir do quaternion duplo: R(q0) * s, coluna 3 = t.
glm::mat4 toMatrix(const glm::mat4& d) {
    const glm::quat q(d[0].w, d[0].x, d[0].y, d[0].z);
    glm::mat4 m = glm::mat4_cast(q) * glm::scale(glm::mat4(1.0f), Vec3(d[2].x));
    m[3] = Vec4(dqTranslation(d[0], d[1]), 1.0f);
    return m;
}

glm::mat4 rigid(const Vec3& t, float angle, const Vec3& axis, float scale = 1.0f) {
    return glm::translate(glm::mat4(1.0f), t) * glm::rotate(glm::mat4(1.0f), angle, glm::normalize(axis)) *
           glm::scale(glm::mat4(1.0f), Vec3(scale));
}

void testRigid() {
    std::printf("matriz rigida reaplicada\n");
    const glm::mat4 S = rigid(Vec3(1.5f, -2.0f, 0.25f), 1.1f, Vec3(0.3f, 1.0f, -0.4f));
    glm::mat4 d;
    check(SkinRig::dualQuat(S, d), "rigida aceita");
    check(maxDiff(toMatrix(d), S) < 1e-5f, "reconstrucao igual a matriz (< 1e-5)");
    check(std::fabs(d[2].x - 1.0f) < 1e-6f, "escala 1");
    check(d[2].y == 0.0f && d[2].z == 0.0f && d[2].w == 0.0f && d[3] == Vec4(0.0f), "colunas livres zeradas");
    check(std::fabs(glm::length(d[0]) - 1.0f) < 1e-6f, "q0 unitario");
    const Vec3 p(0.7f, -0.3f, 2.0f);
    check(dist(applyOne(d, p), Vec3(S * Vec4(p, 1.0f))) < 1e-5f, "ponto pelo ramo DQS igual a S * p");
}

void testUniformScale() {
    std::printf("escala uniforme 0,01\n");
    const glm::mat4 S = rigid(Vec3(0.2f, 1.0f, -0.5f), 0.6f, Vec3(1.0f, 0.0f, 1.0f), 0.01f);
    glm::mat4 d;
    check(SkinRig::dualQuat(S, d), "escala uniforme aceita");
    check(std::fabs(d[2].x - 0.01f) < 1e-7f, "escala 0,01 preservada");
    float worst = 0.0f;
    const Vec3 pts[] = {{100.0f, 0.0f, 0.0f}, {0.0f, -100.0f, 50.0f}, {37.0f, 82.0f, -64.0f}};
    for (const Vec3& p : pts) worst = std::fmax(worst, dist(applyOne(d, p), Vec3(S * Vec4(p, 1.0f))));
    check(worst < 1e-5f, "pontos x100 reproduzidos (< 1e-5)");
}

void testNotRigid() {
    std::printf("nao rigidos\n");
    glm::mat4 d;
    const glm::mat4 base = rigid(Vec3(1.0f, 2.0f, 3.0f), 0.8f, Vec3(0.0f, 1.0f, 0.0f));
    check(!SkinRig::dualQuat(base * glm::scale(glm::mat4(1.0f), Vec3(1.0f, 2.0f, 1.0f)), d), "escala nao uniforme");
    check(!SkinRig::dualQuat(base * glm::scale(glm::mat4(1.0f), Vec3(-1.0f, 1.0f, 1.0f)), d), "espelho");
    glm::mat4 shear(1.0f);
    shear[1][0] = 0.1f;
    check(!SkinRig::dualQuat(base * shear, d), "cisalhamento 0,1");
    glm::mat4 noisy = base;
    noisy[0][1] += 1e-5f;
    noisy[1][2] -= 1e-5f;
    noisy[2][0] += 1e-5f;
    check(SkinRig::dualQuat(noisy, d), "ruido de 1e-5 continua rigido");
    glm::mat4 bad = base;
    bad[3][0] = std::nanf("");
    check(!SkinRig::dualQuat(bad, d), "NaN na translacao");
}

void testZeroScale() {
    std::printf("escala 0\n");
    const Vec3 t(3.0f, -1.0f, 4.0f);
    const glm::mat4 S = glm::translate(glm::mat4(1.0f), t) * glm::scale(glm::mat4(1.0f), Vec3(0.0f));
    glm::mat4 d;
    check(SkinRig::dualQuat(S, d), "escala 0 aceita");
    check(d[2].x == 0.0f, "s = 0");
    check(dist(applyOne(d, Vec3(5.0f, 6.0f, -7.0f)), t) < 1e-6f, "vertice vai para t");
}

void testPositiveW() {
    std::printf("q0.w >= 0\n");
    const Vec3 axes[] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}};
    bool allW = true, allOk = true;
    float worst = 0.0f;
    for (const Vec3& a : axes) {
        for (int deg = 0; deg <= 720; deg += 10) {
            const glm::mat4 S = rigid(Vec3(0.5f, 0.0f, -1.0f), static_cast<float>(deg) * kPi / 180.0f, a);
            glm::mat4 d;
            if (!SkinRig::dualQuat(S, d)) {
                allOk = false;
                continue;
            }
            if (d[0].w < -1e-3f) allW = false; // zona morta kDqSignTol
            worst = std::fmax(worst, maxDiff(toMatrix(d), S));
        }
    }
    check(allOk, "todas as rotacoes aceitas");
    check(allW, "w >= -1e-3 em todas");
    check(worst < 1e-5f, "todas reconstruidas (< 1e-5)");
}

void testTwist() {
    std::printf("torcao de 180 graus, pesos 0,5\n");
    glm::mat4 joints[2];
    check(SkinRig::dualQuat(glm::mat4(1.0f), joints[0]), "identidade");
    check(SkinRig::dualQuat(glm::rotate(glm::mat4(1.0f), kPi, Vec3(0.0f, 0.0f, 1.0f)), joints[1]), "rot180 em z");
    const uint32_t j[4] = {0, 1, 0, 0};
    const float w[4] = {0.5f, 0.5f, 0.0f, 0.0f};
    // LBS pelas matrizes (em LBS o anel guarda a matriz, não o quaternion).
    const glm::mat4 mats[2] = {glm::mat4(1.0f), glm::rotate(glm::mat4(1.0f), kPi, Vec3(0.0f, 0.0f, 1.0f))};
    float dqsDev = 0.0f, lbsMax = 0.0f;
    for (int k = 0; k < 16; ++k) {
        const float a = static_cast<float>(k) * 2.0f * kPi / 16.0f;
        const Vec3 p(2.0f * std::cos(a), 2.0f * std::sin(a), 0.5f);
        Vec3 sp, sn;
        dqsSkin(joints, j, w, p, Vec3(std::cos(a), std::sin(a), 0.0f), sp, sn);
        dqsDev = std::fmax(dqsDev, std::fabs(std::hypot(sp.x, sp.y) - 2.0f));
        const Vec3 lm = lbsSkin(mats, j, w, p);
        lbsMax = std::fmax(lbsMax, std::hypot(lm.x, lm.y));
    }
    std::printf("    desvio de raio DQS %.2e, raio LBS %.2e\n", static_cast<double>(dqsDev), static_cast<double>(lbsMax));
    check(dqsDev < 1e-4f, "DQS mantem o raio 2 (< 1e-4)");
    check(lbsMax < 2e-4f, "LBS colapsa no eixo (raio < 2e-4)");
}

void testAntipodal() {
    std::printf("antipodalidade\n");
    glm::mat4 joints[2];
    const glm::mat4 S = rigid(Vec3(-1.0f, 0.5f, 2.0f), 2.3f, Vec3(0.2f, -1.0f, 0.6f));
    check(SkinRig::dualQuat(S, joints[0]), "osso rigido");
    joints[1] = joints[0];
    joints[1][0] = -joints[0][0];
    joints[1][1] = -joints[0][1];
    const uint32_t j[4] = {0, 1, 0, 0};
    const float w[4] = {0.5f, 0.5f, 0.0f, 0.0f};
    const Vec3 p(0.3f, 1.2f, -0.8f);
    Vec3 sp, sn;
    dqsSkin(joints, j, w, p, Vec3(0.0f, 1.0f, 0.0f), sp, sn);
    check(dist(sp, applyOne(joints[0], p)) < 1e-5f, "q e -q somam a mesma pose (< 1e-5)");
    check(dist(sp, Vec3(S * Vec4(p, 1.0f))) < 1e-5f, "igual a S * p");
}

void testSkin() {
    std::printf("jointDualQuats pela skin\n");
    SkinRig rig;
    SkinRig::Skin sk;
    sk.joints = {0, 1, 7};
    const glm::mat4 ib = glm::scale(glm::mat4(1.0f), Vec3(0.01f));
    sk.inverseBind = {ib, glm::mat4(1.0f), glm::mat4(1.0f)};
    rig.skins.push_back(sk);
    std::vector<glm::mat4> globals = {rigid(Vec3(1.0f, 0.0f, 0.0f), 0.5f, Vec3(0.0f, 0.0f, 1.0f), 100.0f),
                                      rigid(Vec3(0.0f, 2.0f, 0.0f), -1.2f, Vec3(1.0f, 1.0f, 0.0f))};
    glm::mat4 out[3];
    check(rig.jointDualQuats(0, globals, out), "skin rigida aceita");
    glm::mat4 d0, d1;
    SkinRig::dualQuat(globals[0] * ib, d0);
    SkinRig::dualQuat(globals[1], d1);
    check(maxDiff(out[0], d0) == 0.0f && maxDiff(out[1], d1) == 0.0f, "igual a dualQuat(global * bind inversa)");
    check(std::fabs(out[0][2].x - 1.0f) < 1e-5f, "escala 0,01 cancelada pelo global");
    check(out[2][0] == Vec4(0.0f, 0.0f, 0.0f, 1.0f) && out[2][1] == Vec4(0.0f) && out[2][2].x == 1.0f,
          "no 7 fora dos globais vira identidade");
    check(!rig.jointDualQuats(1, globals, out), "skin inexistente: falso");
    globals[1] = globals[1] * glm::scale(glm::mat4(1.0f), Vec3(1.0f, 2.0f, 1.0f));
    check(!rig.jointDualQuats(0, globals, out), "osso esticado (1, 2, 1): falso");
}

// Torção parada em 180° exatos (o defeito visto no protótipo): w = cos(90°) em
// float é -4,4e-8. O sentido da mistura em 180° tem que ser o mesmo de 179°,
// senão a malha gira para o lado contrário no topo da torção.
void testTwistHold180() {
    std::printf("torcao parada em 180 graus\n");
    const Vec3 x(1.0f, 0.0f, 0.0f);
    const uint32_t j[4] = {0, 1, 0, 0};
    const float w[4] = {0.5f, 0.5f, 0.0f, 0.0f};
    const Vec3 p(0.0f, 1.0f, 0.0f), n(0.0f, 1.0f, 0.0f);
    float side[2] = {0.0f, 0.0f};
    const float degs[2] = {179.0f, 180.0f};
    for (int i = 0; i < 2; ++i) {
        glm::mat4 joints[2];
        SkinRig::dualQuat(glm::mat4(1.0f), joints[0]);
        SkinRig::dualQuat(rigid(Vec3(0.0f), glm::radians(degs[i]), x), joints[1]);
        Vec3 sp, sn;
        dqsSkin(joints, j, w, p, n, sp, sn);
        side[i] = sp.z; // +90° em x leva (0,1,0) para (0,0,1)
    }
    check(side[0] > 0.5f && side[1] > 0.5f, "180 graus gira para o mesmo lado de 179");
}

} // namespace

int main() {
    testRigid();
    testUniformScale();
    testNotRigid();
    testZeroScale();
    testPositiveW();
    testTwist();
    testAntipodal();
    testSkin();
    testTwistHold180();
    std::printf(g_fail ? "HOUVE FALHA\n" : "tudo certo\n");
    return g_fail ? 1 : 0;
}
