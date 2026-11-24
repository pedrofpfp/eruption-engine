#include "renderer/SkinRig.hpp"

#include <algorithm>
#include <cmath>

namespace eruption {

namespace {

// Rigidez, relativa ao quadrado da escala média: 2e-3 no comprimento² = 0,1% no
// comprimento; 1e-3 no produto escalar = ~0,06° fora do ângulo reto. O
// arredondamento de float numa cadeia de ossos fica perto de 1e-6.
constexpr float kRigidLen2Tol = 2e-3f;
constexpr float kRigidDotTol = 1e-3f;
// Abaixo disso o osso está escondido por escala 0 (truque comum de exportador).
constexpr float kZeroScale2 = 1e-12f;
// Zona morta do sinal do quaternion (e do alinhamento ao pivô em skin.comp).
// Numa torção de 180° exatos o w vale cos(90°), que em float é -4,4e-8 e não 0:
// inverter por "w < 0" trocava o lado da mistura no topo da torção e a malha
// girava para o sentido contrário (visto no protótipo da Entrega 5).
constexpr float kDqSignTol = 1e-3f;

// Base de Hermite da CUBICSPLINE (glTF 2.0, apêndice C) em s = (t - t[k]) / td:
// p = h00·v[k] + h10·b[k] + h01·v[k+1] + h11·a[k+1], com b a tangente de saída
// da chave k e a a de entrada da k+1. As tangentes do arquivo estão em
// unidades por segundo, por isso h10 e h11 já vêm multiplicados por td.
struct Hermite {
    float h00, h10, h01, h11;
};

Hermite hermite(float s, float td) {
    const float s2 = s * s;
    const float s3 = s2 * s;
    return {2.0f * s3 - 3.0f * s2 + 1.0f, (s3 - 2.0f * s2 + s) * td, -2.0f * s3 + 3.0f * s2, (s3 - s2) * td};
}

} // namespace

// Valor do canal no tempo t: procura o intervalo e interpola. Fora do
// intervalo das chaves devolve a ponta (sem extrapolar), em todos os modos.
Vec4 SkinRig::sampleChannel(const Channel& c, float t) {
    if (c.times.empty() || c.values.empty()) return Vec4(0.0f);
    const size_t n = std::min(c.times.size(), c.values.size());
    if (t <= c.times.front() || n == 1) return c.values.front();
    if (t >= c.times[n - 1]) return c.values[n - 1];
    const size_t hi = static_cast<size_t>(std::upper_bound(c.times.begin(), c.times.begin() + n, t) - c.times.begin());
    const size_t lo = hi - 1;
    if (c.interp == Channel::Interp::Step) return c.values[lo];
    const float span = c.times[hi] - c.times[lo];
    const float f = span > 0.0f ? (t - c.times[lo]) / span : 0.0f;
    // Cubic só com as duas tangentes cobrindo as chaves usadas; um canal
    // montado à mão (ou truncado) sem elas cai no LINEAR abaixo em vez de
    // ler fora do vetor.
    if (c.interp == Channel::Interp::Cubic && span > 0.0f && c.inTan.size() >= n && c.outTan.size() >= n) {
        const Hermite h = hermite(f, span);
        const Vec4 p = h.h00 * c.values[lo] + h.h10 * c.outTan[lo] + h.h01 * c.values[hi] + h.h11 * c.inTan[hi];
        if (c.path != 1) return p;
        // Rotação: a especificação manda Hermite nos 4 componentes e
        // normalizar, não slerp. Quaternion nulo (tangentes absurdas) fica
        // com a chave de baixo em vez de virar NaN.
        const float len = glm::length(p);
        return len > 1e-8f ? p / len : c.values[lo];
    }
    if (c.path == 1) {
        const glm::quat a(c.values[lo].w, c.values[lo].x, c.values[lo].y, c.values[lo].z);
        const glm::quat b(c.values[hi].w, c.values[hi].x, c.values[hi].y, c.values[hi].z);
        const glm::quat q = glm::slerp(a, b, f);
        return Vec4(q.x, q.y, q.z, q.w);
    }
    return glm::mix(c.values[lo], c.values[hi], f);
}

namespace {

// Posição, rotação e escala locais de cada nó (a de repouso onde a animação
// não tem canal).
void localPose(const SkinRig& rig, int anim, float time, std::vector<Vec3>& t, std::vector<glm::quat>& r,
               std::vector<Vec3>& s) {
    const size_t n = rig.nodes.size();
    t.resize(n);
    r.resize(n);
    s.resize(n);
    for (size_t i = 0; i < n; ++i) {
        t[i] = rig.nodes[i].t;
        r[i] = rig.nodes[i].r;
        s[i] = rig.nodes[i].s;
    }
    if (anim < 0 || anim >= static_cast<int>(rig.animations.size())) return;
    for (const SkinRig::Channel& c : rig.animations[static_cast<size_t>(anim)].channels) {
        if (c.node < 0 || c.node >= static_cast<int>(n) || c.path > 2) continue; // pesos de morph: morphWeights
        const Vec4 v = SkinRig::sampleChannel(c, time);
        const size_t k = static_cast<size_t>(c.node);
        if (c.path == 0) t[k] = Vec3(v);
        else if (c.path == 1) r[k] = glm::normalize(glm::quat(v.w, v.x, v.y, v.z));
        else s[k] = Vec3(v);
    }
}

} // namespace

int SkinRig::findAnimation(const std::string& name) const {
    for (size_t i = 0; i < animations.size(); ++i)
        if (animations[i].name == name) return static_cast<int>(i);
    return -1;
}

void SkinRig::pose(int anim, float time, std::vector<glm::mat4>& globals) const {
    blendPose(anim, time, anim, time, 1.0f, globals);
}

void SkinRig::blendPose(int from, float fromTime, int anim, float time, float w, std::vector<glm::mat4>& globals) const {
    std::vector<Vec3> t, s;
    std::vector<glm::quat> r;
    localPose(*this, anim, time, t, r, s);
    if (w < 1.0f) {
        std::vector<Vec3> t0, s0;
        std::vector<glm::quat> r0;
        localPose(*this, from, fromTime, t0, r0, s0);
        const float k = std::clamp(w, 0.0f, 1.0f);
        for (size_t i = 0; i < nodes.size(); ++i) {
            t[i] = glm::mix(t0[i], t[i], k);
            s[i] = glm::mix(s0[i], s[i], k);
            r[i] = glm::slerp(r0[i], r[i], k); // o caminho curto (sinal do quaternion)
        }
    }
    globals.resize(nodes.size());
    for (int i : order) {
        const size_t k = static_cast<size_t>(i);
        const glm::mat4 local = glm::translate(glm::mat4(1.0f), t[k]) * glm::mat4_cast(r[k]) * glm::scale(glm::mat4(1.0f), s[k]);
        globals[k] = nodes[k].parent >= 0 ? globals[static_cast<size_t>(nodes[k].parent)] * local : local;
    }
}

bool SkinRig::morphWeights(int anim, float time, int node, float* out, uint32_t count) const {
    if (anim < 0 || anim >= static_cast<int>(animations.size()) || node < 0) return false;
    for (const Channel& c : animations[static_cast<size_t>(anim)].channels) {
        if (c.path != 3 || c.node != node || c.weightCount == 0 || c.times.empty()) continue;
        const size_t keys = std::min(c.times.size(), c.weights.size() / c.weightCount);
        if (keys == 0) return false;
        size_t lo = 0, hi = 0;
        float f = 0.0f, span = 0.0f;
        if (time >= c.times[keys - 1]) {
            lo = hi = keys - 1;
        } else if (time > c.times.front()) {
            hi = static_cast<size_t>(std::upper_bound(c.times.begin(), c.times.begin() + std::ptrdiff_t(keys), time) -
                                     c.times.begin());
            lo = hi - 1;
            span = c.times[hi] - c.times[lo];
            f = c.interp == Channel::Interp::Step || span <= 0.0f ? 0.0f : (time - c.times[lo]) / span;
        }
        // Cubic entre duas chaves distintas e com as tangentes completas; nas
        // pontas (lo == hi) o valor é o da chave, como no LINEAR.
        const size_t need = keys * c.weightCount;
        const bool cubic = c.interp == Channel::Interp::Cubic && lo != hi && span > 0.0f &&
                           c.weightsIn.size() >= need && c.weightsOut.size() >= need;
        const Hermite h = cubic ? hermite(f, span) : Hermite{};
        for (uint32_t t = 0; t < count; ++t) {
            if (t >= c.weightCount) {
                out[t] = 0.0f;
                continue;
            }
            const float a = c.weights[lo * c.weightCount + t];
            const float b = c.weights[hi * c.weightCount + t];
            if (cubic)
                out[t] = h.h00 * a + h.h10 * c.weightsOut[lo * c.weightCount + t] + h.h01 * b +
                         h.h11 * c.weightsIn[hi * c.weightCount + t];
            else
                out[t] = a + (b - a) * f;
        }
        return true;
    }
    return false;
}

void SkinRig::jointMatrices(int skin, const std::vector<glm::mat4>& globals, glm::mat4* out) const {
    if (skin < 0 || skin >= static_cast<int>(skins.size())) return;
    const Skin& sk = skins[static_cast<size_t>(skin)];
    for (size_t j = 0; j < sk.joints.size(); ++j) {
        const int n = sk.joints[j];
        const glm::mat4 g = n >= 0 && n < static_cast<int>(globals.size()) ? globals[static_cast<size_t>(n)] : glm::mat4(1.0f);
        out[j] = g * (j < sk.inverseBind.size() ? sk.inverseBind[j] : glm::mat4(1.0f));
    }
}

bool SkinRig::dualQuat(const glm::mat4& S, glm::mat4& out) {
    const Vec3 c0(S[0]), c1(S[1]), c2(S[2]), t(S[3]);
    const float l0 = glm::dot(c0, c0), l1 = glm::dot(c1, c1), l2 = glm::dot(c2, c2);
    const float s2 = (l0 + l1 + l2) / 3.0f;
    glm::mat4 d(0.0f);
    if (!std::isfinite(s2) || !std::isfinite(t.x + t.y + t.z)) {
        out = d;
        return false;
    }
    if (s2 < kZeroScale2) {
        // Escala 0: rotação identidade e escala 0 levam o vértice a t, como o LBS.
        d[0] = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        d[1] = Vec4(0.5f * t, 0.0f);
        out = d;
        return true;
    }
    const float inv = 1.0f / s2;
    const bool rigid = std::fabs(l0 * inv - 1.0f) <= kRigidLen2Tol && std::fabs(l1 * inv - 1.0f) <= kRigidLen2Tol &&
                       std::fabs(l2 * inv - 1.0f) <= kRigidLen2Tol &&
                       std::fabs(glm::dot(c0, c1)) * inv <= kRigidDotTol &&
                       std::fabs(glm::dot(c0, c2)) * inv <= kRigidDotTol &&
                       std::fabs(glm::dot(c1, c2)) * inv <= kRigidDotTol &&
                       glm::dot(glm::cross(c0, c1), c2) > 0.0f;
    if (!rigid) {
        out = d;
        return false;
    }
    const glm::mat3 R(c0 / std::sqrt(l0), c1 / std::sqrt(l1), c2 / std::sqrt(l2));
    glm::quat q0 = glm::normalize(glm::quat_cast(R));
    if (q0.w < -kDqSignTol) q0 = -q0; // tolerância: ver kDqSignTol
    const glm::quat qe = 0.5f * (glm::quat(0.0f, t.x, t.y, t.z) * q0);
    d[0] = Vec4(q0.x, q0.y, q0.z, q0.w);
    d[1] = Vec4(qe.x, qe.y, qe.z, qe.w);
    d[2] = Vec4(std::sqrt(s2), 0.0f, 0.0f, 0.0f);
    out = d;
    return true;
}

bool SkinRig::jointDualQuats(int skin, const std::vector<glm::mat4>& globals, glm::mat4* out) const {
    if (skin < 0 || skin >= static_cast<int>(skins.size())) return false;
    const Skin& sk = skins[static_cast<size_t>(skin)];
    for (size_t j = 0; j < sk.joints.size(); ++j) {
        const int n = sk.joints[j];
        const glm::mat4 g = n >= 0 && n < static_cast<int>(globals.size()) ? globals[static_cast<size_t>(n)] : glm::mat4(1.0f);
        glm::mat4 d;
        if (!dualQuat(g * (j < sk.inverseBind.size() ? sk.inverseBind[j] : glm::mat4(1.0f)), d)) return false;
        out[j] = d; // um mat4 inteiro por escrita: `out` é o anel mapeado (write-combined)
    }
    return true;
}

} // namespace eruption
