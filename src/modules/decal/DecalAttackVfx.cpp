#include "common/Capability.h"
#include "common/Module.h"
#include "decal/DecalManager.h"
#include "graphics/Graphics.h"
#include "stylize/AttackVfxLayerExecutor.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eve::decal {
namespace {

float layerParam(const eve::stylize::AttackVfxLayerStartRequest& request, const char* key,
                 float fallback) {
    if (request.layer) {
        const auto found = request.layer->floatParams.find(key);
        if (found != request.layer->floatParams.end()) return found->second;
    }
    return fallback;
}

graphics::Texture* resolveAlbedo(const eve::stylize::AttackVfxLayerStartRequest& request) {
    auto* graphics = eve::ModuleManager::getInstance<graphics::Graphics>("Graphics");
    if (!graphics) return nullptr;

    const std::string& uri = request.layer ? request.layer->uri : std::string{};
    if (uri.rfind("solid:", 0) == 0 || uri.rfind("decal://", 0) == 0 || uri.empty()) {
        const float r = layerParam(request, "r", 0.85f);
        const float g = layerParam(request, "g", 0.25f);
        const float b = layerParam(request, "b", 0.08f);
        const float a = layerParam(request, "a", 0.85f);
        const auto toByte = [](float v) -> std::uint8_t {
            if (v <= 0.f) return 0;
            if (v >= 1.f) return 255;
            return static_cast<std::uint8_t>(v * 255.f + 0.5f);
        };
        const std::uint8_t rgba[4] = {toByte(r), toByte(g), toByte(b), toByte(a)};
        return graphics->newTexture(1, 1, rgba);
    }
    try {
        return graphics->newTextureFromFile(uri);
    } catch (...) {
        return nullptr;
    }
}

class DecalAttackVfxExecutor final : public eve::stylize::IAttackVfxLayerExecutor {
public:
    eve::stylize::AttackVfxLayerRole role() const noexcept override {
        return eve::stylize::AttackVfxLayerRole::Decal;
    }

    eve::Result<eve::stylize::AttackVfxLayerHandle> start(
        const eve::stylize::AttackVfxLayerStartRequest& request) override {
        if (!request.layer)
            return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "AttackVfx decal layer is null", "layer"));
        if (request.layer->uri.empty())
            return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "AttackVfx decal layer uri is empty", "uri"));

        graphics::Texture* albedo = resolveAlbedo(request);
        if (!albedo)
            return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound, "decal albedo texture could not be resolved", "uri"));

        const float x = layerParam(request, "x",
                                   request.playRequest ? static_cast<float>(request.playRequest->sourceId)
                                                       : 0.f);
        const float y = layerParam(request, "y", 0.05f);
        const float z = layerParam(request, "z",
                                   request.playRequest ? static_cast<float>(request.playRequest->targetId)
                                                       : 0.f);
        const float size     = layerParam(request, "size", 1.2f);
        const float depth    = layerParam(request, "depth", 0.35f);
        const float lifetime = layerParam(request, "lifetime", 1.5f);
        const float fadeIn   = layerParam(request, "fadeIn", 0.05f);
        const float fadeOut  = layerParam(request, "fadeOut", 0.35f);
        const int   seed =
            request.playRequest
                ? static_cast<int>(request.playRequest->sourceId ^ request.playRequest->targetId)
                : 0;

        const int decalId = DecalManager::inst().project(
            x, y, z, 0.f, 1.f, 0.f, albedo, "attackvfx", size, depth, true, seed, fadeIn, lifetime,
            fadeOut, 0.f, 0.f, 0.f, 0.f);
        if (decalId <= 0)
            return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "DecalManager.project returned an invalid id", "uri"));

        const auto id = ++nextId_;
        live_.emplace(id, decalId);
        return eve::Result<eve::stylize::AttackVfxLayerHandle>::success(
            eve::stylize::AttackVfxLayerHandle{id});
    }

    eve::Result<void> update(eve::stylize::AttackVfxLayerHandle handle, double dtSeconds,
                             const eve::stylize::AttackVfxLayerStartRequest&) override {
        if (!live_.contains(handle.id))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::StaleHandle, "decal AttackVfx layer handle is stale", "handle"));
        DecalManager::inst().update(static_cast<float>(dtSeconds));
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<void> stop(eve::stylize::AttackVfxLayerHandle handle,
                           eve::stylize::AttackVfxStopBehavior behavior) override {
        const auto found = live_.find(handle.id);
        if (found == live_.end())
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::StaleHandle, "decal AttackVfx layer handle is stale", "handle"));
        if (behavior == eve::stylize::AttackVfxStopBehavior::ClearImmediately)
            (void)DecalManager::inst().remove(found->second);
        live_.erase(found);
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

private:
    std::uint64_t nextId_ = 1;
    std::unordered_map<std::uint64_t, int> live_;
};

DecalAttackVfxExecutor& executor() {
    static DecalAttackVfxExecutor instance;
    return instance;
}

bool gRegistered = false;

}  // namespace

void registerDecalAttackVfxExecutor() {
    if (gRegistered) return;
    eve::cap::addListener<eve::stylize::IAttackVfxLayerExecutor>(&executor());
    gRegistered = true;
}

void unregisterDecalAttackVfxExecutor() {
    if (!gRegistered) return;
    eve::cap::removeListener<eve::stylize::IAttackVfxLayerExecutor>(&executor());
    gRegistered = false;
}

}  // namespace eve::decal
