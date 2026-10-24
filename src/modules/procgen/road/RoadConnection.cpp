#include "procgen/road/RoadAuthoringInternal.h"
#include "procgen/road/RoadBakeInternal.h"
#include "procgen/road/RoadConnectionInternal.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_set>

namespace eve::procgen::road::detail {
namespace {
Result<void> invalidConnection(std::string message) {
    return bakeFail<void>(DiagnosticCode::PreconditionViolation, std::move(message), "node.connection");
}
V3 rotate(V3 p, float degrees) {
    const float radians = degrees * 0.017453292519943295f, c = std::cos(radians), s = std::sin(radians);
    return {p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
}
V3 world(V3 p, const RoadNode& node) { return rotate(p, node.connection.yawDegrees) + V3{node.x, node.y, node.z}; }
V3 seamPoint(const RoadConnectionPort& port, const RoadProfilePoint& point) {
    const V3 forward{port.forwardX, port.forwardY, port.forwardZ}, up{port.upX, port.upY, port.upZ};
    return V3{port.x, port.y, port.z} + cross(up, forward) * point.side + up * point.up;
}
bool isFinite(V3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
bool isCoincident(V3 a, V3 b) { return length(a - b) <= 0.01f; }
}  // namespace
Result<void> validateConnectionAsset(const RoadConnection& connection) {
    if (!std::isfinite(connection.yawDegrees)) return invalidConnection("connection yaw is not finite");
    if (connection.ports.empty()) {
        if (!connection.vertices.empty() || !connection.triangles.empty() || !connection.assetId.empty())
            return invalidConnection("custom geometry needs explicit connection ports");
        return Result<void>::success();
    }
    if (connection.assetId.empty() || connection.assetId.size() > 256u || connection.ports.size() > 64u ||
        connection.vertices.size() < 3u || connection.vertices.size() > 65536u || connection.triangles.empty() ||
        connection.triangles.size() > 131072u || !std::isfinite(connection.yawDegrees) ||
        std::fabs(connection.yawDegrees) > 180.f)
        return invalidConnection("custom connection identity, transform or mesh budget is invalid");
    std::size_t seamPoints = 0;
    for (const auto& port : connection.ports) seamPoints += port.section.size();
    if (seamPoints > 64u * 128u || seamPoints * connection.vertices.size() > 8u * 1024u * 1024u)
        return invalidConnection("connection seam validation exceeds work budget");
    for (const auto& vertex : connection.vertices) {
        const V3 position{vertex.x, vertex.y, vertex.z}, normal{vertex.nx, vertex.ny, vertex.nz};
        if (!isFinite(position) || length(position) > 10000.f || !isFinite(normal) ||
            std::fabs(length(normal) - 1.f) > 1e-3f || !std::isfinite(vertex.u) || !std::isfinite(vertex.v))
            return invalidConnection("custom mesh vertex is invalid");
    }
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> edges;
    std::vector<bool>                                      referenced(connection.vertices.size(), false);
    for (const auto& triangle : connection.triangles) {
        if (triangle.a >= connection.vertices.size() || triangle.b >= connection.vertices.size() ||
            triangle.c >= connection.vertices.size() || triangle.material > RoadMaterial::TunnelPortalAsphalt)
            return invalidConnection("custom mesh triangle index/material is invalid");
        const auto& a         = connection.vertices[triangle.a];
        const auto& b         = connection.vertices[triangle.b];
        const auto& c         = connection.vertices[triangle.c];
        const V3    geometric = cross(V3{b.x - a.x, b.y - a.y, b.z - a.z}, V3{c.x - a.x, c.y - a.y, c.z - a.z});
        if (length(geometric) < 1e-7f ||
            dot(geometric, V3{a.nx + b.nx + c.nx, a.ny + b.ny + c.ny, a.nz + b.nz + c.nz}) <= 0.f)
            return invalidConnection("custom mesh has degenerate or reversed triangles");
        const std::uint32_t indices[]{triangle.a, triangle.b, triangle.c};
        for (int i = 0; i < 3; ++i) {
            referenced[indices[i]] = true;
            auto key               = std::minmax(indices[i], indices[(i + 1) % 3]);
            if (++edges[{key.first, key.second}] > 2) return invalidConnection("custom mesh has a nonmanifold edge");
        }
    }
    std::unordered_set<std::uint32_t> ids;
    for (const auto& port : connection.ports) {
        const V3 origin{port.x, port.y, port.z}, forward{port.forwardX, port.forwardY, port.forwardZ},
            up{port.upX, port.upY, port.upZ};
        if (port.id == 0 || !ids.insert(port.id).second || !isFinite(origin) || length(origin) < 0.01f ||
            length(origin) > 10000.f || !isFinite(forward) || !isFinite(up) ||
            std::fabs(length(forward) - 1.f) > 1e-4f || std::fabs(length(up) - 1.f) > 1e-4f ||
            std::fabs(dot(forward, up)) > 1e-4f || dot(forward, origin) <= 0.f)
            return invalidConnection("connection port frame must be outward and orthonormal");
        auto section = validateSection(port.section, port.sectionClosed);
        if (!section.ok()) return section;
        std::vector<std::vector<std::uint32_t>> seamVertices;
        for (const auto& point : port.section) {
            const V3                   position = seamPoint(port, point);
            std::vector<std::uint32_t> matches;
            for (std::size_t i = 0; i < connection.vertices.size(); ++i) {
                const auto& vertex = connection.vertices[i];
                if (referenced[i] && isCoincident(position, {vertex.x, vertex.y, vertex.z}) &&
                    (std::fabs(vertex.u) < .01f || std::fabs(vertex.u - 1.f) < .01f || std::fabs(vertex.v) < .01f ||
                     std::fabs(vertex.v - 1.f) < .01f))
                    matches.push_back(static_cast<std::uint32_t>(i));
            }
            if (matches.empty())
                return invalidConnection(
                    "port section must coincide with referenced mesh boundary vertices and UV 0/1");
            seamVertices.push_back(std::move(matches));
        }
        const std::size_t segmentCount = port.sectionClosed ? seamVertices.size() : seamVertices.size() - 1;
        for (std::size_t i = 0; i < segmentCount; ++i) {
            bool boundary = false;
            for (auto a : seamVertices[i])
                for (auto b : seamVertices[(i + 1) % seamVertices.size()]) {
                    const auto key   = std::minmax(a, b);
                    const auto found = edges.find({key.first, key.second});
                    boundary         = boundary || (found != edges.end() && found->second == 1);
                }
            if (!boundary) return invalidConnection("port segment is not an open mesh boundary");
        }
    }
    return Result<void>::success();
}
Result<void> validateConnectionBindings(const RoadNetwork& network, const RoadNode& node) {
    auto asset = validateConnectionAsset(node.connection);
    if (!asset.ok()) return asset;
    if (node.connection.ports.empty()) return Result<void>::success();
    std::unordered_set<std::uint32_t> attached;
    for (const auto& port : node.connection.ports) {
        if (port.edgeId == 0) continue;
        auto edge = network.edgeResult(port.edgeId);
        if (!edge.ok()) return invalidConnection("connection port refers to a stale edge");
        if ((edge.value().from != node.id && edge.value().to != node.id) || !attached.insert(port.edgeId).second)
            return invalidConnection("connection port binds a nonincident or duplicate edge");
    }
    for (const auto& edge : network.edges())
        if ((edge.from == node.id || edge.to == node.id) && !attached.contains(edge.id))
            return invalidConnection("every incident road needs exactly one bound custom port");
    return Result<void>::success();
}
float connectionSocketDistance(const RoadNode& node, const RoadEdge& edge, float pathLength) {
    (void)pathLength;
    for (const auto& port : node.connection.ports)
        if (port.edgeId == edge.id) return length(V3{port.x, port.y, port.z});
    return 0.f;
}
Result<void> validateConnectionSeams(const RoadNetwork& network, const RoadNode& node) {
    auto bindings = validateConnectionBindings(network, node);
    if (!bindings.ok()) return bindings;
    for (const auto& port : node.connection.ports) {
        if (port.edgeId == 0) continue;
        auto edge = network.edgeResult(port.edgeId);
        if (!edge.ok()) return Result<void>::failure(edge.status());
        auto path = edgeToSpline(edge.value());
        if (!path.ok()) return Result<void>::failure(path.status());
        auto pathLength = path.value().lengthResult(24);
        if (!pathLength.ok()) return Result<void>::failure(pathLength.status());
        const bool  outgoing = edge.value().from == node.id;
        const float distance = connectionSocketDistance(node, edge.value(), pathLength.value());
        if (distance >= pathLength.value() - .01f)
            return invalidConnection("custom socket consumes the full road length");
        auto frame = path.value().travelFrameResult(outgoing ? distance : pathLength.value() - distance, "clamp", 24);
        if (!frame.ok()) return Result<void>::failure(frame.status());
        const auto& f = frame.value();
        const V3 origin{f.sample.x, f.sample.y, f.sample.z}, side{f.sideX, f.sideY, f.sideZ}, up{f.upX, f.upY, f.upZ};
        const V3 forward = V3{f.forwardX, f.forwardY, f.forwardZ} * (outgoing ? 1.f : -1.f);
        if (!isCoincident(origin, world({port.x, port.y, port.z}, node)) ||
            dot(forward, rotate({port.forwardX, port.forwardY, port.forwardZ}, node.connection.yawDegrees)) < .9999f ||
            dot(up, rotate({port.upX, port.upY, port.upZ}, node.connection.yawDegrees)) < .9999f)
            return invalidConnection("road spline position, grade or crossfall does not match custom port frame");
        auto profile = makeRoadProfile(edge.value().style, edge.value().lanesForward, edge.value().lanesBackward);
        if (!profile.ok()) return Result<void>::failure(profile.status());
        if (profile.value().points.size() != port.section.size() || profile.value().closed != port.sectionClosed)
            return invalidConnection("custom port section topology differs from road profile");
        for (const auto& p : profile.value().points) {
            const V3 roadPoint =
                origin + side * scaledProfileSide(p.side, asphaltHalfWidth(edge.value()), f.sample.scaleX) + up * p.up;
            const bool found = std::any_of(port.section.begin(), port.section.end(), [&](const auto& point) {
                return isCoincident(roadPoint, world(seamPoint(port, point), node));
            });
            if (!found) return invalidConnection("custom port full section/width differs from the road socket");
        }
    }
    return Result<void>::success();
}
Result<void> bakeCustomConnection(MeshBuild& mesh, const RoadNetwork& network, const RoadNode& node,
                                  const RoadBakeOptions& options) {
    auto seams = validateConnectionSeams(network, node);
    if (!seams.ok()) return seams;
    const std::size_t needed = node.connection.vertices.size() + node.connection.triangles.size() * 3u;
    const std::size_t existing =
        static_cast<std::size_t>(mesh.getVertexCount()) + static_cast<std::size_t>(mesh.getIndexCount());
    if (existing > options.maximumMeshElements || needed > options.maximumMeshElements - existing)
        return invalidConnection("custom connection exceeds mesh output budget");
    const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
    for (const auto& v : node.connection.vertices) {
        const V3 p = world({v.x, v.y, v.z}, node), n = rotate({v.nx, v.ny, v.nz}, node.connection.yawDegrees);
        mesh.addVertex(p.x, p.y, p.z, n.x, n.y, n.z, v.u, v.v);
    }
    for (const auto& tri : node.connection.triangles) {
        mesh.setActiveGroup(roadMaterialGroup(tri.material));
        mesh.addTriangle(base + tri.a, base + tri.b, base + tri.c);
    }
    return Result<void>::success();
}
}  // namespace eve::procgen::road::detail

namespace eve::procgen::road {
Result<void> RoadNetwork::setNodeConnection(std::uint32_t nodeId, RoadConnection connection) {
    const int index = findNodeIndex(nodeId);
    if (index < 0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "unknown connection node", "nodeId"));
    auto candidate                                               = *this;
    candidate.nodes_[static_cast<std::size_t>(index)].connection = std::move(connection);
    auto valid                                                   = candidate.validate();
    if (!valid.ok()) return valid;
    candidate.revision_ = revision_ + 1;
    *this               = std::move(candidate);
    return Result<void>::success();
}
}  // namespace eve::procgen::road
