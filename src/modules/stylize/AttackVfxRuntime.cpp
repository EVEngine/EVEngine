#include "stylize/AttackVfxRuntime.h"

#include "common/Capability.h"
#include "stylize/AttackVfxLayerExecutor.h"

#include <cmath>
#include <optional>
#include <unordered_map>
#include <utility>

namespace eve::stylize {
namespace {

constexpr std::uint32_t kDefaultCapacity = 32;

Result<void> fail(DiagnosticCode code, std::string_view message, std::string_view path = {}) {
    return Result<void>::failure(Diagnostic::error(code, std::string(message), std::string(path)));
}

template <class T>
Result<T> failT(DiagnosticCode code, std::string_view message, std::string_view path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::string(message), std::string(path)));
}

IAttackVfxLayerExecutor* findExecutor(AttackVfxLayerRole role) {
    IAttackVfxLayerExecutor* found = nullptr;
    cap::forEach<IAttackVfxLayerExecutor>([&](IAttackVfxLayerExecutor* executor) {
        if (!found && executor && executor->role() == role) found = executor;
    });
    return found;
}

}  // namespace

struct AttackVfxRuntime::Impl {
    struct LiveLayer {
        AttackVfxLayerRole    role = AttackVfxLayerRole::Particles;
        AttackVfxLayerHandle  handle{};
        AttackVfxStopBehavior stopBehavior = AttackVfxStopBehavior::StopEmitting;
        std::size_t           layerIndex   = 0;
        AttackVfxPhaseKind    phase        = AttackVfxPhaseKind::Release;
        std::size_t           phaseIndex   = 0;
    };

    struct LivePhase {
        std::vector<LiveLayer> layers;
    };

    struct Slot {
        std::uint32_t                         generation = 1;
        std::optional<AttackVfxInstanceState> state;
        std::vector<LivePhase>                phaseLayers;
        std::vector<LiveLayer>                draining;
    };

    std::vector<Slot>                                slots{kDefaultCapacity};
    std::unordered_map<std::string, AttackVfxRecipe> recipes;
    std::unordered_map<std::string, AttackVfxSkin>   skins;
    std::uint64_t                                    tickSerial = 0;

    Result<AttackVfxInstanceState*> resolve(AttackVfxHandle handle) {
        if (handle.slot >= slots.size())
            return failT<AttackVfxInstanceState*>(DiagnosticCode::NotFound, "attack VFX handle slot out of range",
                                                  "handle");
        auto& slot = slots[handle.slot];
        if (!slot.state || slot.generation != handle.generation)
            return failT<AttackVfxInstanceState*>(DiagnosticCode::StaleHandle, "stale attack VFX handle", "handle");
        return Result<AttackVfxInstanceState*>::success(&*slot.state);
    }

    const AttackVfxInstanceState* peek(AttackVfxHandle handle) const noexcept {
        if (handle.slot >= slots.size()) return nullptr;
        const auto& slot = slots[handle.slot];
        if (!slot.state || slot.generation != handle.generation) return nullptr;
        return &*slot.state;
    }

    Result<LogicalId> resolveSkinId(const AttackVfxRecipe& recipe, const AttackVfxRequest& request) const {
        if (request.skinOverride) {
            if (!skins.contains(request.skinOverride->format()))
                return failT<LogicalId>(DiagnosticCode::NotFound, "skin override is not registered", "skinOverride");
            return Result<LogicalId>::success(*request.skinOverride);
        }
        if (recipe.skinId) {
            if (!skins.contains(recipe.skinId->format()) && !recipe.skin)
                return failT<LogicalId>(DiagnosticCode::NotFound, "recipe skinId is not registered", "skinId");
            return Result<LogicalId>::success(*recipe.skinId);
        }
        return Result<LogicalId>::success(LogicalId{});
    }

    const AttackVfxSkin* skinFor(const AttackVfxInstanceState& state) const {
        if (!state.activeSkinId) return nullptr;
        const auto it = skins.find(state.activeSkinId->format());
        return it == skins.end() ? nullptr : &it->second;
    }

    AttackVfxLayerStartRequest makeLayerRequest(const AttackVfxInstanceState& state, AttackVfxPhaseKind phase,
                                                std::size_t phaseIndex, std::size_t layerIndex,
                                                const AttackVfxLayer* layer, const AttackVfxSkin* skin) const {
        AttackVfxLayerStartRequest request;
        request.instance     = state.handle;
        request.phase        = phase;
        request.phaseIndex   = phaseIndex;
        request.layerIndex   = layerIndex;
        request.layer        = layer;
        request.skin         = skin;
        request.playRequest  = &state.request;
        request.tickSerial   = tickSerial;
        return request;
    }

    Result<void> startOneLayer(Slot& slot, std::size_t phaseIndex, const AttackVfxPhase& authored,
                               const AttackVfxLayer& layer, std::size_t layerIndex, const AttackVfxSkin* skin,
                               AttackVfxFrame& frame) {
        auto& state = *slot.state;
        AttackVfxFrameEvent event;
        event.handle     = state.handle;
        event.phase      = authored.kind;
        event.role       = layer.role;
        event.layerIndex = layerIndex;
        event.layerCount = authored.layers.size();

        auto* executor = findExecutor(layer.role);
        if (!executor) {
            event.kind = AttackVfxFrameEvent::Kind::LayerSkipped;
            frame.events.push_back(std::move(event));
            return Result<void>::success();
        }
        auto request = makeLayerRequest(state, authored.kind, phaseIndex, layerIndex, &layer, skin);
        auto started = executor->start(request);
        if (!started) {
            const auto code = started.code();
            if (code == StatusCode::NotFound || code == StatusCode::Unsupported) {
                event.kind = AttackVfxFrameEvent::Kind::LayerSkipped;
                frame.events.push_back(std::move(event));
                return Result<void>::success();
            }
            return Result<void>::failure(started.status());
        }
        LiveLayer live;
        live.role         = layer.role;
        live.handle       = std::move(started).takeValue();
        live.stopBehavior = layer.stopBehavior;
        live.layerIndex   = layerIndex;
        live.phase        = authored.kind;
        live.phaseIndex   = phaseIndex;
        slot.phaseLayers.at(phaseIndex).layers.push_back(std::move(live));
        event.kind = AttackVfxFrameEvent::Kind::LayerStarted;
        frame.events.push_back(std::move(event));
        return Result<void>::success();
    }

    Result<void> startPhaseLayers(Slot& slot, std::size_t phaseIndex, AttackVfxFrame& frame) {
        auto& state = *slot.state;
        const auto recipeIt = recipes.find(state.recipeId.format());
        if (recipeIt == recipes.end())
            return fail(DiagnosticCode::NotFound, "recipe missing while starting layers", "recipeId");
        const auto& recipe = recipeIt->second;
        const auto& authored = recipe.phases.at(phaseIndex);
        auto& livePhase = slot.phaseLayers.at(phaseIndex);
        livePhase.layers.clear();
        const AttackVfxSkin* skin = skinFor(state);

        for (std::size_t layerIndex = 0; layerIndex < authored.layers.size(); ++layerIndex) {
            auto started = startOneLayer(slot, phaseIndex, authored, authored.layers[layerIndex], layerIndex, skin,
                                         frame);
            if (!started) return started;
        }

        // Skin statusOverlayUri synthesizes a MeshVfx layer when Status phases omit it.
        if (authored.kind == AttackVfxPhaseKind::Status && skin && !skin->statusOverlayUri.empty()) {
            bool hasOverlay = false;
            for (const auto& layer : authored.layers) {
                if (layer.uri == skin->statusOverlayUri) {
                    hasOverlay = true;
                    break;
                }
            }
            if (!hasOverlay) {
                AttackVfxLayer overlay;
                overlay.role = AttackVfxLayerRole::MeshVfx;
                overlay.uri  = skin->statusOverlayUri;
                auto started =
                    startOneLayer(slot, phaseIndex, authored, overlay, authored.layers.size(), skin, frame);
                if (!started) return started;
            }
        }
        return Result<void>::success();
    }

    Result<void> stopLiveLayer(LiveLayer& live, AttackVfxStopBehavior behavior) {
        if (!live.handle.valid()) return Result<void>::success();
        auto* executor = findExecutor(live.role);
        if (!executor) {
            live.handle = {};
            return Result<void>::success();
        }
        auto stopped = executor->stop(live.handle, behavior);
        if (behavior == AttackVfxStopBehavior::ClearImmediately) live.handle = {};
        return stopped;
    }

    Result<void> stopPhaseLayers(Slot& slot, std::size_t phaseIndex,
                                 std::optional<AttackVfxStopBehavior> behaviorOverride = std::nullopt) {
        auto& livePhase = slot.phaseLayers.at(phaseIndex);
        Result<void> status = Result<void>::success();
        for (auto& live : livePhase.layers) {
            if (!live.handle.valid()) continue;
            const auto behavior = behaviorOverride.value_or(live.stopBehavior);
            auto stopped = stopLiveLayer(live, behavior);
            if (!stopped && status) status = Result<void>::failure(stopped.status());
            if (behavior == AttackVfxStopBehavior::StopEmitting && live.handle.valid()) {
                slot.draining.push_back(live);
            }
            live.handle = {};
        }
        livePhase.layers.clear();
        return status;
    }

    Result<void> clearDraining(Slot& slot, AttackVfxStopBehavior behavior) {
        Result<void> status = Result<void>::success();
        for (auto& live : slot.draining) {
            if (!live.handle.valid()) continue;
            auto stopped = stopLiveLayer(live, behavior);
            if (!stopped && status) status = Result<void>::failure(stopped.status());
            live.handle = {};
        }
        if (behavior == AttackVfxStopBehavior::ClearImmediately) slot.draining.clear();
        return status;
    }

    Result<void> updateLiveLayer(Slot& slot, LiveLayer& live, double dtSeconds) {
        if (!live.handle.valid() || !slot.state) return Result<void>::success();
        auto* executor = findExecutor(live.role);
        if (!executor) return Result<void>::success();

        const auto recipeIt = recipes.find(slot.state->recipeId.format());
        const AttackVfxLayer* layer = nullptr;
        if (recipeIt != recipes.end() && live.phaseIndex < recipeIt->second.phases.size()) {
            const auto& authored = recipeIt->second.phases[live.phaseIndex];
            if (live.layerIndex < authored.layers.size() &&
                authored.layers[live.layerIndex].role == live.role) {
                layer = &authored.layers[live.layerIndex];
            }
        }
        const AttackVfxSkin* skin = skinFor(*slot.state);
        auto request =
            makeLayerRequest(*slot.state, live.phase, live.phaseIndex, live.layerIndex, layer, skin);
        return executor->update(live.handle, dtSeconds, request);
    }

    Result<void> updatePhaseLayers(Slot& slot, std::size_t phaseIndex, double dtSeconds) {
        auto& livePhase = slot.phaseLayers.at(phaseIndex);
        for (auto& live : livePhase.layers) {
            if (!live.handle.valid()) continue;
            auto updated = updateLiveLayer(slot, live, dtSeconds);
            if (!updated) return updated;
        }
        return Result<void>::success();
    }

    Result<void> updateDraining(Slot& slot, double dtSeconds) {
        Result<void> status = Result<void>::success();
        for (auto it = slot.draining.begin(); it != slot.draining.end();) {
            if (!it->handle.valid()) {
                it = slot.draining.erase(it);
                continue;
            }
            auto* executor = findExecutor(it->role);
            if (!executor) {
                it = slot.draining.erase(it);
                continue;
            }
            AttackVfxLayerStartRequest request;
            request.instance   = slot.state ? slot.state->handle : AttackVfxHandle{};
            request.phase      = it->phase;
            request.phaseIndex = it->phaseIndex;
            request.layerIndex = it->layerIndex;
            request.tickSerial = tickSerial;
            if (slot.state) {
                request.skin        = skinFor(*slot.state);
                request.playRequest = &slot.state->request;
            }
            auto updated = executor->update(it->handle, dtSeconds, request);
            if (!updated) {
                // Rejected/NotFound means the executor finished residual lifetime.
                if (updated.code() == StatusCode::Rejected || updated.code() == StatusCode::NotFound ||
                    updated.code() == StatusCode::NoOp) {
                    it = slot.draining.erase(it);
                    continue;
                }
                if (status) status = Result<void>::failure(updated.status());
                ++it;
                continue;
            }
            if (updated.code() == StatusCode::NoOp) {
                it = slot.draining.erase(it);
                continue;
            }
            ++it;
        }
        return status;
    }

    Result<void> enterPhase(Slot& slot, std::size_t phaseIndex, AttackVfxFrame& frame) {
        auto& phase = slot.state->phases.at(phaseIndex);
        if (phase.active || phase.completed) return Result<void>::success();
        phase.armed     = false;
        phase.active    = true;
        phase.localTime = 0.0;
        AttackVfxFrameEvent event;
        event.kind       = AttackVfxFrameEvent::Kind::PhaseEnter;
        event.handle     = slot.state->handle;
        event.phase      = phase.kind;
        event.layerCount = phase.layerCount;
        frame.events.push_back(std::move(event));
        return startPhaseLayers(slot, phaseIndex, frame);
    }

    Result<void> exitPhase(Slot& slot, std::size_t phaseIndex, AttackVfxFrame& frame, std::string_view cue,
                           std::optional<AttackVfxStopBehavior> behaviorOverride = std::nullopt) {
        auto& phase = slot.state->phases.at(phaseIndex);
        if (!phase.active) return Result<void>::success();
        auto stopped = stopPhaseLayers(slot, phaseIndex, behaviorOverride);
        phase.active    = false;
        phase.completed = true;
        AttackVfxFrameEvent event;
        event.kind       = AttackVfxFrameEvent::Kind::PhaseExit;
        event.handle     = slot.state->handle;
        event.phase      = phase.kind;
        event.cue        = std::string(cue);
        event.layerCount = phase.layerCount;
        frame.events.push_back(std::move(event));
        return stopped;
    }

    void armTimedPhases(AttackVfxInstanceState& state) {
        const auto recipeIt = recipes.find(state.recipeId.format());
        if (recipeIt == recipes.end()) return;
        const auto& recipe = recipeIt->second;
        for (std::size_t i = 0; i < recipe.phases.size(); ++i) {
            const auto& authored = recipe.phases[i];
            auto&       live     = state.phases[i];
            if (live.active || live.completed || live.armed) continue;
            if (authored.startCue.empty()) live.armed = true;
        }
    }

    Result<void> applyCue(Slot& slot, std::string_view cue, AttackVfxFrame& frame) {
        auto& state = *slot.state;
        const auto recipeIt = recipes.find(state.recipeId.format());
        if (recipeIt == recipes.end()) return Result<void>::success();
        const auto& recipe = recipeIt->second;
        bool        consumed = false;
        for (std::size_t i = 0; i < recipe.phases.size(); ++i) {
            const auto& authored = recipe.phases[i];
            auto&       live     = state.phases[i];
            if (!live.completed && !live.active && authored.startCue == cue) {
                live.armed = true;
                if (authored.startOffsetSeconds <= 0.0) {
                    auto entered = enterPhase(slot, i, frame);
                    if (!entered) return entered;
                }
                consumed = true;
            }
            if (live.active && !authored.endCue.empty() && authored.endCue == cue) {
                auto exited = exitPhase(slot, i, frame, cue);
                if (!exited) return exited;
                consumed = true;
            }
        }
        AttackVfxFrameEvent event;
        event.kind   = consumed ? AttackVfxFrameEvent::Kind::CueConsumed : AttackVfxFrameEvent::Kind::CueIgnored;
        event.handle = state.handle;
        event.cue    = std::string(cue);
        frame.events.push_back(std::move(event));
        return Result<void>::success();
    }

    Result<void> finishInstance(std::size_t slotIndex, AttackVfxFrame& frame, std::string_view cue,
                                bool clearImmediately) {
        auto& slot = slots.at(slotIndex);
        if (!slot.state && slot.draining.empty()) return Result<void>::success();
        Result<void> status = Result<void>::success();
        const auto overrideBehavior =
            clearImmediately ? std::optional<AttackVfxStopBehavior>(AttackVfxStopBehavior::ClearImmediately)
                             : std::nullopt;
        if (slot.state) {
            for (std::size_t i = 0; i < slot.state->phases.size(); ++i) {
                if (slot.state->phases[i].active) {
                    auto exited = exitPhase(slot, i, frame, cue, overrideBehavior);
                    if (!exited && status) status = Result<void>::failure(exited.status());
                }
            }
        }
        if (clearImmediately) {
            auto cleared = clearDraining(slot, AttackVfxStopBehavior::ClearImmediately);
            if (!cleared && status) status = Result<void>::failure(cleared.status());
            slot.draining.clear();
        }
        // Free the playable identity immediately. StopEmitting residuals remain in
        // slot.draining and continue to receive updateDraining ticks without a live state.
        if (slot.state) {
            AttackVfxFrameEvent event;
            event.kind   = AttackVfxFrameEvent::Kind::InstanceStopped;
            event.handle = slot.state->handle;
            event.cue    = std::string(cue);
            frame.events.push_back(std::move(event));
            frame.stopped.push_back(slot.state->handle);
            slot.state.reset();
            slot.phaseLayers.clear();
            ++slot.generation;
            if (slot.generation == 0) slot.generation = 1;
        }
        return status;
    }

    void stopAllOccupied() {
        AttackVfxFrame frame;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (slots[i].state || !slots[i].draining.empty())
                (void)finishInstance(i, frame, "shutdown", true);
        }
    }
};

AttackVfxRuntime::AttackVfxRuntime() : impl_(std::make_unique<Impl>()) {}
AttackVfxRuntime::~AttackVfxRuntime() {
    // Best-effort teardown: never throw from a destructor.
    try {
        impl_->stopAllOccupied();
    } catch (...) {
    }
}

std::size_t AttackVfxRuntime::capacity() const noexcept { return impl_->slots.size(); }

Result<void> AttackVfxRuntime::configurePool(std::uint32_t capacity) {
    if (capacity == 0) return fail(DiagnosticCode::InvalidArgument, "pool capacity must be > 0", "capacity");
    for (const auto& slot : impl_->slots) {
        if (slot.state)
            return fail(DiagnosticCode::InvariantViolation, "cannot resize pool while instances are live",
                        "capacity");
    }
    AttackVfxFrame discard;
    for (std::size_t i = 0; i < impl_->slots.size(); ++i) {
        if (!impl_->slots[i].draining.empty())
            (void)impl_->finishInstance(i, discard, "resize", true);
    }
    // Bump generations so stale handles cannot revive after resize into a default slot.
    std::vector<Impl::Slot> next(capacity);
    for (std::size_t i = 0; i < capacity; ++i) {
        if (i < impl_->slots.size()) {
            next[i].generation = impl_->slots[i].generation + 1;
            if (next[i].generation == 0) next[i].generation = 1;
        } else {
            next[i].generation = 1;
        }
    }
    impl_->slots = std::move(next);
    return Result<void>::success();
}

Result<void> AttackVfxRuntime::registerRecipe(const AttackVfxRecipe& recipe) {
    auto valid = recipe.validate();
    if (!valid) return valid;
    const std::string key = recipe.id.format();
    for (const auto& slot : impl_->slots) {
        if (slot.state && slot.state->recipeId.format() == key)
            return fail(DiagnosticCode::Conflict, "cannot replace recipe while instances are live", "recipeId");
    }
    impl_->recipes[key] = recipe;
    if (recipe.skin) {
        auto skinStatus = registerSkin(*recipe.skin);
        if (!skinStatus) return skinStatus;
    }
    return Result<void>::success();
}

Result<void> AttackVfxRuntime::registerSkin(const AttackVfxSkin& skin) {
    auto valid = skin.validate();
    if (!valid) return valid;
    impl_->skins[skin.id.format()] = skin;
    return Result<void>::success();
}

std::optional<AttackVfxRecipe> AttackVfxRuntime::findRecipe(const LogicalId& id) const {
    const auto it = impl_->recipes.find(id.format());
    if (it == impl_->recipes.end()) return std::nullopt;
    return it->second;
}

std::optional<AttackVfxSkin> AttackVfxRuntime::findSkin(const LogicalId& id) const {
    const auto it = impl_->skins.find(id.format());
    if (it == impl_->skins.end()) return std::nullopt;
    return it->second;
}

Result<AttackVfxHandle> AttackVfxRuntime::play(const LogicalId& recipeId, const AttackVfxRequest& request) {
    const auto recipeIt = impl_->recipes.find(recipeId.format());
    if (recipeIt == impl_->recipes.end())
        return failT<AttackVfxHandle>(DiagnosticCode::NotFound, "recipe is not registered", "recipeId");
    const auto& recipe = recipeIt->second;

    auto skinId = impl_->resolveSkinId(recipe, request);
    if (!skinId) return Result<AttackVfxHandle>::failure(skinId.status());

    std::size_t freeSlot = impl_->slots.size();
    for (std::size_t i = 0; i < impl_->slots.size(); ++i) {
        if (!impl_->slots[i].state) {
            freeSlot = i;
            break;
        }
    }
    if (freeSlot == impl_->slots.size())
        return failT<AttackVfxHandle>(DiagnosticCode::Failed, "attack VFX pool is full", "pool");

    AttackVfxInstanceState state;
    state.handle.slot       = static_cast<std::uint32_t>(freeSlot);
    state.handle.generation = impl_->slots[freeSlot].generation;
    state.recipeId          = recipe.id;
    if (skinId.value().isValid()) state.activeSkinId = skinId.value();
    state.request = request;
    state.phases.reserve(recipe.phases.size());
    for (const auto& phase : recipe.phases) {
        AttackVfxPhaseState live;
        live.kind       = phase.kind;
        live.layerCount = phase.layers.size();
        state.phases.push_back(live);
    }

    auto& slot = impl_->slots[freeSlot];
    slot.state = std::move(state);
    slot.phaseLayers.assign(recipe.phases.size(), Impl::LivePhase{});
    impl_->armTimedPhases(*slot.state);

    AttackVfxFrame bootstrap;
    for (std::size_t i = 0; i < recipe.phases.size(); ++i) {
        if (slot.state->phases[i].armed && recipe.phases[i].startOffsetSeconds <= 0.0) {
            auto entered = impl_->enterPhase(slot, i, bootstrap);
            if (!entered) {
                AttackVfxFrame cleanup;
                (void)impl_->finishInstance(freeSlot, cleanup, "play-failed", true);
                return Result<AttackVfxHandle>::failure(entered.status());
            }
        }
    }
    (void)bootstrap;
    return Result<AttackVfxHandle>::success(slot.state->handle);
}

Result<AttackVfxFrame> AttackVfxRuntime::signal(AttackVfxHandle handle, std::string_view cue) {
    if (cue.empty())
        return failT<AttackVfxFrame>(DiagnosticCode::InvalidArgument, "cue must be non-empty", "cue");
    auto resolved = impl_->resolve(handle);
    if (!resolved) return Result<AttackVfxFrame>::failure(resolved.status());
    (void)std::move(resolved).takeValue();

    AttackVfxFrame frame;
    if (cue == "cancel") {
        auto finished = impl_->finishInstance(handle.slot, frame, cue, true);
        if (!finished) return Result<AttackVfxFrame>::failure(finished.status());
        return Result<AttackVfxFrame>::success(std::move(frame));
    }
    auto applied = impl_->applyCue(impl_->slots[handle.slot], cue, frame);
    if (!applied) return Result<AttackVfxFrame>::failure(applied.status());
    return Result<AttackVfxFrame>::success(std::move(frame));
}

Result<AttackVfxFrame> AttackVfxRuntime::advance(double dtSeconds) {
    if (!std::isfinite(dtSeconds) || dtSeconds < 0.0)
        return failT<AttackVfxFrame>(DiagnosticCode::InvalidArgument, "dtSeconds must be finite and >= 0", "dt");

    ++impl_->tickSerial;
    AttackVfxFrame frame;
    std::vector<std::size_t> toFinish;
    Result<void> firstError = Result<void>::success();

    for (std::size_t slotIndex = 0; slotIndex < impl_->slots.size(); ++slotIndex) {
        auto& slot = impl_->slots[slotIndex];
        if (!slot.state) {
            if (!slot.draining.empty()) {
                auto drained = impl_->updateDraining(slot, dtSeconds);
                if (!drained && firstError) firstError = Result<void>::failure(drained.status());
            }
            continue;
        }
        auto& state = *slot.state;
        state.age += dtSeconds;
        frame.advanced.push_back(state.handle);

        const auto recipeIt = impl_->recipes.find(state.recipeId.format());
        if (recipeIt == impl_->recipes.end()) {
            toFinish.push_back(slotIndex);
            continue;
        }
        const auto& recipe = recipeIt->second;

        for (std::size_t i = 0; i < recipe.phases.size(); ++i) {
            const auto& authored = recipe.phases[i];
            auto&       live     = state.phases[i];
            if (live.armed && !live.active && !live.completed) {
                bool shouldEnter = false;
                double phaseDt   = dtSeconds;
                if (authored.startCue.empty()) {
                    if (state.age + 1e-12 >= authored.startOffsetSeconds) {
                        shouldEnter = true;
                        phaseDt     = std::max(0.0, state.age - authored.startOffsetSeconds);
                        if (phaseDt > dtSeconds) phaseDt = dtSeconds;
                    }
                } else {
                    live.localTime += dtSeconds;
                    if (live.localTime + 1e-12 >= authored.startOffsetSeconds) {
                        shouldEnter = true;
                        phaseDt     = std::max(0.0, live.localTime - authored.startOffsetSeconds);
                        if (phaseDt > dtSeconds) phaseDt = dtSeconds;
                    }
                }
                if (shouldEnter) {
                    auto entered = impl_->enterPhase(slot, i, frame);
                    if (!entered) {
                        if (firstError) firstError = Result<void>::failure(entered.status());
                        continue;
                    }
                    // Apply only the post-start remainder so delayed phases do not expire early.
                    live.localTime = phaseDt;
                    auto updated = impl_->updatePhaseLayers(slot, i, phaseDt);
                    if (!updated && firstError) firstError = Result<void>::failure(updated.status());
                    if (authored.durationSeconds > 0.0 && live.localTime + 1e-12 >= authored.durationSeconds) {
                        auto exited = impl_->exitPhase(slot, i, frame, "duration");
                        if (!exited && firstError) firstError = Result<void>::failure(exited.status());
                    }
                    continue;
                }
            }
            if (live.active) {
                live.localTime += dtSeconds;
                auto updated = impl_->updatePhaseLayers(slot, i, dtSeconds);
                if (!updated && firstError) firstError = Result<void>::failure(updated.status());
                if (authored.durationSeconds > 0.0 && live.localTime + 1e-12 >= authored.durationSeconds) {
                    auto exited = impl_->exitPhase(slot, i, frame, "duration");
                    if (!exited && firstError) firstError = Result<void>::failure(exited.status());
                }
            }
        }

        // Pump StopEmitting residuals (including layers that exited this tick) once.
        auto drained = impl_->updateDraining(slot, dtSeconds);
        if (!drained && firstError) firstError = Result<void>::failure(drained.status());

        bool anyPending = false;
        for (const auto& live : state.phases) {
            if (!live.completed) {
                anyPending = true;
                break;
            }
        }
        if (!anyPending || state.stopping) toFinish.push_back(slotIndex);
    }

    for (std::size_t slotIndex : toFinish) {
        if (impl_->slots[slotIndex].state) {
            const bool stopped = impl_->slots[slotIndex].state->stopping;
            // Natural completion and StopEmitting honor authored layer stopBehavior (may drain).
            auto finished =
                impl_->finishInstance(slotIndex, frame, stopped ? "stop" : "complete", false);
            if (!finished && firstError) firstError = Result<void>::failure(finished.status());
        }
    }

    if (!firstError) return Result<AttackVfxFrame>::failure(firstError.status());
    return Result<AttackVfxFrame>::success(std::move(frame));
}

Result<AttackVfxFrame> AttackVfxRuntime::stop(AttackVfxHandle handle, AttackVfxStopMode mode) {
    auto resolved = impl_->resolve(handle);
    if (!resolved) return Result<AttackVfxFrame>::failure(resolved.status());
    auto* state = std::move(resolved).takeValue();
    AttackVfxFrame frame;
    if (mode == AttackVfxStopMode::StopEmitting) {
        state->stopping = true;
        return Result<AttackVfxFrame>::success(std::move(frame));
    }
    auto finished = impl_->finishInstance(handle.slot, frame, mode == AttackVfxStopMode::Cancel ? "cancel" : "clear",
                                          true);
    if (!finished) return Result<AttackVfxFrame>::failure(finished.status());
    return Result<AttackVfxFrame>::success(std::move(frame));
}

std::optional<AttackVfxInstanceState> AttackVfxRuntime::inspect(AttackVfxHandle handle) const {
    const auto* state = impl_->peek(handle);
    if (!state) return std::nullopt;
    return *state;
}

std::size_t AttackVfxRuntime::activeCount() const noexcept {
    std::size_t count = 0;
    for (const auto& slot : impl_->slots) {
        if (slot.state) ++count;
    }
    return count;
}

}  // namespace eve::stylize
