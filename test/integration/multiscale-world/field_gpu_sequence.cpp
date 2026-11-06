// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"default":"render-window","module":"physics_field"}
*/
// clang-format on
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#include "gpgpu/ComputeProgram.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/Sequence.h"
#include "graphics/Graphics.h"
#include "window/Window.h"
#include "zeroerr/unittest.h"

namespace {
using Row = std::array<float, 4>;
std::string shaderSource(const char* name) {
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    std::ifstream input(root / "src/modules/physics/field/gpu" / name, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
std::unique_ptr<eve::gpgpu::ComputeShader> shader(const char* name) {
    const auto source = shaderSource(name);
    REQUIRE(!source.empty());
    auto words = eve::gpgpu::compileComputeSpirv(source);
    REQUIRE(words.ok());
    auto program = eve::gpgpu::createComputeShader(words.value());
    REQUIRE(program.ok());
    return std::move(program).takeValue();
}
}  // namespace

TEST_CASE("physics.field.gpu.engineSequenceBuildsMovingFieldAndPublishesTogether") {
    auto* window = eve::window::Window::create();
    auto* graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width = 320;
    settings.height = 180;
    REQUIRE(window->setWindowSettings(settings));
    REQUIRE(graphics->getBackendName() == "vulkan");
    auto* compute = eve::gpgpu::Gpgpu::create();
    REQUIRE(compute->isAvailable());

    constexpr int count = 1024;
    constexpr int groups = count / 256;
    std::vector<Row> baseSources(count), initialTargets(count * 2);
    for (int i = 0; i < count; ++i) {
        const int cell = i & 7;
        const float jitter = (float((i * 37) % 101) / 100.f - .5f) * .02f;
        baseSources[i] = {((cell & 1) ? .5f : -.5f) + jitter,
                          ((cell & 2) ? .5f : -.5f) - jitter * .7f,
                          ((cell & 4) ? .5f : -.5f) + jitter * .5f, 1.f / count};
        initialTargets[i * 2] = {10.f + float(i % 101) * .001f, 0.f, .25f, 0.f};
    }
    std::array<std::unique_ptr<eve::gpgpu::GpuBuffer>, 2> sources, targets;
    for (auto& source : sources) source.reset(compute->newBuffer(count * int(sizeof(Row))));
    for (auto& target : targets) target.reset(compute->newBuffer(count * 2 * int(sizeof(Row))));
    std::unique_ptr<eve::gpgpu::GpuBuffer> partials(compute->newBuffer(groups * 8 * int(sizeof(Row))));
    std::unique_ptr<eve::gpgpu::GpuBuffer> summaries(compute->newBuffer(8 * int(sizeof(Row))));
    std::unique_ptr<eve::gpgpu::GpuBuffer> admission(compute->newBuffer(4));
    std::unique_ptr<eve::gpgpu::GpuBuffer> staging(compute->newBuffer(4, "staging"));
    for (const auto& source : sources) REQUIRE(source.get() != nullptr);
    for (const auto& target : targets) REQUIRE(target.get() != nullptr);
    REQUIRE(partials.get() != nullptr);
    REQUIRE(summaries.get() != nullptr);
    REQUIRE(admission.get() != nullptr);
    REQUIRE(staging.get() != nullptr);
    sources[0]->uploadBytes(baseSources.data(), baseSources.size() * sizeof(Row));
    targets[0]->uploadBytes(initialTargets.data(), initialTargets.size() * sizeof(Row));
    auto reduce = shader("reduce.comp"), final = shader("final.comp"), evaluate = shader("evaluate.comp");
    std::unique_ptr<eve::gpgpu::Sequence> sequence(compute->newSequence());
    REQUIRE(sequence.get() != nullptr);
    REQUIRE(sequence->isAvailable());
    for (auto* program : {reduce.get(), final.get(), evaluate.get()}) {
        program->setFloat(0, float(count));
        program->setFloat(1, float(groups));
        program->setFloat(2, 1.f / 60.f);
        program->setFloat(3, float(count));
        program->setFloat(4, .001f);
        program->setFloat(6, .04f);
    }
    reduce->bindBuffer(1, partials.get());
    final->bindBuffer(1, partials.get());
    final->bindBuffer(2, summaries.get());
    evaluate->bindBuffer(2, summaries.get());
    evaluate->bindBuffer(5, admission.get());

    for (int frame = 0; frame < 4; ++frame) {
        const int phase = frame & 1;
        const float shift = phase ? -.001f : .001f;
        reduce->setFloat(5, shift);
        reduce->bindBuffer(0, sources[phase].get());
        reduce->bindBuffer(6, sources[1 - phase].get());
        evaluate->bindBuffer(3, targets[phase].get());
        evaluate->bindBuffer(4, targets[1 - phase].get());
        const std::uint32_t clear = 0;
        sequence->begin();
        sequence->recordUpload(admission.get(), &clear, sizeof(clear));
        sequence->recordDispatch(reduce.get(), groups);
        sequence->recordDispatch(final.get(), 8);
        sequence->recordDispatch(evaluate.get(), groups);
        sequence->recordDownload(admission.get(), staging.get(), sizeof(clear));
        sequence->submit();
        std::uint32_t rejected = 1;
        staging->downloadBytes(&rejected, sizeof(rejected));
        REQUIRE(rejected == 0);
    }

    std::vector<Row> finalSources(count), finalTargets(count * 2);
    sources[0]->downloadBytes(finalSources.data(), finalSources.size() * sizeof(Row));
    targets[0]->downloadBytes(finalTargets.data(), finalTargets.size() * sizeof(Row));
    for (int i : {0, 1, 127, count - 1}) {
        CHECK(std::abs(finalSources[i][0] - baseSources[i][0]) < 1e-6f);
        CHECK(std::isfinite(finalTargets[i * 2][0]));
        CHECK(finalTargets[i * 2][0] < initialTargets[i * 2][0]);
    }
    std::array<double, 3> referencePosition{initialTargets[0][0], initialTargets[0][1], initialTargets[0][2]};
    std::array<double, 3> referenceVelocity{};
    for (int frame = 0; frame < 4; ++frame) {
        std::array<double, 3> acceleration{};
        for (int i = 0; i < count; ++i) {
            const auto& source = baseSources[i];
            const float displacement = (frame & 1) == 0 ? .001f : 0.f;
            const double x = source[0] + ((i & 1) == 0 ? displacement : -displacement);
            const std::array<double, 3> delta{x - referencePosition[0], source[1] - referencePosition[1],
                                               source[2] - referencePosition[2]};
            const double distance = std::hypot(delta[0], delta[1], delta[2]);
            const double factor = source[3] / (distance * distance * distance);
            for (int axis = 0; axis < 3; ++axis) acceleration[axis] += delta[axis] * factor;
        }
        for (int axis = 0; axis < 3; ++axis) {
            referenceVelocity[axis] += acceleration[axis] / 60.0;
            referencePosition[axis] += referenceVelocity[axis] / 60.0;
        }
    }
    for (int axis = 0; axis < 3; ++axis)
        CHECK(std::abs(double(finalTargets[0][axis]) - referencePosition[axis]) < 1e-5);

    // A near target must reject the entire candidate. The current source and
    // target buffers stay authoritative even though the candidate source writes.
    auto nearTargets = finalTargets;
    nearTargets[0] = {.5f, .5f, .5f, 0.f};
    targets[0]->uploadBytes(nearTargets.data(), nearTargets.size() * sizeof(Row));
    reduce->setFloat(5, .001f);
    reduce->bindBuffer(0, sources[0].get());
    reduce->bindBuffer(6, sources[1].get());
    evaluate->bindBuffer(3, targets[0].get());
    evaluate->bindBuffer(4, targets[1].get());
    const std::uint32_t clear = 0;
    sequence->begin();
    sequence->recordUpload(admission.get(), &clear, sizeof(clear));
    sequence->recordDispatch(reduce.get(), groups);
    sequence->recordDispatch(final.get(), 8);
    sequence->recordDispatch(evaluate.get(), groups);
    sequence->recordDownload(admission.get(), staging.get(), sizeof(clear));
    sequence->submit();
    std::uint32_t rejected = 0;
    staging->downloadBytes(&rejected, sizeof(rejected));
    REQUIRE(rejected == 1);
    Row publishedSource{}, candidateSource{}, publishedTarget{};
    sources[0]->downloadBytes(&publishedSource, sizeof(Row));
    sources[1]->downloadBytes(&candidateSource, sizeof(Row));
    targets[0]->downloadBytes(&publishedTarget, sizeof(Row));
    CHECK(publishedSource == finalSources[0]);
    CHECK(candidateSource != publishedSource);
    CHECK(publishedTarget == nearTargets[0]);
}
