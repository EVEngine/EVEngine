#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>
#include "common/Capability.h"
#include "common/GpuInfo.h"
#include "common/GpuTimer.h"
#include "graphics/Graphics.h"
#include "graphics/multiscale/AngularView.h"
#include "graphics/multiscale/SphereView.h"
#include "spatial/multiscale/CellPosition.h"

namespace {
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(values.size()))) - 1];
}
void samples(const char* name, const std::vector<double>& values) {
    std::cout << ",\"" << name << "\":{\"samples\":" << values.size() << ",\"p50\":" << percentile(values, .5)
              << ",\"p95\":" << percentile(values, .95) << ",\"p99\":" << percentile(values, .99)
              << ",\"max\":" << percentile(values, 1) << '}';
}
}  // namespace

int runMultiscaleBenchmark(eve::graphics::Graphics& graphics, int frames, std::size_t count, int width, int height,
                           std::string_view mode) {
    using namespace eve::graphics::multiscale;
    if (count == 0 || count > 1000000 || frames < 1 || frames > 10000)
        throw std::invalid_argument("multiscale benchmark requires 1..1000000 items and 1..10000 frames");
    if (mode != "static" && mode != "rebuild" && mode != "update")
        throw std::invalid_argument("unknown multiscale benchmark mode");
    const bool rebuild      = mode != "static";
    const bool reuse        = mode == "update";
    const auto inputStarted = Clock::now();
    const auto side         = static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(count))));
    std::vector<eve::spatial::CellPosition> centers(count);
    std::vector<SphereAppearance>           appearances(count);
    for (std::size_t i = 0; i < count; ++i) {
        centers[i].cell[2]     = 1;
        centers[i].fraction[0] = -.45 + .9 * (static_cast<double>(i % side) + .5) / static_cast<double>(side);
        centers[i].fraction[1] = -.45 + .9 * (static_cast<double>(i / side) + .5) / static_cast<double>(side);
        appearances[i]         = {.25 / static_cast<double>(side), 900, {.1f, .1f, .8f}};
    }
    const double inputMs = elapsed(inputStarted);
    AngularView  view;
    view.aspect       = static_cast<double>(width) / height;
    view.cellExponent = 900;
    std::vector<double>          cpuPrepareMs, uploadMs, wallMs, gpuMs;
    std::unique_ptr<SphereBatch> batch;
    auto                         prepare = [&]() {
        const auto started = Clock::now();
        auto       frame   = SphereFrame::build(centers, appearances, {}, view, count);
        if (!frame.ok()) throw std::runtime_error(frame.status().describe());
        const double cpu           = elapsed(started);
        const auto   uploadStarted = Clock::now();
        if (reuse && batch) {
            auto updated = batch->update(frame.value());
            if (!updated.ok()) throw std::runtime_error(updated.status().describe());
        } else {
            auto uploaded = SphereBatch::prepare(graphics, frame.value());
            if (!uploaded.ok()) throw std::runtime_error(uploaded.status().describe());
            batch = std::move(uploaded).takeValue();
        }
        return std::array<double, 2>{cpu, elapsed(uploadStarted)};
    };
    const auto cold = prepare();
    auto       draw = [&]() {
        auto drawn = batch->draw();
        if (!drawn.ok()) throw std::runtime_error(drawn.status().describe());
        graphics.present();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
        }
    };
    constexpr int warmup = 4;
    for (int i = 0; i < warmup; ++i) draw();
    // Resolve the known timing provider once, outside measured frame loops.
    auto* timer = eve::cap::query<eve::service::IGpuTimer>();
    for (int i = 0; i < frames; ++i) {
        const auto start = Clock::now();
        if (rebuild) {
            // Every record moves; retain all independent input records and avoid culling.
            const double step = i % 2 == 0 ? .0001 : -.0001;
            for (auto& center : centers) center.fraction[0] += step;
            const auto costs = prepare();
            cpuPrepareMs.push_back(costs[0]);
            uploadMs.push_back(costs[1]);
        }
        draw();
        wallMs.push_back(elapsed(start));
        if (timer && timer->gpuTimingAvailable()) gpuMs.push_back(timer->gpuFrameMs());
    }
    const auto*       gpu     = eve::cap::query<eve::caps::IGpuInfo>();
    const std::string gpuName = gpu && gpu->gpuReady() ? gpu->gpuName() : "unknown";
    std::cout << "MULTISCALE_BENCH {\"schema\":\"eve.multiscale-render-benchmark/v1\",\"mode\":\"" << mode
              << "\",\"items\":" << count << ",\"width\":" << width << ",\"height\":" << height
              << ",\"warmupFrames\":" << warmup << ",\"inputMs\":" << inputMs << ",\"coldCpuPrepareMs\":" << cold[0]
              << ",\"coldUploadMs\":" << cold[1] << ",\"recordAndMatrixBytes\":" << count * 128
              << ",\"pixelWidth\":" << graphics.getPixelWidth() << ",\"pixelHeight\":" << graphics.getPixelHeight()
              << ",\"gpu\":" << std::quoted(gpuName) << ",\"wholeWorldAcceptance\":false";
    samples("cpuPrepareMs", cpuPrepareMs);
    samples("uploadAndRetireMs", uploadMs);
    samples("cpuLoopWallMs", wallMs);
    samples("delayedGraphicsQueueMs", gpuMs);
    std::cout << "}\n";
    return 0;
}
