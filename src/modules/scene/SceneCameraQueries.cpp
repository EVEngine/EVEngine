// Picking / culling entry points whose camera math belongs to graphics.
//
// The traversal and mutation stay here because the scene owns them; only the
// screen ray and the clip matrix come from the ISceneCameraProjection
// capability (graphics/ScenePicking.cpp registers it). Querying a capability
// instead of calling graphics keeps this module free of an upward include while
// still defining these symbols in scene, which a per-layer DLL split needs:
// graphics (L4) cannot be imported by scene (L1).
//
// Split out of Scene.cpp (already past the ~1000 line limit) rather than
// appended to it.

#include "scene/Scene.h"

#include "common/Capability.h"
#include "scene/SceneBounds.h"
#include "scene/SceneCameraProjection.h"
#include "scene/SceneHost.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace eve::scene {

std::string Scene::pickScreenAt(const std::string &hostName, graphics::Camera3D *cam, float screenX, float screenY,
                                float viewW, float viewH) const {
    if (!cam) return {};
    auto *projection = eve::cap::query<ISceneCameraProjection>();
    if (!projection) return {};
    const auto ray = projection->screenRay(*cam, screenX, screenY, viewW, viewH);
    if (!ray) return {};
    return pickRayAt(hostName, ray->origin.x, ray->origin.y, ray->origin.z, ray->direction.x, ray->direction.y,
                     ray->direction.z);
}

std::vector<std::string> Scene::collectFrustumIdsAt(const std::string &hostName, graphics::Camera3D *cam, float viewW,
                                                    float viewH) const {
    SceneHost               *h = resolveHost(hostName);
    std::vector<std::string> out;
    if (!h || !cam || viewW <= 0.f || viewH <= 0.f) return out;
    auto *projection = eve::cap::query<ISceneCameraProjection>();
    if (!projection) return out;
    const auto clip = projection->clipForViewport(*cam, viewW, viewH);
    if (!clip) return out;

    h->walkDepthFirst([&](SceneHost *, int, SceneNode &n) {
        if (!n.hasBounds) return;
        if (aabbIntersectsFrustum(clip->clip, clip->inverse, worldBoundsOf(n))) {
            out.push_back(n.id);
        }
    });
    return out;
}

Result<int> Scene::applyPcgTerrainCullingAt(const std::string &hostName, graphics::Camera3D *cam, float viewW,
                                            float viewH, const std::string &tag) {
    SceneHost *host = resolveHost(hostName);
    if (!host || !cam || !std::isfinite(viewW) || !std::isfinite(viewH) || viewW <= 0.f || viewH <= 0.f || tag.empty())
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Pcg terrain culling requires host, camera, positive viewport and tag",
            "scene.pcgTerrainCulling"));
    auto *projection = eve::cap::query<ISceneCameraProjection>();
    if (!projection)
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "Pcg terrain culling needs the graphics camera projection capability",
            "scene.pcgTerrainCulling"));
    const auto clip = projection->clipForViewport(*cam, viewW, viewH);
    if (!clip)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "Pcg terrain culling requires a usable camera viewport",
                                                      "scene.pcgTerrainCulling"));

    std::vector<std::pair<std::string, bool>> changes;
    host->walkDepthFirst([&](SceneHost *, int, SceneNode &node) {
        if (!node.hasBounds || std::find(node.tags.begin(), node.tags.end(), tag) == node.tags.end()) return;
        const bool visible = aabbIntersectsFrustum(clip->clip, clip->inverse, worldBoundsOf(node));
        if (node.visible != visible) changes.emplace_back(node.id, visible);
    });
    for (const auto &[id, visible] : changes) {
        auto node = host->findById(id);
        if (!node) continue;
        node.value()->visible = visible;
        host->markSubtreeDirtyById(id);
    }
    return Result<int>::success(static_cast<int>(changes.size()));
}

}  // namespace eve::scene
