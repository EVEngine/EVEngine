// eve-hot-path
#include "stylize/pixelfx/Runtime.h"
#include <cmath>
#include "common/Assert.h"

namespace eve::stylize::pixelfx {
std::shared_ptr<const Program> makeProgram(const Descriptor& descriptor, PixelFxSettings settings) {
    [[maybe_unused]] const bool oneAnalyticRoute =
        !descriptor.prepareAnalytic ||
        (!descriptor.create && !descriptor.draw && !descriptor.drawBack && !descriptor.prepareSimulation);
    EV_ASSERT(oneAnalyticRoute, "Prepared analytic recipes must own their only drawing route");
    auto geometry = descriptor.prepareAnalytic ? descriptor.prepareAnalytic(settings) : nullptr;
    if (descriptor.prepareAnalytic) EV_ASSERT(bool(geometry), "Compiled analytic preparation must own geometry");
    [[maybe_unused]] const bool oneSimulationRoute =
        !descriptor.prepareSimulation ||
        (!descriptor.create && !descriptor.draw && !descriptor.drawBack && !descriptor.prepareAnalytic);
    EV_ASSERT(oneSimulationRoute, "Prepared simulation recipes must own their only construction route");
    auto simulation = descriptor.prepareSimulation ? descriptor.prepareSimulation(settings) : nullptr;
    if (descriptor.prepareSimulation) EV_ASSERT(bool(simulation), "Compiled simulation preparation must own a recipe");
    auto program = std::make_shared<const Program>(
        Program{descriptor, std::move(settings), std::move(geometry), std::move(simulation)});
    return program;
}
std::unique_ptr<PlaybackState> makePlayback(const Program& program) {
    auto state = std::make_unique<PlaybackState>();
    if (program.simulation || program.descriptor.create) {
        state->kernel = program.simulation ? program.simulation->create(program.settings)
                                           : program.descriptor.create(program.settings);
        EV_ASSERT(state->kernel != nullptr, "A compiled simulation factory must create an owned kernel");
    }
    return state;
}
uint64_t fixedTicks(double time) {
    // Same boundary tolerance as upstream's 120-Hz partitioner; at most 1e-9 of one tick.
    return uint64_t(std::floor(time * 120 + 1e-9));
}
void advanceTo(PlaybackState& state, double time) {
    const auto target = fixedTicks(time);
    if (state.kernel)
        while (state.ticks < target) {
            state.kernel->update(1.0 / 120);
            ++state.ticks;
        }
    else
        state.ticks = target;
    state.time = time;
}
namespace {
Diagnostic canvasDiagnostic(std::string message, std::string path) {
    return Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error, std::move(message), std::move(path));
}
}  // namespace
Result<void> validateCanvas(PixelFxCanvas canvas, PixelFxLayer layer, PixelFxPlacement placement) {
    if (canvas.width < 1 || canvas.width > 4096 || canvas.height < 1 || canvas.height > 4096 ||
        canvas.originX < -4096 || canvas.originX > 4096 || canvas.originY < -4096 || canvas.originY > 4096)
        return Result<void>::failure(canvasDiagnostic("Canvas dimensions/origin outside bounded range", "/canvas"));
    const size_t count = size_t(canvas.width) * size_t(canvas.height);
    if (canvas.color.size() != count || canvas.light.size() != count || !canvas.color.data() || !canvas.light.data())
        return Result<void>::failure(
            canvasDiagnostic("Canvas planes must contain exactly width*height pixels", "/canvas/planes"));
    const auto a     = reinterpret_cast<uintptr_t>(canvas.color.data()),
               b     = reinterpret_cast<uintptr_t>(canvas.light.data());
    const auto bytes = count * sizeof(uint32_t);
    if ((a <= b ? b - a : a - b) < bytes)
        return Result<void>::failure(canvasDiagnostic("Canvas planes must not overlap", "/canvas/planes"));
    if (layer != PixelFxLayer::Back && layer != PixelFxLayer::Front && layer != PixelFxLayer::Both)
        return Result<void>::failure(canvasDiagnostic("Unknown effect layer", "/layer"));
    if (!std::isfinite(placement.x) || !std::isfinite(placement.y) || !std::isfinite(placement.angle) ||
        std::abs(placement.x) > 4096 || std::abs(placement.y) > 4096 || std::abs(placement.angle) > 64 * PI)
        return Result<void>::failure(canvasDiagnostic("Placement is outside its finite bounds", "/placement"));
    return Result<void>::success();
}
void drawProgramLayer(const Program& program, const Kernel* kernel, double seconds, Paint& painter,
                      Paint::Layer layer) {
    const auto&  descriptor = program.descriptor;
    const double time       = descriptor.info.loop ? std::fmod(seconds, descriptor.info.duration)
                                                   : std::min(seconds, descriptor.info.duration);
    painter.layer           = layer;
    if (kernel) {
        if (layer == Paint::Layer::Back)
            kernel->drawBack(painter);
        else
            kernel->draw(painter);
    } else if (program.analytic) {
        if (layer == Paint::Layer::Back)
            program.analytic->drawBack(painter, time, program.settings);
        else
            program.analytic->draw(painter, time, program.settings);
    } else if (layer == Paint::Layer::Back) {
        if (descriptor.drawBack) descriptor.drawBack(painter, time, program.settings);
    } else if (descriptor.draw)
        descriptor.draw(painter, time, program.settings);
}
}  // namespace eve::stylize::pixelfx
