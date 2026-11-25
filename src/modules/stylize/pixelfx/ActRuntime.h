// Native source Act orchestration and owning frame preparation.
#pragma once
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "stylize/PixelFx.h"
#include "stylize/pixelfx/Rig.h"
namespace eve::stylize::pixelfx {
/** @brief Owning hit notification captured at the source callback phase, before pose/target stepping. */
struct ActHitEvent {
    size_t index      = 0;
    double sourceTime = 0, actTime = 0, effectTime = 0;
};
/** @brief Owning source clock/pose observation; snapshots remain valid after runtime destruction. */
struct ActSnapshot {
    double             wallTime = 0, actTime = 0, effectTime = 0, freezeRemaining = 0, shakeRemaining = 0;
    bool               frozen = false, stopped = false, done = false, hasAct = false;
    uint64_t           loops = 0;
    std::array<int, 2> shake{};
    rig::AuthoredPose  pose;
};
/** @brief Process-local target tint identity; source hit data is resolved during cold preparation. */
enum class ActTargetSkin { None, Flash, Char, Ice, Shadow, Holy, Red, Shock, Poison };
/** @brief Owned source straw particle, in Act-local floor coordinates. */
struct ActStraw {
    double x = 0, y = 0, vx = 0, vy = 0, age = 0, life = 0;
    bool   big = false;
};
/** @brief Source target state; exclusively owned by its Act, with no entity/VM links. */
struct ActTargetState {
    double                x = 0, theta = 0, omega = 0, flashTime = 0, shiver = 0, clock = 0;
    ActTargetSkin         skin        = ActTargetSkin::None;
    uint32_t              randomState = 1;
    std::vector<ActStraw> straw;
};
/** @brief Authored source timing hit; absent skin differs from explicit None/null clearing. */
struct ActTimingHit {
    double                       time = 0, freeze = 0, shake = 0, amp = 0, push = 0;
    std::optional<ActTargetSkin> skin;
    std::optional<double>        at;
};
/** @brief Authored source pose key with effective values and exact defined-property membership. */
struct ActTimingPose {
    double            time = 0;
    rig::AuthoredPose pose;
    std::string       how;
};
/** @brief Immutable borrowed source choreography, independent of current simulation time.
 * @note Spans remain valid until the owning Act is destroyed; copying this view does not retain the owner.
 * Steps, stops and frame/bounds preparations do not invalidate the view. */
struct ActTimingView {
    double                         windup = 0, recovery = 0;
    bool                           onBody = false;
    std::span<const double>        targets;
    std::span<const ActTimingHit>  hits;
    std::span<const ActTimingPose> poses;
};
/** @brief Source default local box; caller placement/custom parameter extents are not included. */
struct ActBounds {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
/** @brief Opaque completed bounds preparation; its value survives Act/session destruction. */
class ActPreparedBounds {
public:
    /** @brief Read the captured four-scalar local box with no raster work or retained borrows. */
    [[nodiscard]] ActBounds bounds() const noexcept { return value_; }

private:
    ActBounds value_;
    explicit ActPreparedBounds(ActBounds value) : value_(value) {}
    friend class ActRuntime;
};
/** @brief Process-local counters for explicit render-session cache work, with no simulation meaning. */
struct ActRenderCacheStats {
    size_t   entries = 0;
    uint64_t hits = 0, rasterEvaluations = 0, clears = 0;
};
/** @brief Explicit shared owner of source posed-cache history for one caller-thread render session.
 * @note Retains only owning pose keys/pixels and scratch, never Acts, canvases or VM/entity pointers.
 * Each Act retains a strong share; the creator may release its handle before or after every Act.
 * Reproduce source-global cache behavior by sharing this owner across those Acts and preserving draw order. */
class ActRenderSession {
public:
    /** @brief Create an empty cache session; raster scratch is allocated by explicit frame preparation. */
    EVENGINE_API_WORLD ActRenderSession();
    /** @brief Release cache ownership after its last shared owner; captured frames remain independent. */
    EVENGINE_API_WORLD ~ActRenderSession();
    ActRenderSession(const ActRenderSession&)            = delete;
    ActRenderSession& operator=(const ActRenderSession&) = delete;
    /** @brief Read fixed counters without rasterization, mutation, allocation or callbacks; owner-thread only. */
    [[nodiscard]] EVENGINE_API_WORLD ActRenderCacheStats cacheStats() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
    friend class ActRuntime;
};
/** @brief Opaque owning raster snapshot, independent of the Act and input canvas lifetimes.
 * @note Process-local, owner-thread affine; preparation copies the background and captures one Act state. */
class ActPreparedFrame {
public:
    /** @brief Release captured raster ownership. */
    EVENGINE_API_WORLD ~ActPreparedFrame();
    /** @brief Transfer captured raster ownership; source may only be destroyed or assigned. */
    EVENGINE_API_WORLD ActPreparedFrame(ActPreparedFrame&&) noexcept;
    /** @brief Release the old snapshot and transfer captured ownership. */
    EVENGINE_API_WORLD ActPreparedFrame& operator=(ActPreparedFrame&&) noexcept;
    ActPreparedFrame(const ActPreparedFrame&)            = delete;
    ActPreparedFrame& operator=(const ActPreparedFrame&) = delete;
    /** @brief Copy both complete captured planes to a matching canvas, including the captured background.
     * @return Canvas/moved-token diagnostic before any writes, or success.
     * @note Synchronous writable borrow; no Act pointers, callbacks or simulation mutation.
     * @cost Linear plane copy per presentation, without pose rasterization or procedural effect work. */
    [[nodiscard]] EVENGINE_API_WORLD Result<void> copyTo(PixelFxCanvas canvas) const;

private:
    struct State;
    std::unique_ptr<State> state_;
    explicit ActPreparedFrame(std::unique_ptr<State> state);
    friend class ActRuntime;
};
class ActRuntime;
/** @brief Opaque exclusively owned cold Act preparation; no execution is possible before publication.
 * @note Owner-thread affine; moving transfers ownership, with no retained caller inputs or callbacks. */
class ActPreparedRuntime {
public:
    /** @brief Discard unconsumed prepared geometry/state without publication. */
    EVENGINE_API_WORLD ~ActPreparedRuntime();
    /** @brief Transfer prepared ownership; source remains valid for destruction/assignment only. */
    EVENGINE_API_WORLD ActPreparedRuntime(ActPreparedRuntime&&) noexcept;
    /** @brief Release existing preparation and transfer ownership. */
    EVENGINE_API_WORLD ActPreparedRuntime& operator=(ActPreparedRuntime&&) noexcept;
    ActPreparedRuntime(const ActPreparedRuntime&)            = delete;
    ActPreparedRuntime& operator=(const ActPreparedRuntime&) = delete;

private:
    std::unique_ptr<ActRuntime> runtime_;
    explicit ActPreparedRuntime(std::unique_ptr<ActRuntime> runtime);
    friend class ActRuntime;
};
/** @brief Explicit presentation: pristine posed stand-in, owned input pixels, or source body:false. */
enum class ActBodyPresentation { SourceRig, InputActor, EffectsOnly };
/** @brief One call-local replay operation; commands are applied in authored caller order. */
enum class ActReplayOperation { Step, Stop };
/** @brief Ephemeral replay input; Stop requires zero delta, Step uses the original raw update delta. */
struct ActReplayCommand {
    ActReplayOperation operation = ActReplayOperation::Step;
    double             delta     = 0;
};
/** @brief Internal caller-thread-affine source Act owner; typed preparation shares canonical native effect programs.
 * @note No VM/entity pointers, locks or retained caller borrows. Owning frame preparation composes the body;
 * drawEffectLayers intentionally exposes only the complete source effect layers for clock/source verification. */
class ActRuntime {
public:
    /** @brief Cold-prepare an owning source Act in an isolated render session from copied settings and authored
     * metadata.
     * @return Opaque preparation or structured field diagnostic; no state is published on failure.
     * @note Allocation failure may throw; caller-thread affine, no scripts or external callbacks.
     * @cost Compiles bounded track/geometry once per instance; reuse across step/draw calls. */
    [[nodiscard]] EVENGINE_API_WORLD static Result<ActPreparedRuntime> prepare(PixelFxKind            kind,
                                                                               const PixelFxSettings& settings);
    /** @brief Cold-prepare in an explicitly shared render session, using the same sole preparation path.
     * @return Opaque owning preparation, or diagnostic before publication; null sessions are rejected.
     * @note Caller-thread affine; session share is retained, inputs are copied, no callbacks or canvas borrows.
     * Cache history is shared; clocks, kernels, targets, dust and RNG remain independently owned by each Act.
     * @cost Same bounded track/geometry compilation per instance as isolated prepare; pose raster work belongs
     * to explicit prepareFrame and may be reused across Acts in this session. */
    [[nodiscard]] EVENGINE_API_WORLD static Result<ActPreparedRuntime> prepareInSession(
        PixelFxKind kind, const PixelFxSettings& settings, std::shared_ptr<ActRenderSession> session);
    /** @brief Consume a prepared token and transfer the ready runtime without geometry/raster work.
     * @return Owning active runtime or consumed-token diagnostic; rejected tokens publish no state.
     * @note Owner-thread only; no retained caller borrows or callbacks. */
    [[nodiscard]] EVENGINE_API_WORLD static Result<std::unique_ptr<ActRuntime>> publish(ActPreparedRuntime&& prepared);
    /** @brief Prepare independent state from this immutable recipe and an explicit update/stop history.
     * @param commands Synchronous borrowed commands, not retained; at most one million commands and effect substeps.
     * @return An owning unpublished replacement or diagnostic; this Act and render-cache history remain unchanged.
     * @note Owner-thread only, rejected during this Act's hit observer. Replays from initial state, not current state.
     * Raw deltas, zero updates, windup, hitstop, loops and stop ordering are preserved. No callbacks or rendering
     * occur; external callback consequences must be recorded by the caller. The existing shared render session is
     * retained, not rolled back. The prepared replacement survives destruction of the originating Act and command
     * storage.
     * @cost Recreates one mutable kernel and replays every supplied update; amortize at explicit timeline changes.
     * @throws std::bad_alloc If independent simulation state cannot be allocated. */
    [[nodiscard]] EVENGINE_API_WORLD Result<ActPreparedRuntime> prepareReplay(
        std::span<const ActReplayCommand> commands) const;
    /** @brief Release independent clock/kernel ownership; previous snapshots remain valid. */
    EVENGINE_API_WORLD ~ActRuntime();
    ActRuntime(const ActRuntime&)            = delete;
    ActRuntime& operator=(const ActRuntime&) = delete;
    /** @brief Apply one explicit source update delta in 0..1 seconds; replay requires the same injected delta sequence.
     * @return Success or delta diagnostic before any mutation; no simulation time is dropped or quantized.
     * @note Effect simulation alone partitions at120Hz or finer, including a partial zero-crossing delta.
     * Owner-thread only; this overload invokes no callbacks. A successful step invalidates the previous event view.
     * @cost Proportional to crossed effect substeps, hit emissions and active target particles per update. */
    [[nodiscard]] EVENGINE_API_WORLD Result<void> step(double delta);
    /** @brief Apply a source update with synchronous hit observation after target hit and before pose/target stepping.
     * @param observer Call-local callable; receives immutable authored hit data and the callback-phase timestamp.
     * @return Success or diagnostic before mutation for invalid delta/reentrant stepping.
     * @note Owner-thread only, no locks. Callable and arguments are borrowed only during invocation; copy to retain.
     * Observers may query this Act or stop its effect, but must not destroy it. Nested step calls are rejected.
     * Exceptions propagate with source-compatible partial progress; the throwing hit remains pending for retry.
     * @cost Same simulation work as step, plus one synchronous invocation per crossed hit. */
    [[nodiscard]] EVENGINE_API_WORLD Result<void> step(
        double delta, const std::function<void(const Value&, const ActHitEvent&)>& observer);
    /** @brief Stop the effect at its source clock; windup/freeze/pose updates remain source-driven. */
    EVENGINE_API_WORLD void stop() noexcept;
    /** @brief Copy the current source clocks, shake and effective pose; no allocation or callbacks. */
    [[nodiscard]] EVENGINE_API_WORLD ActSnapshot snapshot() const;
    /** @brief Borrow hit events from the last successful step until the next step or owner destruction. */
    [[nodiscard]] EVENGINE_API_WORLD std::span<const ActHitEvent> hitEvents() const noexcept;
    /** @brief Borrow immutable target and straw state until the next successful step or destruction.
     * @note Owner-thread only; no allocations, callbacks or persistent representation. */
    [[nodiscard]] EVENGINE_API_WORLD std::span<const ActTargetState> targetStates() const noexcept;
    /** @brief Borrow authored timing, or explicit absence for an effect with no source Act; no allocations/callbacks.
     * @note Owner-thread only; view survives all updates but not Act destruction. */
    [[nodiscard]] EVENGINE_API_WORLD std::optional<ActTimingView> timing() const noexcept;
    /** @brief Prepare source default bounds by sampling authored poses at12Hz, exact keys and target margins.
     * @return Opaque complete local box or raster diagnostic, with no partially published bounds token.
     * @note Owner-thread only; uses this Act's render session, so first-winner cache history changes as in source.
     * No simulation clock/RNG mutation, callbacks or retained input references. Frame replay includes bounds calls.
     * @cost Up to one pose raster per distinct sampled source cache key; source scratch/cache are reused.
     * @throws std::bad_alloc If pose raster/cache storage cannot be allocated. */
    [[nodiscard]] EVENGINE_API_WORLD Result<ActPreparedBounds> prepareBounds();
    /** @brief Copy immutable effect metadata; its ID string has process lifetime. */
    [[nodiscard]] EVENGINE_API_WORLD PixelFxInfo info() const;
    /** @brief Draw source effect back/front layers without body/targets, borrowing writable canvas only for this call.
     * @return Canvas/placement diagnostic before writes, or success; target contents are preserved outside effect
     * writes.
     * @note Source fade never suppresses child dispatch; no state, RNG or event mutation. Owner-thread only.
     * @cost Raster work scales with visible effect pixels per draw; all recipes/geometry are reused. */
    [[nodiscard]] EVENGINE_API_WORLD Result<void> drawEffectLayers(PixelFxCanvas canvas, PixelFxLayer layer,
                                                                   PixelFxPlacement placement) const;

    /** @brief Compose source body:false picture, with optional targets and straw between/after effect layers.
     * @return Canvas/placement diagnostic before writes or success; existing pixels outside writes remain intact.
     * @note Owner-thread only, synchronous canvas borrow; no state/RNG mutation, allocation or callbacks.
     * Target placement mirrors source direct writes; angle affects effect layers only.
     * @cost Visible effect raster work plus bounded target cells and live straw per draw; prepared art is reused. */
    [[nodiscard]] EVENGINE_API_WORLD Result<void> drawWithoutBody(PixelFxCanvas canvas, PixelFxPlacement placement,
                                                                  bool showTargets) const;

    /** @brief Prepare an owning source body:true picture over a copied background, without modifying the input.
     * @return Opaque complete raster snapshot or diagnostic; failures publish no partial frame.
     * @note Owner-thread only; input borrowed for this call. No callbacks, clock/RNG mutation or retained canvas/Act
     * pointers. Snapshots survive Act updates/destruction and include the background as captured at preparation.
     * @cost Pose/ghost raster evaluations and procedural full-frame composition per changed picture; scratch and a
     * bounded source-compatible pose cache are reused across preparations. Source cache identity retains the first
     * raster for each rounded, defined-property key; replay uses the same update/preparation sequence. Present the
     * result with copyTo.
     * @throws std::bad_alloc If owning frame planes or reusable scratch cannot be allocated. */
    [[nodiscard]] EVENGINE_API_WORLD Result<ActPreparedFrame> prepareFrame(PixelFxCanvas    background,
                                                                           PixelFxPlacement placement,
                                                                           bool             showTargets);
    /** @brief Prepare the same owning frame with an explicit body representation.
     * @note InputActor uses copied actor RGBA; mask remains independent effect input. Authored x/y translate the
     * sprite, lean/crouch shear its upper rows; no skeleton or limb binding is inferred for an arbitrary image.
     * Stock source parity belongs to SourceRig. InputActor respects visibility, front/back effects, ghosts and targets.
     * EffectsOnly omits the external body/shadow/ghost/dust, preserving source effect and optional target/straw passes.
     * Owner-thread only, no callbacks or retained caller borrows; completed frames survive all owners.
     * @return Owning frame or diagnostic before any background writes.
     * @cost Full-frame composition plus body/ghost pixels; scratch is reused across calls, present via copyTo. */
    [[nodiscard]] EVENGINE_API_WORLD Result<ActPreparedFrame> prepareFrame(PixelFxCanvas    background,
                                                                           PixelFxPlacement placement, bool showTargets,
                                                                           ActBodyPresentation body);


private:
    struct State;
    std::unique_ptr<State> state_;
    explicit ActRuntime(std::unique_ptr<State> state);
};
}  // namespace eve::stylize::pixelfx
