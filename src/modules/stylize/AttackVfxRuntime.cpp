#include "stylize/AttackVfxRuntime.h"

#include "common/Capability.h"
#include "stylize/AttackVfxLayerExecutor.h"

#include <cmath>
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
        AttackVfxLayerRole   role = AttackVfxLayerRole::Particles;
        AttackVfxLayerHandle handle{};
        AttackVfxStopBehavior stopBehavior = AttackVfxStopBehavior::StopEmitting;
    };

    struct LivePhase {
        std::vector<LiveLayer> layers;
    };

    struct Slot {
        std::uint32_t                         generation = 1;
        std::optional<AttackVfxInstanceState> state;
        std::vector<LivePhase>                phaseLayers;
    };

    std::vector<Slot>                                slots{kDefaultCapacity};
    std::unordered_map<std::string, AttackVfxRecipe> recipes;
    std::unordered_map<std::string, AttackVfxSkin>   skins;

    Result<AttackVfxInstanceState*> resolve(AttackVfxHandle handle) {
        if (handle.slot >= slots.size())
            return failT<AttackVfxInstanceState*>(DiagnosticCode::NotFound, "attack VFX handle slot out of range",
                                                  "handle");
        auto& slot = slots[handle.slot];
        if (!slot.state || slot.generation != handle.generation)
            return failT<AttackVfxInstanceState*>(DiagnosticCode::StaleHandle, "stale attack VFX handle", "handle");
        return Result<AttackVfxInstanceState*>::success(&*slot.state);
    }

    const AttackVfxInstanceState* resolve(AttackVfxHandle handle) const {
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
            const auto& layer = authored.layers[layerIndex];
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
                continue;
            }
            AttackVfxLayerStartRequest request;
            request.instance    = state.handle;
            request.phase       = authored.kind;
            request.phaseIndex  = phaseIndex;
            request.layerIndex  = layerIndex;
            request.layer       = &layer;
            request.skin        = skin;
            request.playRequest = &state.request;
            auto started = executor->start(request);
            if (!started) {
                // Optional backends (missing Particles/camera sink, unsupported role
                // wiring) soft-skip so authored multi-layer recipes still play.
                const auto code = started.code();
                if (code == StatusCode::NotFound || code == StatusCode::Unsupported) {
                    event.kind = AttackVfxFrameEvent::Kind::LayerSkipped;
                    frame.events.push_back(std::move(event));
                    continue;
                }
                return Result<void>::failure(started.status());
            }
            livePhase.layers.push_back(
                LiveLayer{layer.role, std::move(started).takeValue(), layer.stopBehavior});
            event.kind = AttackVfxFrameEvent::Kind::LayerStarted;
            frame.events.push_back(std::move(event));
        }
        return Result<void>::success();
    }

    Result<void> stopPhaseLayers(Slot& slot, std::size_t phaseIndex) {
        auto& livePhase = slot.phaseLayers.at(phaseIndex);
        Result<void> status = Result<void>::success();
        for (auto& live : livePhase.layers) {
            if (!live.handle.valid()) continue;
            auto* executor = findExecutor(live.role);
            if (!executor) continue;
            auto stopped = executor->stop(live.handle, live.stopBehavior);
            if (!stopped && status) status = Result<void>::failure(stopped.status());
            live.handle = {};
        }
        livePhase.layers.clear();
        return status;
    }

    Result<void> updatePhaseLayers(Slot& slot, std::size_t phaseIndex, double dtSeconds) {
        auto& state = *slot.state;
        const auto recipeIt = recipes.find(state.recipeId.format());
        if (recipeIt == recipes.end()) return Result<void>::success();
        const auto& recipe   = recipeIt->second;
        const auto& authored = recipe.phases.at(phaseIndex);
        auto&       livePhase = slot.phaseLayers.at(phaseIndex);
        const AttackVfxSkin* skin = skinFor(state);
        for (auto& live : livePhase.layers) {
            if (!live.handle.valid()) continue;
            auto* executor = findExecutor(live.role);
            if (!executor) continue;
            AttackVfxLayerStartRequest request;
            request.instance    = state.handle;
            request.phase       = authored.kind;
            request.phaseIndex  = phaseIndex;
            request.layerIndex  = 0;
            request.layer       = nullptr;
            request.skin        = skin;
            request.playRequest = &state.request;
            // Recover layer pointer by matching role + handle order.
            for (std::size_t i = 0; i < authored.layers.size(); ++i) {
                if (authored.layers[i].role == live.role) {
                    request.layerIndex = i;
                    request.layer      = &authored.layers[i];
                    break;
                }
            }
            if (!request.layer) continue;
            auto updated = executor->update(live.handle, dtSeconds, request);
            if (!updated) return updated;
        }
        return Result<void>::success();
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

    Result<void> exitPhase(Slot& slot, std::size_t phaseIndex, AttackVfxFrame& frame, std::string_view cue) {
        auto& phase = slot.state->phases.at(phaseIndex);
        if (!phase.active) return Result<void>::success();
        auto stopped = stopPhaseLayers(slot, phaseIndex);
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

    Result<void> finishInstance(std::size_t slotIndex, AttackVfxFrame& frame, std::string_view cue) {
        auto& slot = slots.at(slotIndex);
        if (!slot.state) return Result<void>::success();
        Result<void> status = Result<void>::success();
        for (std::size_t i = 0; i < slot.state->phases.size(); ++i) {
            if (slot.state->phases[i].active) {
                auto exited = exitPhase(slot, i, frame, cue);
                if (!exited && status) status = Result<void>::failure(exited.status());
            }
        }
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
        return status;
    }
};

AttackVfxRuntime::AttackVfxRuntime() : impl_(std::make_unique<Impl>()) {}
AttackVfxRuntime::~AttackVfxRuntime() = default;

std::size_t AttackVfxRuntime::capacity() const noexcept { return impl_->slots.size(); }

Result<void> AttackVfxRuntime::configurePool(std::uint32_t capacity) {
    if (capacity == 0) return fail(DiagnosticCode::InvalidArgument, "pool capacity must be > 0", "capacity");
    for (const auto& slot : impl_->slots) {
        if (slot.state)
            return fail(DiagnosticCode::InvariantViolation, "cannot resize pool while instances are live",
                        "capacity");
    }
    impl_->slots.assign(capacity, Impl::Slot{});
    return Result<void>::success();
}

Result<void> AttackVfxRuntime::registerRecipe(const AttackVfxRecipe& recipe) {
    auto valid = recipe.validate();
    if (!valid) return valid;
    impl_->recipes[recipe.id.format()] = recipe;
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
    slot.phaseLayers.assign(recipe.phases.size());
    impl_->armTimedPhases(*slot.state);

    AttackVfxFrame bootstrap;
    for (std::size_t i = 0; i < recipe.phases.size(); ++i) {
        if (slot.state->phases[i].armed && recipe.phases[i].startOffsetSeconds <= 0.0) {
            auto entered = impl_->enterPhase(slot, i, bootstrap);
            if (!entered) {
                AttackVfxFrame cleanup;
                (void)impl_->finishInstance(freeSlot, cleanup, "play-failed");
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
        auto finished = impl_->finishInstance(handle.slot, frame, cue);
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

    AttackVfxFrame frame;
    std::vector<std::size_t> toFinish;

    for (std::size_t slotIndex = 0; slotIndex < impl_->slots.size(); ++slotIndex) {
        auto& slot = impl_->slots[slotIndex];
        if (!slot.state) continue;
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
                if (authored.startCue.empty()) {
                    if (state.age + 1e-12 >= authored.startOffsetSeconds) {
                        auto entered = impl_->enterPhase(slot, i, frame);
                        if (!entered) return Result<AttackVfxFrame>::failure(entered.status());
                    }
                } else {
                    live.localTime += dtSeconds;
                    if (live.localTime + 1e-12 >= authored.startOffsetSeconds) {
                        auto entered = impl_->enterPhase(slot, i, frame);
                        if (!entered) return Result<AttackVfxFrame>::failure(entered.status());
                    }
                }
            }
            if (live.active) {
                live.localTime += dtSeconds;
                auto updated = impl_->updatePhaseLayers(slot, i, dtSeconds);
                if (!updated) return Result<AttackVfxFrame>::failure(updated.status());
                if (authored.durationSeconds > 0.0 && live.localTime + 1e-12 >= authored.durationSeconds) {
                    auto exited = impl_->exitPhase(slot, i, frame, "duration");
                    if (!exited) return Result<AttackVfxFrame>::failure(exited.status());
                }
            }
        }

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
            auto finished = impl_->finishInstance(slotIndex, frame, "complete");
            if (!finished) return Result<AttackVfxFrame>::failure(finished.status());
        }
    }
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
    auto finished =
        impl_->finishInstance(handle.slot, frame, mode == AttackVfxStopMode::Cancel ? "cancel" : "clear");
    if (!finished) return Result<AttackVfxFrame>::failure(finished.status());
    return Result<AttackVfxFrame>::success(std::move(frame));
}

std::optional<AttackVfxInstanceState> AttackVfxRuntime::inspect(AttackVfxHandle handle) const {
    const auto* state = impl_->resolve(handle);
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
