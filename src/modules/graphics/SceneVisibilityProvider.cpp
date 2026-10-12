#include <map>
#include <set>
#include "common/Capability.h"
#include "common/SceneVisibilityEvidence.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/SceneVisibility.h"
#include "scene/SceneHost.h"

namespace eve::graphics {
namespace {
const Value&  member(const Value& v, const char* name) { return v.getIf<Value::Object>()->at(name); }
Result<Value> rejected(DiagnosticCode code, const char* message) {
    return Result<Value>::failure(Diagnostic::error(code, message));
}
class VisibilityProvider final : public ISceneVisibilityEvidence {
public:
    Graphics*     graphics = nullptr;
    Result<Value> inspect(const std::string& name, const Value& request) override {
        if (!graphics) return rejected(DiagnosticCode::Unsupported, "Visibility graphics provider detached");
        auto parsed = SceneVisibilityQuery::prepare(request);
        if (!parsed.ok()) return Result<Value>::failure(parsed.status());
        scene::SceneHost* host = nullptr;
        if (ecs::current()->getManager<scene::SceneHost>()) {
            auto view = ecs::View<scene::SceneHost, scene::SceneHost::Meta>();
            for (auto it = view.begin(); it != view.end(); ++it) {
                auto [meta] = *it;
                if (meta->entity && meta->name == name) {
                    if (host) return rejected(DiagnosticCode::Conflict, "Projection host name is ambiguous");
                    host = meta->entity;
                }
            }
        }
        if (!host) return rejected(DiagnosticCode::StaleHandle, "Projection host missing");
        std::map<std::uint32_t, Value> actual;
        bool                           invalid = false;
        const auto*                    ops     = scene::linkOps(scene::findLinkKind("renderable3d"));
        host->walkDepthFirst([&](scene::SceneHost*, int, scene::SceneNode& node) {
            bool visible = host->meta()->visible && node.visible;
            for (int parent = node.parent; parent >= 0; parent = host->tree()->nodes[parent].parent)
                visible = visible && host->tree()->nodes[parent].visible;
            if (!visible) return;
            const auto* link = host->findLink(&node, scene::findLinkKind("renderable3d"));
            if (!link || !link->target) return;
            if (!ops || !ops->alive || !ops->alive(link->target)) {
                invalid = true;
                return;
            }
            auto*                      entity = static_cast<Renderable3D*>(link->target);
            const auto*                mesh   = entity->meshRenderer()->mesh;
            constexpr std::string_view prefix = "@projection/";
            const auto                 split  = node.id.rfind('/');
            if (!node.id.starts_with(prefix) || split <= prefix.size() || !mesh || !mesh->geometryGeneration()) {
                invalid = true;
                return;
            }
            Value::Array model;
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r) model.emplace_back(double(node.world[c][r]));
            Value proof = Value::Object{{"object", node.id.substr(prefix.size(), split - prefix.size())},
                                        {"generation", std::int64_t(entity->generation)},
                                        {"geometryGeneration", std::int64_t(mesh->geometryGeneration())},
                                        {"vertexCount", mesh->gpuVertexCount},
                                        {"indexCount", mesh->indexCount},
                                        {"liveModel", std::move(model)}};
            if (!actual.emplace(entity->id, std::move(proof)).second) invalid = true;
        });
        if (invalid) return rejected(DiagnosticCode::Conflict, "Projection links or geometry cannot be proven");
        const auto& draws = *member(request, "draws").getIf<Value::Array>();
        if (draws.size() != actual.size())
            return rejected(DiagnosticCode::Conflict, "Visibility requires the complete visible native projection");
        Value::Object           proofs;
        std::set<std::uint32_t> ids;
        for (const auto& draw : draws) {
            const auto& value = member(draw, "entityId");
            const auto  id    = value.getIf<std::int64_t>() ? std::uint32_t(*value.getIf<std::int64_t>())
                                                            : std::uint32_t(*value.getIf<double>());
            const auto  it    = actual.find(id);
            if (it == actual.end() || !ids.insert(id).second ||
                member(draw, "generation") != member(it->second, "generation") ||
                member(draw, "object") != member(it->second, "object"))
                return rejected(DiagnosticCode::Conflict, "Visibility draw is not the native object's projection");
            proofs[std::to_string(id)] = it->second;
        }
        return Result<Value>::success(Value::Object{{"hostId", std::int64_t(host->id)},
                                                    {"hostGeneration", std::int64_t(host->generation)},
                                                    {"draws", std::move(proofs)}});
    }
    Result<Value> measure(const std::string& host, const Value& request, const Value& proof) override {
        auto current = inspect(host, request);
        if (!current.ok()) return current;
        if (current.value() != proof)
            return rejected(DiagnosticCode::Conflict, "Projection or geometry changed during preview");
        auto prepared = SceneVisibilityQuery::prepare(request);
        if (!prepared.ok()) return Result<Value>::failure(prepared.status());
        return prepared.value().measure(*graphics);
    }
};
VisibilityProvider& provider() {
    static VisibilityProvider instance;
    return instance;
}
}  // namespace
void registerSceneVisibilityProvider(Graphics* graphics) {
    provider().graphics = graphics;
    cap::provide<ISceneVisibilityEvidence>(&provider());
}
void detachSceneVisibilityProvider(Graphics* graphics) noexcept {
    if (provider().graphics == graphics) {
        cap::revoke<ISceneVisibilityEvidence>(&provider());
        provider().graphics = nullptr;
    }
}
}  // namespace eve::graphics
