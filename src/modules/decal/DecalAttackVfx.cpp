#include "common/Capability.h"
#include "common/Module.h"
#include "decal/DecalManager.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"
#include "stylize/AttackVfxLayerExecutor.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

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
    ~DecalAttackVfxExecutor() {
        // Same atexit hazard as ParticlesAttackVfx: this static may outlive Graphics.
        // Drop albedo pointers without calling releaseTexture (process is exiting).
        live_.clear();
    }

    void clearLive() {
        for (auto& entry : live_) {
            if (entry.second.decalId > 0) (void)DecalManager::inst().remove(entry.second.decalId);
            releaseTexture(entry.second.albedo);
            entry.second.albedo = nullptr;
        }
        live_.clear();
    }

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

        // World placement comes from authored floatParams (or spatial adapters later).
        // Entity ids must never be treated as coordinates.
        const float x        = layerParam(request, "x", 0.f);
        const float y        = layerParam(request, "y", 0.05f);
        const float z        = layerParam(request, "z", 0.f);
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
        if (decalId <= 0) {
            releaseTexture(albedo);
            return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "DecalManager.project returned an invalid id", "uri"));
        }

        Live owned;
        owned.decalId = decalId;
        owned.albedo  = albedo;
        const auto id = ++nextId_;
        live_.emplace(id, std::move(owned));
        return eve::Result<eve::stylize::AttackVfxLayerHandle>::success(
            eve::stylize::AttackVfxLayerHandle{id});
    }

    eve::Result<void> update(eve::stylize::AttackVfxLayerHandle handle, double dtSeconds,
                             const eve::stylize::AttackVfxLayerStartRequest& request) override {
        const auto found = live_.find(handle.id);
        if (found == live_.end())
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::StaleHandle, "decal AttackVfx layer handle is stale", "handle"));

        // Coalesce DecalManager::update to once per runtime tickSerial.
        if (request.tickSerial != lastTickSerial_) {
            DecalManager::inst().update(static_cast<float>(dtSeconds));
            lastTickSerial_ = request.tickSerial;
        }

        if (found->second.draining && !DecalManager::inst().contains(found->second.decalId)) {
            releaseTexture(found->second.albedo);
            live_.erase(found);
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
        }
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<void> stop(eve::stylize::AttackVfxLayerHandle handle,
                           eve::stylize::AttackVfxStopBehavior behavior) override {
        const auto found = live_.find(handle.id);
        if (found == live_.end())
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::StaleHandle, "decal AttackVfx layer handle is stale", "handle"));
        if (behavior == eve::stylize::AttackVfxStopBehavior::ClearImmediately) {
            (void)DecalManager::inst().remove(found->second.decalId);
            releaseTexture(found->second.albedo);
            live_.erase(found);
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
        }
        // StopEmitting: leave the projected decal to age out via update()/DecalManager.
        found->second.draining = true;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

private:
    struct Live {
        int                decalId  = 0;
        graphics::Texture* albedo   = nullptr;
        bool               draining = false;
    };

    static void releaseTexture(graphics::Texture* texture) {
        if (!texture) return;
        auto* graphics = eve::ModuleManager::getInstance<graphics::Graphics>("Graphics");
        if (graphics && graphics->releaseTexture(texture)) delete texture;
    }

    std::uint64_t nextId_ = 1;
    std::uint64_t lastTickSerial_ = 0;
    std::unordered_map<std::uint64_t, Live> live_;
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
    executor().clearLive();
    gRegistered = false;
}

}  // namespace eve::decal
