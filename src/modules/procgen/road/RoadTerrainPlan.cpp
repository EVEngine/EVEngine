#include "procgen/road/RoadTerrainPlan.h"
#include <limits>
#include <unordered_map>
#include "procgen/heightmap/TerrainWorldWorkspace.h"
#include "procgen/road/RoadAuthoringInternal.h"
#include "procgen/road/RoadGeometrySources.h"
#include "procgen/road/RoadTerrainWorkspace.h"
namespace eve::procgen::road {
namespace {
template <class T>
Result<T> fail(const char* message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "road.terrainPlan"));
}
RoadTerrainWriteOptions optionsFor(const RoadTerrainPolicy& p, std::size_t budget) {
    RoadTerrainWriteOptions o;
    o.blendDistance            = p.blendDistance;
    o.maxVerticalDelta         = p.maxVerticalDelta;
    o.splatLayer               = p.splatLayer;
    o.splatOpacity             = p.splatOpacity;
    o.clearDetails             = p.clearDetails;
    o.clearTrees               = p.clearTrees;
    o.cutTunnelHoles           = p.cutTunnelHoles;
    o.tunnelHolesAtPortalsOnly = p.tunnelHolesAtPortalsOnly;
    o.maximumWork              = budget;
    return o;
}
Result<MeshBuild> compact(const MeshBuild& source, const RoadGeometrySourceSpan& span, std::size_t& used,
                          std::size_t cap) {
    MeshBuild                              mesh;
    std::unordered_map<std::uint32_t, int> vertices;
    std::vector<float>                     colors;
    const auto&                            indices = source.indices();
    const std::size_t                      end     = static_cast<std::size_t>(span.firstTriangle) + span.triangleCount;
    for (std::size_t t = span.firstTriangle; t < end; ++t) {
        if (cap - used < 3) return fail<MeshBuild>("prepared index budget exhausted");
        used += 3;
        mesh.setActiveGroup(source.getGroupName(source.getTriangleGroup(static_cast<int>(t))));
        int out[3];
        for (int j = 0; j < 3; ++j) {
            auto v     = indices[t * 3 + j];
            auto found = vertices.find(v);
            if (found != vertices.end()) {
                out[j] = found->second;
                continue;
            }
            const std::size_t cost = source.hasVertexColors() ? 12 : 8;
            if (cost > cap - used) return fail<MeshBuild>("prepared vertex budget exhausted");
            used += cost;
            auto i     = static_cast<std::size_t>(v);
            int  index = mesh.getVertexCount();
            mesh.addVertex(source.positions()[i * 3], source.positions()[i * 3 + 1], source.positions()[i * 3 + 2],
                           source.normals()[i * 3], source.normals()[i * 3 + 1], source.normals()[i * 3 + 2],
                           source.uvs()[i * 2], source.uvs()[i * 2 + 1]);
            vertices.emplace(v, index);
            out[j] = index;
            if (source.hasVertexColors())
                colors.insert(colors.end(), source.colors().begin() + i * 4, source.colors().begin() + i * 4 + 4);
        }
        mesh.addTriangle(out[0], out[1], out[2]);
    }
    auto colored = mesh.setVertexColors(std::move(colors));
    if (!colored.ok()) return Result<MeshBuild>::failure(colored.status());
    return Result<MeshBuild>::success(std::move(mesh));
}
}  // namespace
struct RoadTerrainPlan::Impl {
    struct Item {
        MeshBuild         mesh;
        RoadTerrainPolicy policy;
    };
    std::vector<Item>             items;
    std::vector<RoadTerrainWrite> writes(std::size_t budget) const {
        std::vector<RoadTerrainWrite> out;
        out.reserve(items.size());
        for (const auto& item : items) out.push_back({std::cref(item.mesh), optionsFor(item.policy, budget)});
        return out;
    }
};
RoadTerrainPlan::RoadTerrainPlan(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RoadTerrainPlan::~RoadTerrainPlan()                                            = default;
RoadTerrainPlan::RoadTerrainPlan(RoadTerrainPlan&&) noexcept                   = default;
RoadTerrainPlan&        RoadTerrainPlan::operator=(RoadTerrainPlan&&) noexcept = default;
Result<RoadTerrainPlan> RoadTerrainPlan::prepare(const RoadNetwork& network, const RoadBakeOptions& options,
                                                 const RoadTerrainPolicy& sharedPolicy,
                                                 std::size_t              maximumPreparedElements) {
    if (maximumPreparedElements == 0) return fail<RoadTerrainPlan>("prepared element budget must be positive");
    RoadStyle sharedStyle;
    sharedStyle.terrainPolicy = sharedPolicy;
    auto validShared          = detail::validateRoadAuthoring(sharedStyle);
    if (!validShared.ok()) return Result<RoadTerrainPlan>::failure(validShared.status());
    auto baked = bakeRoadNetwork(network, options);
    if (!baked.ok()) return Result<RoadTerrainPlan>::failure(baked.status());
    auto sources = PreparedRoadGeometrySources::prepare(baked.value().mesh);
    if (!sources.ok()) return Result<RoadTerrainPlan>::failure(sources.status());
    std::unordered_map<std::uint32_t, RoadTerrainPolicy> policies;
    for (const auto& edge : network.edges()) policies.emplace(edge.id, edge.style.terrainPolicy);
    auto        impl = std::make_unique<Impl>();
    std::size_t used = 0;
    for (const auto& span : sources.value().spans()) {
        RoadTerrainPolicy policy = sharedPolicy;
        if (span.kind == RoadGeometrySourceKind::Edge) {
            auto edge = policies.find(span.id);
            if (edge == policies.end()) return fail<RoadTerrainPlan>("source edge has no author policy");
            policy = edge->second;
        }
        if (!policy.enabled) continue;
        if (impl->items.size() >= 4096) return fail<RoadTerrainPlan>("prepared write count exceeds batch limit");
        auto mesh = compact(baked.value().mesh, span, used, maximumPreparedElements);
        if (!mesh.ok()) return Result<RoadTerrainPlan>::failure(mesh.status());
        impl->items.push_back({std::move(mesh).takeValue(), policy});
    }
    return Result<RoadTerrainPlan>::success(RoadTerrainPlan(std::move(impl)));
}
Result<int> RoadTerrainPlan::apply(RoadTerrainWorkspace& workspace, std::size_t maximumWork) const {
    if (!impl_ || maximumWork == 0) return fail<int>("missing plan or invalid execution budget");
    if (impl_->items.empty()) return Result<int>::success(0, Status::success(StatusCode::NoOp));
    auto writes = impl_->writes(maximumWork);
    return workspace.applyBatch(writes, maximumWork);
}
Result<int> RoadTerrainPlan::applyWorld(TerrainWorldWorkspace& world, std::size_t maximumWork) const {
    if (!impl_ || maximumWork == 0) return fail<int>("missing plan or invalid execution budget");
    if (impl_->items.empty()) return Result<int>::success(0, Status::success(StatusCode::NoOp));
    auto writes = impl_->writes(maximumWork);
    return world.conformRoadBatch(writes, maximumWork);
}
}  // namespace eve::procgen::road
