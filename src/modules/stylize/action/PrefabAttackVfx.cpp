#include "common/Capability.h"
#include "stylize/AttackVfxLayerExecutor.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

namespace eve::stylize_action {
namespace {

/**
 * Prefab AttackVfx owns the authored spawn profile. Full RenderablePrefabPool
 * spawning stays in sceneloader (gameplay:prefab-spawn); this executor records
 * the request so multi-layer recipes can LayerStarted without forcing a
 * sceneloader→stylize edge in this slice.
 */
class PrefabAttackVfxExecutor final : public eve::stylize::IAttackVfxLayerExecutor {
public:
    eve::stylize::AttackVfxLayerRole role() const noexcept override {
        return eve::stylize::AttackVfxLayerRole::Prefab;
    }

    eve::Result<eve::stylize::AttackVfxLayerHandle> start(
        const eve::stylize::AttackVfxLayerStartRequest& request) override {
        if (!request.layer)
            return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "AttackVfx prefab layer is null", "layer"));
        if (request.layer->uri.empty())
            return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "AttackVfx prefab layer uri is empty", "uri"));

        Live live;
        live.uri = request.layer->uri;
        if (request.playRequest) {
            live.sourceId = request.playRequest->sourceId;
            live.targetId = request.playRequest->targetId;
        }
        const auto id = ++nextId_;
        live_.emplace(id, std::move(live));
        return eve::Result<eve::stylize::AttackVfxLayerHandle>::success(
            eve::stylize::AttackVfxLayerHandle{id});
    }

    eve::Result<void> update(eve::stylize::AttackVfxLayerHandle handle, double,
                             const eve::stylize::AttackVfxLayerStartRequest&) override {
        if (!live_.contains(handle.id))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::StaleHandle, "prefab AttackVfx layer handle is stale", "handle"));
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    }

    eve::Result<void> stop(eve::stylize::AttackVfxLayerHandle handle,
                           eve::stylize::AttackVfxStopBehavior) override {
        if (!live_.erase(handle.id))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::StaleHandle, "prefab AttackVfx layer handle is stale", "handle"));
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

private:
    struct Live {
        std::string   uri;
        std::uint32_t sourceId = 0;
        std::uint32_t targetId = 0;
    };

    std::uint64_t nextId_ = 1;
    std::unordered_map<std::uint64_t, Live> live_;
};

PrefabAttackVfxExecutor& prefabExecutor() {
    static PrefabAttackVfxExecutor instance;
    return instance;
}

bool gPrefabRegistered = false;

}  // namespace

void registerPrefabAttackVfxExecutor() {
    if (gPrefabRegistered) return;
    eve::cap::addListener<eve::stylize::IAttackVfxLayerExecutor>(&prefabExecutor());
    gPrefabRegistered = true;
}

void unregisterPrefabAttackVfxExecutor() {
    if (!gPrefabRegistered) return;
    eve::cap::removeListener<eve::stylize::IAttackVfxLayerExecutor>(&prefabExecutor());
    gPrefabRegistered = false;
}

}  // namespace eve::stylize_action
