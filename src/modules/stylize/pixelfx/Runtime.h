#pragma once
#include <optional>
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Painter.h"

namespace eve::stylize::pixelfx {
/** @brief Internal owning simulation behavior; trusted compiled kernels retain no external pointers. */
struct Kernel {
    virtual ~Kernel()                           = default;
    virtual void update(double delta)           = 0;
    virtual void drawBack(Paint& painter) const = 0;
    virtual void draw(Paint& painter) const     = 0;
    /** @brief Query source external-body policy at the supplied bounded playback time; no state retained. */
    virtual PixelFxActorVisibility actorVisibility(double) const { return PixelFxActorVisibility::Visible; }
};
/** @brief Immutable prepared analytic behavior; geometry is owned, settings/painter borrowed per draw.
 * @note Caller-thread affine, no external pointers or retained callbacks; draw never changes geometry. */
struct AnalyticKernel {
    /** @brief Release implementation-owned immutable geometry. */
    virtual ~AnalyticKernel() = default;
    /** @brief Draw the rear layer with synchronous target/settings borrows and supplied analytic time. */
    virtual void drawBack(Paint& painter, double time, const PixelFxSettings& settings) const = 0;
    /** @brief Draw the front layer with synchronous target/settings borrows and supplied analytic time. */
    virtual void draw(Paint& painter, double time, const PixelFxSettings& settings) const = 0;
    /** @brief Query external-body policy at the same bounded/wrapped time used for analytic drawing. */
    virtual PixelFxActorVisibility actorVisibility(double) const { return PixelFxActorVisibility::Visible; }
};
/** @brief Immutable cold recipe creating independent mutable kernels from shared immutable geometry.
 * @note Trusted owner-thread factories copy settings synchronously; kernels own geometry shares,
 * never recipe/settings pointers, so either recipe/kernel destruction order is supported. */
struct SimulationRecipe {
    /** @brief Release recipe ownership; existing kernels retain their immutable geometry. */
    virtual ~SimulationRecipe() = default;
    /** @brief Reset mutable simulation state while reusing geometry; no scripts or retained input borrows.
     * @param settings Synchronously borrowed normalized settings; copy all retained values.
     * @return Independent owning kernel retaining its own shares of immutable geometry.
     * @cost Linear in bounded active initial state per instance/seek, without map resampling or sorting. */
    virtual std::unique_ptr<Kernel> create(const PixelFxSettings& settings) const = 0;
};
/** @brief Immutable prepared inputs, shared only by an instance and pending seek candidates. */
struct Program {
    const Descriptor& descriptor;  // Borrowed immutable static definition, valid for process lifetime.
    PixelFxSettings   settings;
    std::unique_ptr<const AnalyticKernel>   analytic;
    std::unique_ptr<const SimulationRecipe> simulation;
};
/** @brief Prepare the sole immutable program from validated, normalized owning settings.
 * @note Caller-thread affine; static descriptor is borrowed for process lifetime. No scripts are invoked.
 * @return Owning shared program whose geometry outlives independently prepared playback states.
 * @cost Builds bounded source geometry once per cold configuration, amortized across frames and seeks. */
[[nodiscard]] std::shared_ptr<const Program> makeProgram(const Descriptor& descriptor, PixelFxSettings settings);
/** @brief Sole mutable timeline owner; fractional time is retained separately from active kernel ticks. */
struct PlaybackState {
    std::unique_ptr<Kernel> kernel;
    double                  time = 0, compensation = 0;
    uint64_t                ticks = 0;
    std::optional<double>   stopAt;
};
/** @brief Build private mutable playback state; prepared analytic/simulation geometry is reused.
 * @cost Creates simulation state once per instance/configuration/seek preparation; analytic state is constant-sized. */
std::unique_ptr<PlaybackState> makePlayback(const Program& program);
/** @brief Count all accepted fixed ticks, independently of done() or stop() status. */
uint64_t fixedTicks(double time);
/** @brief Replay to a validated target; caller owns the state exclusively, and no time is dropped.
 * @cost Linear in newly crossed active fixed ticks per advance/replay call. */
void advanceTo(PlaybackState& state, double time);
/** @brief Validate synchronous canvas/placement borrows without writes or publication; owner-thread only. */
[[nodiscard]] Result<void> validateCanvas(PixelFxCanvas canvas, PixelFxLayer layer, PixelFxPlacement placement);
/** @brief Dispatch one native effect layer at explicit source wrapper time; painter/optional kernel are borrowed.
 * @note Owner-thread only; no callbacks, retained target references or allocation introduced by dispatch.
 * Parent fade must not suppress invocation: source chain children replace their own painter fade. */
void drawProgramLayer(const Program& program, const Kernel* kernel, double seconds, Paint& painter, Paint::Layer layer);
}  // namespace eve::stylize::pixelfx
