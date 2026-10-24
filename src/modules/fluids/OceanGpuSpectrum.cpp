#include "fluids/OceanGpuSpectrum.h"

#include "common/Subscription.h"
#include "fluids/OceanSeaTransition.h"
#include "fluids/OceanSpectrumInternal.h"
#include "fluids/shaders/ocean_evolve_spv.inc"
#include "fluids/shaders/ocean_fft_spv.inc"
#include "fluids/shaders/ocean_pack_spv.inc"
#include "gpgpu/ComputeProgram.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/Sequence.h"
#include "graphics/Graphics.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <numbers>

namespace eve::fluids {
namespace {
constexpr double           tau = 2.0 * std::numbers::pi;
std::atomic<std::uint64_t> nextOwner{1};
Diagnostic                 diagnostic(DiagnosticCode code, const char* message) {
    return Diagnostic::error(code, message, "fluids.ocean.gpu");
}
}  // namespace
struct OceanGpuSpectrum::Impl {
    enum class State { Empty, Ready, Failed };
    struct Band {
        std::unique_ptr<gpgpu::GpuBuffer> coefficients, ping, pong, packed;
    };
    std::shared_ptr<const OceanSpectrumData> data, target;
    double                                   start = 0.0, duration = 1.0;
    std::weak_ptr<const void>                lifetime;
    std::unique_ptr<gpgpu::ComputeShader>    evolve, fft, pack;
    std::unique_ptr<gpgpu::Sequence>         sequence;
    std::vector<Band>                        bands;
    std::unique_ptr<gpgpu::GpuBuffer>        atlas;
    OceanGpuAtlasView                        atlasLayout;
    std::uint64_t                            owner = nextOwner.fetch_add(1), revision = 0;
    double                                   anchor = std::numeric_limits<double>::quiet_NaN();
    State                                    status = State::Empty;
    Subscription                             retirement;
    void                                     release() {
        status = State::Empty;
        ++revision;
        sequence.reset();
        evolve.reset();
        fft.reset();
        pack.reset();
        bands.clear();
        atlas.reset();
    }
    ~Impl() {
        retirement.dispose();
        release();
    }
};
OceanGpuSpectrum::OceanGpuSpectrum() : impl_(std::make_unique<Impl>()) {}
OceanGpuSpectrum::~OceanGpuSpectrum() = default;
OceanGpuSpectrumBuilder::OceanGpuSpectrumBuilder(const OceanSpectrum& spectrum, graphics::Graphics& graphics,
                                                 gpgpu::Gpgpu& compute)
    : data_(spectrum.data_), graphics_(graphics), compute_(compute) {}

OceanGpuSpectrumBuilder::OceanGpuSpectrumBuilder(const OceanSeaTransition& transition, graphics::Graphics& graphics,
                                                 gpgpu::Gpgpu& compute)
    : data_(transition.from_.data_),
      target_(transition.to_.data_),
      start_(transition.start_),
      duration_(transition.duration_),
      graphics_(graphics),
      compute_(compute) {}

Result<std::unique_ptr<OceanGpuSpectrum>> OceanGpuSpectrumBuilder::build() const {
    using Output = Result<std::unique_ptr<OceanGpuSpectrum>>;
    if (graphics_.resourceLifetime().expired())
        return Output::failure(diagnostic(DiagnosticCode::StaleHandle, "Graphics provider retired"));
    if (graphics_.getBackendName() != "vulkan" || !compute_.isAvailable())
        return Output::failure(
            diagnostic(DiagnosticCode::Unsupported, "Ocean FFT requires an available Vulkan compute provider"));
    std::unique_ptr<OceanGpuSpectrum> result(new OceanGpuSpectrum);
    auto&                             state = *result->impl_;
    state.data                              = data_;
    state.target                            = target_;
    state.start                             = start_;
    state.duration                          = duration_;
    state.lifetime                          = graphics_.resourceLifetime();
    auto retiring                           = graphics_.onResourcesRetiring([target = &state]() { target->release(); });
    if (!retiring) return Output::failure(retiring.status());
    state.retirement = std::move(retiring.value());
    try {
        auto evolve = gpgpu::createComputeShader(
            std::vector<uint32_t>(ocean_evolve_spv, ocean_evolve_spv + ocean_evolve_spv_count));
        if (!evolve) return Output::failure(evolve.status());
        state.evolve = std::move(evolve.value());
        auto fft =
            gpgpu::createComputeShader(std::vector<uint32_t>(ocean_fft_spv, ocean_fft_spv + ocean_fft_spv_count));
        if (!fft) return Output::failure(fft.status());
        state.fft = std::move(fft.value());
        auto pack =
            gpgpu::createComputeShader(std::vector<uint32_t>(ocean_pack_spv, ocean_pack_spv + ocean_pack_spv_count));
        if (!pack) return Output::failure(pack.status());
        state.pack = std::move(pack.value());
        state.sequence.reset(compute_.newSequence());
        if (!state.sequence || !state.sequence->isAvailable())
            return Output::failure(diagnostic(DiagnosticCode::Unsupported, "Ocean GPU command sequence unavailable"));
        for (const auto& source : data_->bands) {
            OceanGpuSpectrum::Impl::Band band;
            const auto                   count = source.settings.resolution * source.settings.resolution;
            band.coefficients.reset(
                compute_.newBuffer(static_cast<int>(count * 8 * (target_ ? 2 : 1) * sizeof(float))));
            band.ping.reset(compute_.newBuffer(static_cast<int>(count * 12 * sizeof(float))));
            band.pong.reset(compute_.newBuffer(static_cast<int>(count * 12 * sizeof(float))));
            band.packed.reset(compute_.newBuffer(static_cast<int>(count * 6 * sizeof(float))));
            if (!band.coefficients || !band.ping || !band.pong || !band.packed)
                return Output::failure(diagnostic(DiagnosticCode::Unsupported, "Ocean GPU allocation unavailable"));
            state.bands.push_back(std::move(band));
            auto& layout                     = state.atlasLayout;
            layout.width                     = std::max(layout.width, source.settings.resolution);
            layout.bands[layout.bandCount++] = {source.settings.resolution, layout.height, source.settings.length};
            layout.height += source.settings.resolution * 3;
        }
        state.atlasLayout.seaLevel = data_->settings.seaLevel;
        state.atlas.reset(
            compute_.newBuffer(static_cast<int>(state.atlasLayout.width * state.atlasLayout.height * sizeof(float))));
        if (!state.atlas)
            return Output::failure(diagnostic(DiagnosticCode::Unsupported, "Ocean GPU atlas unavailable"));
    } catch (const std::exception& error) {
        return Output::failure(diagnostic(DiagnosticCode::Failed, error.what()));
    }
    return Output::success(std::move(result));
}
OceanGpuFrameBuilder    OceanGpuSpectrum::frameBuilder(double time) { return {*this, time}; }
OceanGpuReadbackBuilder OceanGpuSpectrum::readbackBuilder(const OceanGpuFrameToken& token) const {
    return {*this, token};
}

Result<OceanGpuFrameToken> OceanGpuFrameBuilder::submit() const {
    using Output = Result<OceanGpuFrameToken>;
    auto& state  = *owner_.impl_;
    if (state.lifetime.expired() || !state.sequence)
        return Output::failure(diagnostic(DiagnosticCode::StaleHandle, "Ocean GPU provider retired"));
    if (state.status == OceanGpuSpectrum::Impl::State::Failed)
        return Output::failure(
            diagnostic(DiagnosticCode::Failed, "Rebuild the ocean GPU projection after a backend failure"));
    if (!std::isfinite(time_) || std::abs(time_) > 1e9)
        return Output::failure(diagnostic(DiagnosticCode::InvalidArgument, "Invalid ocean GPU time"));
    ++state.revision;
    state.status = OceanGpuSpectrum::Impl::State::Failed;
    // Rebase phase in double once per 16 simulation seconds. The shader sees only a small
    // relative time, preventing float clocks from freezing or losing phase in long sessions.
    const double anchor     = std::floor(time_ / 16.0) * 16.0;
    const double u          = state.target ? std::clamp((time_ - state.start) / state.duration, 0.0, 1.0) : 0.0;
    const double weight     = u * u * u * (10.0 + u * (-15.0 + 6.0 * u));
    const double rate       = state.target ? 30.0 * u * u * (1.0 - u) * (1.0 - u) / state.duration : 0.0;
    const double levelDelta = state.target ? state.target->settings.seaLevel - state.data->settings.seaLevel : 0.0;
    try {
        auto& sequence = *state.sequence;
        sequence.begin();
        for (std::size_t bandIndex = 0; bandIndex < state.bands.size(); ++bandIndex) {
            const auto&    source = state.data->bands[bandIndex];
            auto&          band   = state.bands[bandIndex];
            const unsigned n = source.settings.resolution, count = n * n;
            const int      groups = static_cast<int>((count * 6 + 63) / 64);
            if (anchor != state.anchor) {
                std::vector<float> coefficients(count * 8 * (state.target ? 2 : 1));
                for (unsigned z = 0; z < n; ++z)
                    for (unsigned x = 0; x < n; ++x) {
                        const int        ix        = x < n / 2 ? int(x) : int(x) - int(n);
                        const int        iz        = z < n / 2 ? int(z) : int(z) - int(n);
                        const glm::dvec2 k         = glm::dvec2(ix, iz) * (tau / source.settings.length);
                        const double     omega     = std::sqrt(state.data->settings.gravity * glm::length(k));
                        const double     advection = glm::dot(k, state.data->settings.current);
                        const unsigned   i = z * n + x, opposite = ((n - z) % n) * n + (n - x) % n;
                        // Match CPU phase reduction before multiplication, including very large times.
                        const auto wind = std::polar(1.0, std::remainder(-omega * anchor, tau));
                        const auto flow = std::polar(1.0, std::remainder(-advection * anchor, tau));
                        const auto a    = source.coefficients[i] * wind * flow;
                        const auto b    = std::conj(source.coefficients[opposite]) * std::conj(wind) * flow;
                        const std::array<double, 8> values{a.real(), a.imag(), b.real(), b.imag(),
                                                           k.x,      k.y,      omega,    advection};
                        for (unsigned component = 0; component < 8; ++component)
                            coefficients[i * 8 + component] = static_cast<float>(values[component]);
                        if (state.target) {
                            const auto& target = state.target->bands[bandIndex];
                            const auto  ta     = target.coefficients[i] * wind * flow;
                            const auto  tb     = std::conj(target.coefficients[opposite]) * std::conj(wind) * flow;
                            const std::array<double, 8> next{ta.real(), ta.imag(), tb.real(), tb.imag(),
                                                             k.x,       k.y,       omega,     advection};
                            for (unsigned component = 0; component < 8; ++component)
                                coefficients[(count + i) * 8 + component] = static_cast<float>(next[component]);
                        }
                    }
                sequence.recordUpload(band.coefficients.get(), coefficients.data(),
                                      coefficients.size() * sizeof(float));
            }
            state.evolve->bindBuffer(0, band.coefficients.get());
            state.evolve->bindBuffer(1, band.ping.get());
            state.evolve->setFloat(0, static_cast<float>(n));
            state.evolve->setFloat(1, static_cast<float>(time_ - anchor));
            state.evolve->setFloat(2, static_cast<float>(state.data->settings.choppiness));
            state.evolve->setFloat(3, state.target ? 1.0F : 0.0F);
            state.evolve->setFloat(4, static_cast<float>(weight));
            state.evolve->setFloat(5, static_cast<float>(rate));
            state.evolve->setFloat(6, state.target ? static_cast<float>(state.target->settings.choppiness) : 0.0F);
            state.evolve->setFloat(7, bandIndex == 0 ? static_cast<float>(levelDelta * rate) : 0.0F);
            sequence.recordDispatch(state.evolve.get(), groups);
            auto* sourceBuffer = band.ping.get();
            auto* destination  = band.pong.get();
            for (unsigned axis = 0; axis < 2; ++axis)
                for (unsigned width = 2; width <= n; width *= 2) {
                    state.fft->bindBuffer(0, sourceBuffer);
                    state.fft->bindBuffer(1, destination);
                    state.fft->setFloat(0, static_cast<float>(n));
                    state.fft->setFloat(1, static_cast<float>(width));
                    state.fft->setFloat(2, static_cast<float>(axis));
                    sequence.recordDispatch(state.fft.get(), groups);
                    std::swap(sourceBuffer, destination);
                }
            state.pack->bindBuffer(0, sourceBuffer);
            state.pack->bindBuffer(1, band.packed.get());
            state.pack->bindBuffer(2, state.atlas.get());
            state.pack->setFloat(0, static_cast<float>(count * 6));
            state.pack->setFloat(1, static_cast<float>(n));
            state.pack->setFloat(2, static_cast<float>(state.atlasLayout.width));
            state.pack->setFloat(3, static_cast<float>(state.atlasLayout.bands[bandIndex].firstRow));
            sequence.recordDispatch(state.pack.get(), groups);
        }
        sequence.submit();
        if (sequence.getStatus() != gpgpu::SequenceStatus::Complete)
            return Output::failure(diagnostic(DiagnosticCode::Failed, "Ocean GPU submission did not complete"));
    } catch (const std::exception& error) {
        state.anchor = std::numeric_limits<double>::quiet_NaN();
        return Output::failure(diagnostic(DiagnosticCode::Failed, error.what()));
    }
    state.atlasLayout.seaLevel = state.data->settings.seaLevel + weight * levelDelta;
    state.anchor               = anchor;
    state.status               = OceanGpuSpectrum::Impl::State::Ready;
    OceanGpuFrameToken token;
    token.owner_    = state.owner;
    token.revision_ = state.revision;
    token.time_     = time_;
    return Output::success(token);
}
Result<OceanGpuBandView> OceanGpuSpectrum::band(const OceanGpuFrameToken& token, unsigned index) const {
    const auto& state = *impl_;
    if (state.lifetime.expired() || state.status != Impl::State::Ready || token.owner_ != state.owner ||
        token.revision_ != state.revision)
        return Result<OceanGpuBandView>::failure(diagnostic(DiagnosticCode::StaleHandle, "Ocean GPU frame is stale"));
    if (index >= state.bands.size())
        return Result<OceanGpuBandView>::failure(
            diagnostic(DiagnosticCode::InvalidArgument, "Ocean band index out of range"));
    const auto& settings = state.data->bands[index].settings;
    return Result<OceanGpuBandView>::success(
        {state.bands[index].packed->residentView(), settings.resolution, settings.length});
}
Result<OceanGpuAtlasView> OceanGpuSpectrum::atlas(const OceanGpuFrameToken& token) const {
    auto valid = band(token, 0);
    if (!valid) return Result<OceanGpuAtlasView>::failure(valid.status());
    auto view   = impl_->atlasLayout;
    view.buffer = impl_->atlas->residentView();
    return Result<OceanGpuAtlasView>::success(view);
}
Result<OceanSpectrumFrame> OceanGpuReadbackBuilder::build() const {
    const auto& state = *owner_.impl_;
    auto        valid = owner_.band(token_, 0);
    if (!valid) return Result<OceanSpectrumFrame>::failure(valid.status());
    OceanSpectrumFrame result;
    result.time_     = token_.time_;
    result.seaLevel_ = state.atlasLayout.seaLevel;
    result.current_  = state.data->settings.current;
    try {
        for (std::size_t b = 0; b < state.bands.size(); ++b) {
            const auto&        settings = state.data->bands[b].settings;
            const auto         count    = settings.resolution * settings.resolution;
            std::vector<float> values(count * 6);
            state.bands[b].packed->readFloat32s(values.data(), static_cast<int>(values.size()));
            if (!std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); }))
                return Result<OceanSpectrumFrame>::failure(
                    diagnostic(DiagnosticCode::Failed, "Ocean GPU output contains nonfinite values"));
            OceanSpectrumGrid grid;
            grid.resolution = settings.resolution;
            grid.length     = settings.length;
            grid.displacement.resize(count);
            grid.velocity.resize(count);
            for (unsigned i = 0; i < count; ++i) {
                grid.displacement[i] = {values[i], values[count + i], values[2 * count + i]};
                grid.velocity[i]     = {values[3 * count + i], values[4 * count + i], values[5 * count + i]};
            }
            result.grids_.push_back(std::move(grid));
        }
    } catch (const std::exception& error) {
        return Result<OceanSpectrumFrame>::failure(diagnostic(DiagnosticCode::Failed, error.what()));
    }
    return Result<OceanSpectrumFrame>::success(std::move(result));
}
}  // namespace eve::fluids
