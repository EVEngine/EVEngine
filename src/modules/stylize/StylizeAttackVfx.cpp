#include "common/Capability.h"
#include "stylize/AttackVfxLayerExecutor.h"
#include "stylize/MeshVfxAsset.h"
#include "stylize/SkillMeshEffect.h"
#include "stylize/TrailEffect.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>

namespace eve::stylize {
namespace {

float layerParam(const AttackVfxLayerStartRequest& request, const char* key, float fallback) {
    if (request.layer) {
        const auto found = request.layer->floatParams.find(key);
        if (found != request.layer->floatParams.end()) return found->second;
    }
    return fallback;
}

std::string skinHint(const AttackVfxSkin* skin, const char* key) {
    if (!skin) return {};
    const auto found = skin->styleHints.find(key);
    return found == skin->styleHints.end() ? std::string{} : found->second;
}

std::optional<SkillMeshEffectKind> parseSkillKind(std::string_view name) {
    if (name == "weaponSlash" || name == "slash") return SkillMeshEffectKind::WeaponSlash;
    if (name == "impactFlash" || name == "impact" || name == "rim") return SkillMeshEffectKind::ImpactFlash;
    if (name == "chargeAura" || name == "aura") return SkillMeshEffectKind::ChargeAura;
    if (name == "burningBody" || name == "ember") return SkillMeshEffectKind::BurningBody;
    return std::nullopt;
}

Result<std::unique_ptr<MeshVfxAssetInstance>> makeStyleInstance(const std::string& style,
                                                                const AttackVfxLayerStartRequest& request) {
    MeshVfxAsset asset;
    MeshVfxLayerAsset layer;
    layer.style    = style;
    layer.playback = {0.02f, 0.12f, 0.18f, false};
    if (request.layer) {
        for (const auto& [key, value] : request.layer->floatParams) layer.floatParameters[key] = value;
    }
    asset.layers.push_back(std::move(layer));
    return MeshVfxAssetInstance::create(asset);
}

class MeshVfxAttackVfxExecutor final : public IAttackVfxLayerExecutor {
public:
    AttackVfxLayerRole role() const noexcept override { return AttackVfxLayerRole::MeshVfx; }

    Result<AttackVfxLayerHandle> start(const AttackVfxLayerStartRequest& request) override {
        if (!request.layer)
            return Result<AttackVfxLayerHandle>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "AttackVfx meshVfx layer is null", "layer"));

        const std::string& uri = request.layer->uri;
        using Owned =
            std::variant<std::unique_ptr<MeshVfxAssetInstance>, std::unique_ptr<SkillMeshEffect>>;
        Owned owned = std::unique_ptr<MeshVfxAssetInstance>{};

        if (uri.rfind("json:", 0) == 0) {
            auto parsed = MeshVfxAsset::fromJson(uri.substr(5));
            if (!parsed) return Result<AttackVfxLayerHandle>::failure(parsed.status());
            auto created = MeshVfxAssetInstance::create(parsed.value());
            if (!created) return Result<AttackVfxLayerHandle>::failure(created.status());
            owned = std::move(created).takeValue();
        } else if (uri.rfind("skill:", 0) == 0) {
            auto kind = parseSkillKind(uri.substr(6));
            if (!kind)
                return Result<AttackVfxLayerHandle>::failure(Diagnostic::error(
                    DiagnosticCode::NotFound, "unknown AttackVfx skill mesh kind", "uri"));
            owned = std::make_unique<SkillMeshEffect>(*kind);
        } else if (uri.rfind("style:", 0) == 0) {
            auto created = makeStyleInstance(uri.substr(6), request);
            if (!created) return Result<AttackVfxLayerHandle>::failure(created.status());
            owned = std::move(created).takeValue();
        } else {
            std::string token = uri;
            if (token.rfind("meshvfx://", 0) == 0) token = token.substr(10);
            std::string style = skinHint(request.skin, "meshStyle");
            if (style.empty()) {
                if (auto kind = parseSkillKind(token)) {
                    owned = std::make_unique<SkillMeshEffect>(*kind);
                } else if (!token.empty()) {
                    style = token;
                } else {
                    return Result<AttackVfxLayerHandle>::failure(Diagnostic::error(
                        DiagnosticCode::NotFound, "meshVfx layer has no resolvable style", "uri"));
                }
            }
            if (std::holds_alternative<std::unique_ptr<MeshVfxAssetInstance>>(owned) &&
                !std::get<std::unique_ptr<MeshVfxAssetInstance>>(owned)) {
                auto created = makeStyleInstance(style, request);
                if (!created) return Result<AttackVfxLayerHandle>::failure(created.status());
                owned = std::move(created).takeValue();
            }
        }

        if (auto* asset = std::get_if<std::unique_ptr<MeshVfxAssetInstance>>(&owned)) {
            if (!*asset)
                return Result<AttackVfxLayerHandle>::failure(Diagnostic::error(
                    DiagnosticCode::Failed, "meshVfx instance was not created", "uri"));
            (*asset)->play();
        } else if (auto* skill = std::get_if<std::unique_ptr<SkillMeshEffect>>(&owned)) {
            (*skill)->play();
        }

        const auto id = ++nextId_;
        live_.emplace(id, std::move(owned));
        return Result<AttackVfxLayerHandle>::success(AttackVfxLayerHandle{id});
    }

    Result<void> update(AttackVfxLayerHandle handle, double dtSeconds,
                        const AttackVfxLayerStartRequest&) override {
        const auto found = live_.find(handle.id);
        if (found == live_.end())
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::StaleHandle, "meshVfx AttackVfx layer handle is stale", "handle"));
        const float dt = static_cast<float>(dtSeconds);
        if (auto* asset = std::get_if<std::unique_ptr<MeshVfxAssetInstance>>(&found->second))
            (*asset)->update(dt);
        else if (auto* skill = std::get_if<std::unique_ptr<SkillMeshEffect>>(&found->second))
            (*skill)->update(dt);
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    Result<void> stop(AttackVfxLayerHandle handle, AttackVfxStopBehavior behavior) override {
        const auto found = live_.find(handle.id);
        if (found == live_.end())
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::StaleHandle, "meshVfx AttackVfx layer handle is stale", "handle"));
        if (auto* asset = std::get_if<std::unique_ptr<MeshVfxAssetInstance>>(&found->second))
            (*asset)->stop(behavior == AttackVfxStopBehavior::ClearImmediately ? 0.f : -1.f);
        else if (auto* skill = std::get_if<std::unique_ptr<SkillMeshEffect>>(&found->second))
            (*skill)->stop(behavior == AttackVfxStopBehavior::ClearImmediately ? 0.f : 0.1f);
        live_.erase(found);
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

private:
    using Owned =
        std::variant<std::unique_ptr<MeshVfxAssetInstance>, std::unique_ptr<SkillMeshEffect>>;
    std::uint64_t nextId_ = 1;
    std::unordered_map<std::uint64_t, Owned> live_;
};

class TrailAttackVfxExecutor final : public IAttackVfxLayerExecutor {
public:
    AttackVfxLayerRole role() const noexcept override { return AttackVfxLayerRole::Trail; }

    Result<AttackVfxLayerHandle> start(const AttackVfxLayerStartRequest& request) override {
        if (!request.layer)
            return Result<AttackVfxLayerHandle>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "AttackVfx trail layer is null", "layer"));

        TrailSettings settings;
        if (const auto maxSamples = layerParam(request, "maxSamples", 0.f); maxSamples > 0.f)
            settings.maxSamples = static_cast<std::size_t>(maxSamples);
        if (const auto lifetime = layerParam(request, "lifetime", 0.f); lifetime > 0.f)
            settings.lifetime = lifetime;
        if (const auto minDist = layerParam(request, "minSampleDistance", 0.f); minDist > 0.f)
            settings.minSampleDistance = minDist;
        if (const auto teleport = layerParam(request, "teleportDistance", 0.f); teleport > 0.f)
            settings.teleportDistance = teleport;

        Live live;
        live.emitter = std::make_unique<TrailEmitter>(settings);
        if (request.playRequest) {
            live.sourceX = static_cast<float>(request.playRequest->sourceId);
            live.targetX = static_cast<float>(request.playRequest->targetId);
        }
        (void)live.emitter->append({live.sourceX, 0.f, 0.f}, {live.targetX, 0.2f, 0.f});

        const auto id = ++nextId_;
        live_.emplace(id, std::move(live));
        return Result<AttackVfxLayerHandle>::success(AttackVfxLayerHandle{id});
    }

    Result<void> update(AttackVfxLayerHandle handle, double dtSeconds,
                        const AttackVfxLayerStartRequest& request) override {
        const auto found = live_.find(handle.id);
        if (found == live_.end())
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::StaleHandle, "trail AttackVfx layer handle is stale", "handle"));
        found->second.emitter->update(static_cast<float>(dtSeconds));
        float sourceX = found->second.sourceX;
        float targetX = found->second.targetX;
        if (request.playRequest) {
            sourceX = static_cast<float>(request.playRequest->sourceId);
            targetX = static_cast<float>(request.playRequest->targetId);
        }
        found->second.phase += static_cast<float>(dtSeconds);
        (void)found->second.emitter->append({sourceX, 0.f, 0.f},
                                            {targetX, 0.2f + 0.05f * found->second.phase, 0.f});
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    Result<void> stop(AttackVfxLayerHandle handle, AttackVfxStopBehavior behavior) override {
        const auto found = live_.find(handle.id);
        if (found == live_.end())
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::StaleHandle, "trail AttackVfx layer handle is stale", "handle"));
        if (behavior == AttackVfxStopBehavior::ClearImmediately)
            found->second.emitter->clear();
        else
            found->second.emitter->breakTrail();
        live_.erase(found);
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

private:
    struct Live {
        std::unique_ptr<TrailEmitter> emitter;
        float                         sourceX = 0.f;
        float                         targetX = 1.f;
        float                         phase   = 0.f;
    };

    std::uint64_t nextId_ = 1;
    std::unordered_map<std::uint64_t, Live> live_;
};

class DistortionAttackVfxExecutor final : public IAttackVfxLayerExecutor {
public:
    AttackVfxLayerRole role() const noexcept override { return AttackVfxLayerRole::Distortion; }

    Result<AttackVfxLayerHandle> start(const AttackVfxLayerStartRequest& request) override {
        if (!request.layer)
            return Result<AttackVfxLayerHandle>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "AttackVfx distortion layer is null", "layer"));

        float strength = layerParam(request, "strength", 0.f);
        if (strength <= 0.f && request.skin) {
            const auto found = request.skin->distortionProfile.find("strength");
            if (found != request.skin->distortionProfile.end()) strength = found->second;
        }
        if (strength < 0.f)
            return Result<AttackVfxLayerHandle>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "distortion strength must be non-negative",
                "floatParams.strength"));

        // Phase 3 owns the authored profile; GPU warp sinks arrive in a later slice.
        const auto id = ++nextId_;
        live_.emplace(id, strength);
        return Result<AttackVfxLayerHandle>::success(AttackVfxLayerHandle{id});
    }

    Result<void> update(AttackVfxLayerHandle handle, double,
                        const AttackVfxLayerStartRequest&) override {
        if (!live_.contains(handle.id))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::StaleHandle, "distortion AttackVfx layer handle is stale", "handle"));
        return Result<void>::success(Status::success(StatusCode::NoOp));
    }

    Result<void> stop(AttackVfxLayerHandle handle, AttackVfxStopBehavior) override {
        if (!live_.erase(handle.id))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::StaleHandle, "distortion AttackVfx layer handle is stale", "handle"));
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

private:
    std::uint64_t nextId_ = 1;
    std::unordered_map<std::uint64_t, float> live_;
};

MeshVfxAttackVfxExecutor& meshExecutor() {
    static MeshVfxAttackVfxExecutor instance;
    return instance;
}
TrailAttackVfxExecutor& trailExecutor() {
    static TrailAttackVfxExecutor instance;
    return instance;
}
DistortionAttackVfxExecutor& distortionExecutor() {
    static DistortionAttackVfxExecutor instance;
    return instance;
}

bool gRegistered = false;

}  // namespace

void registerStylizeAttackVfxExecutors() {
    if (gRegistered) return;
    eve::cap::addListener<IAttackVfxLayerExecutor>(&meshExecutor());
    eve::cap::addListener<IAttackVfxLayerExecutor>(&trailExecutor());
    eve::cap::addListener<IAttackVfxLayerExecutor>(&distortionExecutor());
    gRegistered = true;
}

void unregisterStylizeAttackVfxExecutors() {
    if (!gRegistered) return;
    eve::cap::removeListener<IAttackVfxLayerExecutor>(&distortionExecutor());
    eve::cap::removeListener<IAttackVfxLayerExecutor>(&trailExecutor());
    eve::cap::removeListener<IAttackVfxLayerExecutor>(&meshExecutor());
    gRegistered = false;
}

}  // namespace eve::stylize
