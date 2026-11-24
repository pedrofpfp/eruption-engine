// Amostragem dos canais de animação (renderer/SkinRig): CUBICSPLINE pela
// spline de Hermite do glTF 2.0 (apêndice C), com STEP e LINEAR inalterados.
// Canais montados à mão, sem arquivo .glb nem GPU.
#include "renderer/SkinRig.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace eruption;

namespace {

using Interp = SkinRig::Channel::Interp;

int g_fail = 0;

void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FALHA", what);
    if (!ok) ++g_fail;
}

float maxDiff(const Vec4& a, const Vec4& b) {
    const Vec4 d = glm::abs(a - b);
    return std::fmax(std::fmax(d.x, d.y), std::fmax(d.z, d.w));
}

float smoothstep01(float s) { return s * s * (3.0f - 2.0f * s); }

SkinRig::Channel channel(int path, Interp interp, std::vector<float> times, std::vector<Vec4> values) {
    SkinRig::Channel c;
    c.node = 0;
    c.path = path;
    c.interp = interp;
    c.times = std::move(times);
    c.values = std::move(values);
    return c;
}

// Mesmo canal de posição usado para tirar os valores de referência do
// protótipo da Entrega 5 (anim::sampleClip): 3 chaves com tangentes arbitrárias.
SkinRig::Channel referencePosition() {
    SkinRig::Channel c = channel(0, Interp::Cubic, {0.0f, 1.0f, 2.5f},
                                 {{0.0f, 1.0f, -2.0f, 0.0f}, {1.0f, 0.5f, 3.0f, 0.0f}, {2.0f, 0.0f, -1.0f, 0.0f}});
    c.inTan = {Vec4(0.0f), {0.75f, 2.0f, 1.0f, 0.0f}, {0.5f, -1.0f, 0.0f, 0.0f}};
    c.outTan = {{1.5f, -0.5f, 0.25f, 0.0f}, {-1.0f, 0.5f, 2.0f, 0.0f}, Vec4(0.0f)};
    return c;
}

SkinRig::Channel referenceRotation() {
    SkinRig::Channel c = channel(1, Interp::Cubic, {0.0f, 2.0f},
                                 {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.70710678f, 0.0f, 0.70710678f}});
    c.inTan = {Vec4(0.0f), {0.1f, 0.3f, -0.2f, 0.05f}};
    c.outTan = {{0.4f, -0.2f, 0.1f, 0.0f}, Vec4(0.0f)};
    return c;
}

void testZeroTangents() {
    std::printf("tangentes zero: smoothstep entre as chaves\n");
    SkinRig::Channel c = channel(0, Interp::Cubic, {1.0f, 3.0f}, {Vec4(2.0f, 0.0f, -1.0f, 0.0f), Vec4(4.0f, 1.0f, 1.0f, 0.0f)});
    c.inTan.assign(2, Vec4(0.0f));
    c.outTan.assign(2, Vec4(0.0f));
    float worst = 0.0f;
    for (int i = 0; i <= 20; ++i) {
        const float s = static_cast<float>(i) / 20.0f;
        const Vec4 want = glm::mix(c.values[0], c.values[1], smoothstep01(s));
        worst = std::fmax(worst, maxDiff(SkinRig::sampleChannel(c, 1.0f + 2.0f * s), want));
    }
    std::printf("    maior erro %.3g\n", static_cast<double>(worst));
    check(worst < 1e-5f, "21 tempos iguais ao smoothstep");
}

void testSlopeTangentsMatchLinear() {
    std::printf("tangentes = inclinacao da reta: igual ao LINEAR\n");
    const std::vector<float> times = {0.0f, 0.5f, 2.0f};
    const std::vector<Vec4> values = {Vec4(0.0f, 1.0f, 2.0f, 0.0f), Vec4(1.0f, -1.0f, 2.5f, 0.0f), Vec4(4.0f, 2.0f, 0.0f, 0.0f)};
    const SkinRig::Channel lin = channel(0, Interp::Linear, times, values);
    SkinRig::Channel cub = channel(0, Interp::Cubic, times, values);
    // Na chave do meio a tangente de entrada é a do segmento anterior e a de
    // saída a do seguinte: cada trecho vira uma reta.
    const Vec4 slope0 = (values[1] - values[0]) / (times[1] - times[0]);
    const Vec4 slope1 = (values[2] - values[1]) / (times[2] - times[1]);
    cub.inTan = {slope0, slope0, slope1};
    cub.outTan = {slope0, slope1, slope1};
    float worst = 0.0f;
    for (int i = -2; i <= 44; ++i) {
        const float t = static_cast<float>(i) * 0.05f;
        worst = std::fmax(worst, maxDiff(SkinRig::sampleChannel(cub, t), SkinRig::sampleChannel(lin, t)));
    }
    std::printf("    maior erro %.3g\n", static_cast<double>(worst));
    check(worst < 1e-5f, "47 tempos (com as bordas) a menos de 1e-5");
}

void testCubicRotationUnit() {
    std::printf("rotacao cubica: sempre unitaria\n");
    // Tangentes grandes de propósito: o Hermite cru sai bem longe do círculo
    // unitário, e só a normalização o traz de volta.
    SkinRig::Channel c = channel(1, Interp::Cubic, {0.0f, 1.0f, 2.0f},
                                 {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.70710678f, 0.0f, 0.70710678f}, {1.0f, 0.0f, 0.0f, 0.0f}});
    c.inTan = {Vec4(0.0f), {3.0f, -2.0f, 1.0f, 4.0f}, {-1.0f, 2.0f, 2.0f, -3.0f}};
    c.outTan = {{2.0f, 1.0f, -3.0f, 0.5f}, {-4.0f, 0.0f, 1.0f, 2.0f}, Vec4(0.0f)};
    float worst = 0.0f;
    for (int i = 1; i < 200; ++i) {
        const float t = static_cast<float>(i) * 0.01f;
        worst = std::fmax(worst, std::fabs(glm::length(SkinRig::sampleChannel(c, t)) - 1.0f));
    }
    std::printf("    maior |len - 1| %.3g\n", static_cast<double>(worst));
    check(worst < 1e-5f, "199 tempos com norma 1");
}

void testKeyTimes() {
    std::printf("tempos exatos das chaves e bordas\n");
    const SkinRig::Channel c = referencePosition();
    bool ok = true;
    for (size_t k = 0; k < c.times.size(); ++k) ok = ok && maxDiff(SkinRig::sampleChannel(c, c.times[k]), c.values[k]) < 1e-6f;
    check(ok, "cada chave devolve o proprio valor");
    check(SkinRig::sampleChannel(c, -1.0f) == c.values.front(), "antes da primeira: valor da ponta");
    check(SkinRig::sampleChannel(c, 9.0f) == c.values.back(), "depois da ultima: valor da ponta (sem extrapolar)");
}

void testReferencePrototype() {
    std::printf("referencia do prototipo (anim::sampleClip)\n");
    // Valores impressos pelo protótipo com os mesmos canais; a rotação em
    // t = 2.2 já passou da última chave (2.0) e devolve a ponta.
    const SkinRig::Channel pos = referencePosition();
    const SkinRig::Channel rot = referenceRotation();
    const float times[3] = {0.3f, 1.7f, 2.2f};
    const Vec4 wantPos[3] = {{0.38925001f, 0.692499995f, -0.946250021f, 0.0f},
                             {1.16385186f, 0.548740745f, 1.59792566f, 0.0f},
                             {1.75200009f, 0.267999977f, -0.488000274f, 0.0f}};
    const Vec4 wantRot[3] = {{0.0841969252f, -0.0120575763f, 0.0297927577f, 0.995930731f},
                             {-0.0068664006f, 0.637065709f, 0.0508112423f, 0.769102275f},
                             {0.0f, 0.707106769f, 0.0f, 0.707106769f}};
    float worst = 0.0f;
    for (int i = 0; i < 3; ++i) {
        worst = std::fmax(worst, maxDiff(SkinRig::sampleChannel(pos, times[i]), wantPos[i]));
        worst = std::fmax(worst, maxDiff(SkinRig::sampleChannel(rot, times[i]), wantRot[i]));
    }
    std::printf("    maior erro %.3g\n", static_cast<double>(worst));
    check(worst < 1e-5f, "posicao e rotacao em 3 tempos");
}

void testStepAndLinearUnchanged() {
    std::printf("STEP e LINEAR como antes\n");
    const std::vector<float> times = {0.0f, 1.0f};
    const std::vector<Vec4> values = {Vec4(1.0f, 2.0f, 3.0f, 0.0f), Vec4(3.0f, 2.0f, -1.0f, 0.0f)};
    const SkinRig::Channel step = channel(0, Interp::Step, times, values);
    const SkinRig::Channel lin = channel(0, Interp::Linear, times, values);
    check(SkinRig::sampleChannel(step, 0.75f) == values[0], "STEP segura a chave de baixo");
    check(maxDiff(SkinRig::sampleChannel(lin, 0.25f), glm::mix(values[0], values[1], 0.25f)) < 1e-6f, "LINEAR mistura");

    const glm::quat a(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::quat b = glm::angleAxis(1.2f, glm::normalize(Vec3(0.3f, 1.0f, -0.2f)));
    const SkinRig::Channel rot = channel(1, Interp::Linear, times, {Vec4(a.x, a.y, a.z, a.w), Vec4(b.x, b.y, b.z, b.w)});
    const glm::quat q = glm::slerp(a, b, 0.4f);
    check(maxDiff(SkinRig::sampleChannel(rot, 0.4f), Vec4(q.x, q.y, q.z, q.w)) < 1e-6f, "rotacao LINEAR por slerp");

    // Cubic sem tangentes (canal montado à mão ou truncado) cai no LINEAR.
    const SkinRig::Channel bare = channel(0, Interp::Cubic, times, values);
    check(SkinRig::sampleChannel(bare, 0.25f) == SkinRig::sampleChannel(lin, 0.25f), "Cubic sem tangentes = LINEAR");
}

void testPoseUsesCubic() {
    std::printf("pose() usa a cubica\n");
    SkinRig rig;
    rig.nodes.resize(1);
    rig.order = {0};
    SkinRig::Animation an;
    an.name = "anda";
    an.duration = 2.5f;
    an.channels.push_back(referencePosition());
    rig.animations.push_back(an);
    std::vector<glm::mat4> globals;
    rig.pose(0, 1.7f, globals);
    const Vec4 want = SkinRig::sampleChannel(rig.animations[0].channels[0], 1.7f);
    check(globals.size() == 1 && maxDiff(Vec4(Vec3(globals[0][3]), 0.0f), want) < 1e-6f, "translacao do no = canal");
}

void testMorphCubic() {
    std::printf("pesos de morph cubicos\n");
    SkinRig rig;
    rig.nodes.resize(1);
    rig.order = {0};
    SkinRig::Channel c;
    c.node = 0;
    c.path = 3;
    c.interp = Interp::Cubic;
    c.times = {0.0f, 2.0f};
    c.weightCount = 2;
    c.weights = {0.0f, 1.0f, 1.0f, 0.5f};
    c.weightsIn = {0.0f, 0.0f, 0.0f, 0.0f};
    c.weightsOut = {0.0f, 0.0f, 0.0f, 0.0f};
    SkinRig::Animation an;
    an.duration = 2.0f;
    an.channels.push_back(c);
    rig.animations.push_back(an);

    float w[3] = {};
    float worst = 0.0f;
    for (int i = 0; i <= 10; ++i) {
        const float s = static_cast<float>(i) / 10.0f;
        rig.morphWeights(0, 2.0f * s, 0, w, 3);
        const float k = smoothstep01(s);
        worst = std::fmax(worst, std::fabs(w[0] - k));
        worst = std::fmax(worst, std::fabs(w[1] - (1.0f - 0.5f * k)));
        worst = std::fmax(worst, std::fabs(w[2]));
    }
    std::printf("    tangentes zero, maior erro %.3g\n", static_cast<double>(worst));
    check(worst < 1e-5f, "tangentes zero: smoothstep (alvo extra = 0)");

    // Tangentes = inclinação: igual ao LINEAR.
    SkinRig::Channel& m = rig.animations[0].channels[0];
    m.weightsIn = {0.5f, -0.25f, 0.5f, -0.25f};
    m.weightsOut = m.weightsIn;
    worst = 0.0f;
    for (int i = -1; i <= 11; ++i) {
        const float t = static_cast<float>(i) * 0.2f;
        rig.morphWeights(0, t, 0, w, 2);
        const float s = std::fmin(std::fmax(t / 2.0f, 0.0f), 1.0f);
        worst = std::fmax(worst, std::fabs(w[0] - s));
        worst = std::fmax(worst, std::fabs(w[1] - (1.0f - 0.5f * s)));
    }
    std::printf("    tangentes = inclinacao, maior erro %.3g\n", static_cast<double>(worst));
    check(worst < 1e-5f, "tangentes = inclinacao: LINEAR, bordas na ponta");

    // Sem as tangentes: LINEAR.
    m.weightsIn.clear();
    m.weightsOut.clear();
    rig.morphWeights(0, 0.5f, 0, w, 2);
    check(std::fabs(w[0] - 0.25f) < 1e-6f && std::fabs(w[1] - 0.875f) < 1e-6f, "Cubic sem tangentes = LINEAR");

    m.interp = Interp::Step;
    rig.morphWeights(0, 1.5f, 0, w, 2);
    check(w[0] == 0.0f && w[1] == 1.0f, "STEP segura a chave de baixo");
}

} // namespace

int main() {
    std::printf("=== amostragem do SkinRig ===\n");
    testZeroTangents();
    testSlopeTangentsMatchLinear();
    testCubicRotationUnit();
    testKeyTimes();
    testReferencePrototype();
    testStepAndLinearUnchanged();
    testPoseUsesCubic();
    testMorphCubic();
    if (g_fail) {
        std::printf("FALHOU (%d)\n", g_fail);
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
