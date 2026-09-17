#include "procgen/mesh/MeshBoolean.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace eve::procgen {
namespace {

constexpr float kEpsilon = 1e-5f;

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 normalized(Vec3 value) {
    const float length = std::sqrt(dot(value, value));
    return length > kEpsilon ? value * (1.f / length) : Vec3{};
}

struct Vertex {
    Vec3  position;
    Vec3  normal;
    float u = 0.f, v = 0.f;

    Vertex interpolated(const Vertex& other, float t) const {
        Vertex result;
        result.position = position + (other.position - position) * t;
        result.normal   = normalized(normal + (other.normal - normal) * t);
        result.u        = u + (other.u - u) * t;
        result.v        = v + (other.v - v) * t;
        return result;
    }
    void flip() { normal = normal * -1.f; }
};

struct Plane;

struct Polygon {
    std::vector<Vertex> vertices;
    Vec3                planeNormal;
    float               planeW = 0.f;
    int                 group  = -1;
    bool                fromRight = false;

    void recalculatePlane() {
        planeNormal = normalized(cross(vertices[1].position - vertices[0].position,
                                       vertices[2].position - vertices[0].position));
        planeW = dot(planeNormal, vertices[0].position);
    }
    void flip() {
        std::reverse(vertices.begin(), vertices.end());
        for (auto& vertex : vertices) vertex.flip();
        planeNormal = planeNormal * -1.f;
        planeW      = -planeW;
    }
};

enum PolygonType { Coplanar = 0, Front = 1, Back = 2, Spanning = 3 };

void splitPolygon(const Vec3& normal, float w, const Polygon& polygon, std::vector<Polygon>& coplanarFront,
                  std::vector<Polygon>& coplanarBack, std::vector<Polygon>& front, std::vector<Polygon>& back) {
    int              polygonType = Coplanar;
    std::vector<int> types;
    types.reserve(polygon.vertices.size());
    for (const auto& vertex : polygon.vertices) {
        const float distance = dot(normal, vertex.position) - w;
        const int type = distance < -kEpsilon ? Back : (distance > kEpsilon ? Front : Coplanar);
        polygonType |= type;
        types.push_back(type);
    }
    if (polygonType == Coplanar) {
        (dot(normal, polygon.planeNormal) > 0.f ? coplanarFront : coplanarBack).push_back(polygon);
        return;
    }
    if (polygonType == Front) {
        front.push_back(polygon);
        return;
    }
    if (polygonType == Back) {
        back.push_back(polygon);
        return;
    }

    Polygon frontPolygon = polygon;
    Polygon backPolygon  = polygon;
    frontPolygon.vertices.clear();
    backPolygon.vertices.clear();
    for (std::size_t i = 0; i < polygon.vertices.size(); ++i) {
        const std::size_t j  = (i + 1u) % polygon.vertices.size();
        const int         ti = types[i];
        const int         tj = types[j];
        const Vertex&     vi = polygon.vertices[i];
        const Vertex&     vj = polygon.vertices[j];
        if (ti != Back) frontPolygon.vertices.push_back(vi);
        if (ti != Front) backPolygon.vertices.push_back(vi);
        if ((ti | tj) == Spanning) {
            const Vec3 edge        = vj.position - vi.position;
            const float denominator = dot(normal, edge);
            const float t = std::clamp((w - dot(normal, vi.position)) / denominator, 0.f, 1.f);
            const Vertex split = vi.interpolated(vj, t);
            frontPolygon.vertices.push_back(split);
            backPolygon.vertices.push_back(split);
        }
    }
    if (frontPolygon.vertices.size() >= 3u) {
        frontPolygon.recalculatePlane();
        front.push_back(std::move(frontPolygon));
    }
    if (backPolygon.vertices.size() >= 3u) {
        backPolygon.recalculatePlane();
        back.push_back(std::move(backPolygon));
    }
}

class BspNode {
public:
    BspNode() = default;
    explicit BspNode(const std::vector<Polygon>& polygons) { build(polygons); }

    void invert() {
        for (auto& polygon : polygons_) polygon.flip();
        planeNormal_ = planeNormal_ * -1.f;
        planeW_      = -planeW_;
        if (front_) front_->invert();
        if (back_) back_->invert();
        std::swap(front_, back_);
    }

    std::vector<Polygon> clipPolygons(const std::vector<Polygon>& polygons) const {
        if (!hasPlane_) return polygons;
        std::vector<Polygon> front;
        std::vector<Polygon> back;
        for (const auto& polygon : polygons)
            splitPolygon(planeNormal_, planeW_, polygon, front, back, front, back);
        if (front_) front = front_->clipPolygons(front);
        if (back_)
            back = back_->clipPolygons(back);
        else
            back.clear();
        front.insert(front.end(), std::make_move_iterator(back.begin()), std::make_move_iterator(back.end()));
        return front;
    }

    void clipTo(const BspNode& other) {
        polygons_ = other.clipPolygons(polygons_);
        if (front_) front_->clipTo(other);
        if (back_) back_->clipTo(other);
    }

    std::vector<Polygon> allPolygons() const {
        std::vector<Polygon> result = polygons_;
        if (front_) {
            auto values = front_->allPolygons();
            result.insert(result.end(), std::make_move_iterator(values.begin()), std::make_move_iterator(values.end()));
        }
        if (back_) {
            auto values = back_->allPolygons();
            result.insert(result.end(), std::make_move_iterator(values.begin()), std::make_move_iterator(values.end()));
        }
        return result;
    }

    void build(const std::vector<Polygon>& polygons) {
        if (polygons.empty()) return;
        if (!hasPlane_) {
            planeNormal_ = polygons.front().planeNormal;
            planeW_      = polygons.front().planeW;
            hasPlane_    = true;
        }
        std::vector<Polygon> front;
        std::vector<Polygon> back;
        for (const auto& polygon : polygons)
            splitPolygon(planeNormal_, planeW_, polygon, polygons_, polygons_, front, back);
        if (!front.empty()) {
            if (!front_) front_ = std::make_unique<BspNode>();
            front_->build(front);
        }
        if (!back.empty()) {
            if (!back_) back_ = std::make_unique<BspNode>();
            back_->build(back);
        }
    }

private:
    bool                     hasPlane_ = false;
    Vec3                     planeNormal_;
    float                    planeW_ = 0.f;
    std::vector<Polygon>     polygons_;
    std::unique_ptr<BspNode> front_;
    std::unique_ptr<BspNode> back_;
};

Result<std::vector<Polygon>> polygonsFromMesh(const MeshBuild& mesh, bool fromRight) {
    if (mesh.empty() || mesh.getIndexCount() % 3 != 0)
        return Result<std::vector<Polygon>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "mesh.boolean requires non-empty triangle meshes", "mesh",
            {}, "procgen.meshBoolean"));
    if (mesh.getIndexCount() / 3 > 50000)
        return Result<std::vector<Polygon>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "mesh.boolean input exceeds the 50000-triangle budget", "mesh",
            {}, "procgen.meshBoolean"));
    std::vector<Polygon> result;
    result.reserve(static_cast<std::size_t>(mesh.getIndexCount() / 3));
    for (int triangle = 0; triangle < mesh.getIndexCount() / 3; ++triangle) {
        Polygon polygon;
        polygon.group     = mesh.getTriangleGroup(triangle);
        polygon.fromRight = fromRight;
        for (int corner = 0; corner < 3; ++corner) {
            const int index = mesh.getIndex(triangle * 3 + corner);
            if (index < 0 || index >= mesh.getVertexCount())
                return Result<std::vector<Polygon>>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "mesh.boolean found an out-of-range index", "mesh.indices",
                    {}, "procgen.meshBoolean"));
            Vertex vertex{{mesh.getPositionX(index), mesh.getPositionY(index), mesh.getPositionZ(index)},
                          {mesh.getNormalX(index), mesh.getNormalY(index), mesh.getNormalZ(index)},
                          mesh.getUvU(index), mesh.getUvV(index)};
            if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) ||
                !std::isfinite(vertex.position.z))
                return Result<std::vector<Polygon>>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "mesh.boolean requires finite vertex positions",
                    "mesh.positions", {}, "procgen.meshBoolean"));
            polygon.vertices.push_back(vertex);
        }
        polygon.recalculatePlane();
        if (dot(polygon.planeNormal, polygon.planeNormal) <= kEpsilon * kEpsilon)
            return Result<std::vector<Polygon>>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "mesh.boolean rejects degenerate triangles", "mesh.indices",
                {}, "procgen.meshBoolean"));
        result.push_back(std::move(polygon));
    }
    return Result<std::vector<Polygon>>::success(std::move(result));
}

std::vector<Polygon> booleanPolygons(std::vector<Polygon> left, std::vector<Polygon> right,
                                     std::string_view operation) {
    BspNode a(left);
    BspNode b(right);
    if (operation == "union") {
        a.clipTo(b);
        b.clipTo(a);
        b.invert();
        b.clipTo(a);
        b.invert();
        a.build(b.allPolygons());
        return a.allPolygons();
    }
    if (operation == "difference") {
        a.invert();
        a.clipTo(b);
        b.clipTo(a);
        b.invert();
        b.clipTo(a);
        b.invert();
        a.build(b.allPolygons());
        a.invert();
        return a.allPolygons();
    }
    a.invert();
    b.clipTo(a);
    b.invert();
    a.clipTo(b);
    b.clipTo(a);
    a.build(b.allPolygons());
    a.invert();
    return a.allPolygons();
}

MeshBuild meshFromPolygons(const std::vector<Polygon>& polygons, const MeshBuild& left, const MeshBuild& right,
                           std::string_view operation) {
    MeshBuild output;
    std::vector<int> leftGroups;
    std::vector<int> rightGroups;
    for (int i = 0; i < left.getGroupCount(); ++i) leftGroups.push_back(output.setActiveGroup(left.getGroupName(i)));
    for (int i = 0; i < right.getGroupCount(); ++i)
        rightGroups.push_back(output.setActiveGroup("cutter." + right.getGroupName(i)));
    const int leftDefault  = output.setActiveGroup("boolean.left");
    const int rightDefault = output.setActiveGroup("boolean.cutter");
    for (const auto& polygon : polygons) {
        if (polygon.vertices.size() < 3u) continue;
        const auto& groups = polygon.fromRight ? rightGroups : leftGroups;
        const int fallback = polygon.fromRight ? rightDefault : leftDefault;
        const int group = polygon.group >= 0 && static_cast<std::size_t>(polygon.group) < groups.size()
                              ? groups[static_cast<std::size_t>(polygon.group)]
                              : fallback;
        output.setActiveGroup(output.getGroupName(group));
        const auto base = static_cast<std::uint32_t>(output.getVertexCount());
        for (const auto& vertex : polygon.vertices)
            output.addVertex(vertex.position.x, vertex.position.y, vertex.position.z, vertex.normal.x,
                             vertex.normal.y, vertex.normal.z, vertex.u, vertex.v);
        for (std::uint32_t i = 1; i + 1 < polygon.vertices.size(); ++i)
            output.addTriangle(base, base + i, base + i + 1u);
    }
    output.setMeta("generator", "mesh.boolean");
    output.setMeta("boolean.operation", std::string(operation));
    return output;
}

}  // namespace

Result<MeshBuild> meshBooleanResult(const MeshBuild& left, const MeshBuild& right, std::string_view operation) {
    if (operation != "union" && operation != "difference" && operation != "intersection")
        return Result<MeshBuild>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "mesh.boolean operation must be union, difference, or intersection",
            "operation", {}, "procgen.meshBoolean"));
    auto leftPolygons = polygonsFromMesh(left, false);
    if (!leftPolygons.ok()) return Result<MeshBuild>::failure(leftPolygons.status());
    auto rightPolygons = polygonsFromMesh(right, true);
    if (!rightPolygons.ok()) return Result<MeshBuild>::failure(rightPolygons.status());
    auto polygons = booleanPolygons(std::move(leftPolygons).takeValue(), std::move(rightPolygons).takeValue(), operation);
    return Result<MeshBuild>::success(meshFromPolygons(polygons, left, right, operation));
}

}  // namespace eve::procgen
