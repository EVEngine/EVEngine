#include <cmath>
#include <limits>
#include "physics/box3d/Box3DBody.h"
#include "physics/snapshot/WorldSnapshotAccess.h"
#include "physics/snapshot/WorldSnapshotCodec.h"

namespace eve::physics {
namespace {
using namespace snapshot_detail;
template <class T>
Result<T> invalid(const char* message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "payload.shapes.contact"));
}
Value surfaceValue(b3SurfaceMaterial m) {
    return Value(Value::Object{{"values", Value(Value::Array{Value(m.friction), Value(m.restitution),
                                                             Value(m.rollingResistance), Value(m.tangentVelocity.x),
                                                             Value(m.tangentVelocity.y), Value(m.tangentVelocity.z)})},
                               {"id", Value(std::to_string(m.userMaterialId))},
                               {"color", Value(std::to_string(m.customColor))}});
}
Result<b3SurfaceMaterial> parseSurface(const Value& value) {
    const auto* o = value.getIf<Value::Object>();
    if (!o || !hasExactFields(*o, {"values", "id", "color"}))
        return invalid<b3SurfaceMaterial>("Invalid surface fields");
    auto values = readFloatArray(*o, "values");
    if (!values) return Result<b3SurfaceMaterial>::failure(values.status());
    if (values.value().size() != 6 || values.value()[0] < 0 || values.value()[1] < 0 || values.value()[2] < 0)
        return invalid<b3SurfaceMaterial>("Invalid surface coefficients");
    auto id = readUint64(*o, "id");
    if (!id) return Result<b3SurfaceMaterial>::failure(id.status());
    auto color = readUint64(*o, "color");
    if (!color) return Result<b3SurfaceMaterial>::failure(color.status());
    if (color.value() > std::numeric_limits<std::uint32_t>::max())
        return invalid<b3SurfaceMaterial>("Color out of range");
    const auto& n = values.value();
    return Result<b3SurfaceMaterial>::success(
        {n[0], n[1], n[2], {n[3], n[4], n[5]}, id.value(), static_cast<std::uint32_t>(color.value())});
}
bool finite(b3Matrix3 m) { return b3IsValidVec3(m.cx) && b3IsValidVec3(m.cy) && b3IsValidVec3(m.cz); }
}  // namespace
Value WorldSnapshotAccess::shapeContact(const Shape3D& shape) {
    const auto   id     = eve::physics::box3d::Access::shapeId(shape);
    const auto   filter = b3Shape_GetFilter(id);
    Value::Array mesh;
    for (int i = 0; shape.geometry_.kind == Shape3D::Kind::TriangleMesh && i < b3Shape_GetMeshMaterialCount(id); ++i)
        mesh.push_back(surfaceValue(b3Shape_GetMeshSurfaceMaterial(id, i)));
    return Value(Value::Object{
        {"density", Value(b3Shape_GetDensity(id))},
        {"surface", surfaceValue(b3Shape_GetSurfaceMaterial(id))},
        {"sensor", Value(shape.isSensor())},
        {"category", Value(std::to_string(filter.categoryBits))},
        {"mask", Value(std::to_string(filter.maskBits))},
        {"group", Value(std::to_string(filter.groupIndex))},
        {"tag", Value(std::to_string(shape.tag_))},
        {"explosionScale", Value(shape.explosionScale_)},
        {"sensorEvents", Value(b3Shape_AreSensorEventsEnabled(id))},
        {"contactEvents", Value(b3Shape_AreContactEventsEnabled(id))},
        {"preSolveEvents", Value(b3Shape_ArePreSolveEventsEnabled(id))},
        {"hitEvents", Value(b3Shape_AreHitEventsEnabled(id))},
        {"contactOverride", Value(shape.contactOverrideFiltering_)},
        {"oneWay", Value(shape.oneWayEnabled_)},
        {"oneWayParameters", Value(Value::Array{Value(shape.oneWayLocalNormalX_), Value(shape.oneWayLocalNormalY_),
                                                Value(shape.oneWayLocalNormalZ_), Value(shape.oneWayPlaneOffset_),
                                                Value(shape.oneWayMargin_), Value(shape.oneWayMinNormalDot_)})},
        {"meshMaterials", Value(std::move(mesh))}});
}
Result<float> WorldSnapshotAccess::restoreShapeContact(Shape3D& shape, const Value::Object& source) {
    using namespace snapshot_detail;
    const auto* value = field(source, "contact");
    const auto* o     = value ? value->getIf<Value::Object>() : nullptr;
    if (!o || !hasExactFields(*o, {"density", "surface", "sensor", "category", "mask", "group", "tag", "explosionScale",
                                   "sensorEvents", "contactEvents", "preSolveEvents", "hitEvents", "contactOverride",
                                   "oneWay", "oneWayParameters", "meshMaterials"}))
        return invalid<float>("Unknown or missing contact fields");
    auto density = readFloat(*o, "density");
    if (!density) return Result<float>::failure(density.status());
    auto explosion = readFloat(*o, "explosionScale");
    if (!explosion) return Result<float>::failure(explosion.status());
    if (density.value() < 0 || explosion.value() < 0)
        return invalid<float>("Density and explosion scale must be nonnegative");
    auto surface = parseSurface(*field(*o, "surface"));
    if (!surface) return Result<float>::failure(surface.status());
    auto category = readUint64(*o, "category");
    if (!category) return Result<float>::failure(category.status());
    auto mask = readUint64(*o, "mask");
    if (!mask) return Result<float>::failure(mask.status());
    auto group = readInt64(*o, "group");
    if (!group) return Result<float>::failure(group.status());
    auto tag = readInt64(*o, "tag");
    if (!tag) return Result<float>::failure(tag.status());
    if (group.value() < INT32_MIN || group.value() > INT32_MAX || tag.value() < INT32_MIN || tag.value() > INT32_MAX)
        return invalid<float>("Group or tag out of range");
    std::array<bool, 7> flags{};
    int                 index = 0;
    for (const char* name :
         {"sensor", "sensorEvents", "contactEvents", "preSolveEvents", "hitEvents", "contactOverride", "oneWay"}) {
        auto parsed = readBool(*o, name);
        if (!parsed) return Result<float>::failure(parsed.status());
        flags[index++] = parsed.value();
    }
    auto plane = readFloatArray(*o, "oneWayParameters");
    if (!plane) return Result<float>::failure(plane.status());
    const auto& p = plane.value();
    if (p.size() != 6 || !b3IsNormalized({p[0], p[1], p[2]}) || p[4] < 0 || p[5] < -1 || p[5] > 1)
        return invalid<float>("Invalid one-way plane");
    const auto* mesh = field(*o, "meshMaterials")->getIf<Value::Array>();
    if (!mesh) return invalid<float>("Mesh materials must be an array");
    std::vector<b3SurfaceMaterial> materials;
    for (const auto& item : *mesh) {
        auto parsed = parseSurface(item);
        if (!parsed) return Result<float>::failure(parsed.status());
        materials.push_back(parsed.value());
    }
    const auto* indices = field(source, "meshMaterialIndices")->getIf<Value::Array>();
    if (!indices) return invalid<float>("Mesh material indices must be an array");
    std::vector<std::uint8_t> encoded;
    int                       required = 1;
    for (const auto& item : *indices) {
        Value::Object wrapped{{"index", item}};
        auto          parsed = readInt64(wrapped, "index");
        if (!parsed) return Result<float>::failure(parsed.status());
        if (parsed.value() < 0 || parsed.value() > 254) return invalid<float>("Mesh material index out of range");
        encoded.push_back(static_cast<std::uint8_t>(parsed.value()));
        required = std::max(required, static_cast<int>(parsed.value()) + 1);
    }
    if (shape.geometry_.kind == Shape3D::Kind::TriangleMesh) {
        if ((!encoded.empty() && encoded.size() != shape.geometry_.meshIndices.size() / 3) ||
            materials.size() != static_cast<std::size_t>(required))
            return invalid<float>("Mesh material topology mismatch");
    } else if (!encoded.empty() || !materials.empty())
        return invalid<float>("Non-mesh shape has mesh materials");
    // Candidate geometry still uses construction density. All density assignments are deferred until every recreation
    // is done.
    shape.explosionScale_           = explosion.value();
    shape.tag_                      = static_cast<int>(tag.value());
    shape.contactOverrideFiltering_ = flags[5];
    shape.hitEventsEnabled_         = flags[4];
    shape.oneWayEnabled_            = flags[6];
    shape.oneWayLocalNormalX_       = p[0];
    shape.oneWayLocalNormalY_       = p[1];
    shape.oneWayLocalNormalZ_       = p[2];
    shape.oneWayPlaneOffset_        = p[3];
    shape.oneWayMargin_             = p[4];
    shape.oneWayMinNormalDot_       = p[5];
    if (shape.geometry_.kind == Shape3D::Kind::TriangleMesh) {
        shape.geometry_.meshMaterialIndices = std::move(encoded);
        shape.meshMaterials_.clear();
        for (const auto& m : materials)
            shape.meshMaterials_.push_back({m.friction,
                                            m.restitution,
                                            m.rollingResistance,
                                            {m.tangentVelocity.x, m.tangentVelocity.y, m.tangentVelocity.z},
                                            m.userMaterialId,
                                            m.customColor});
        shape.meshMaterialsDirty_ = true;
    }
    shape.recreate(flags[0]);
    const auto raw = eve::physics::box3d::Access::shapeId(shape);
    b3Shape_SetSurfaceMaterial(raw, surface.value());
    b3Shape_SetFilter(raw, {category.value(), mask.value(), static_cast<int>(group.value())}, false);
    b3Shape_EnableSensorEvents(raw, flags[1]);
    b3Shape_EnableContactEvents(raw, flags[2]);
    b3Shape_EnablePreSolveEvents(raw, flags[3]);
    b3Shape_EnableHitEvents(raw, flags[4]);
    shape.refreshOneWayWorldData();
    return Result<float>::success(density.value());
}
Result<void> WorldSnapshotAccess::finishShapeMass(World3D& world) {
    for (auto* body : world.bodies_) {
        if (b3Body_GetType(eve::physics::box3d::Access::bodyId(*body)) != b3_dynamicBody) continue;
        std::vector<b3ShapeId> shapes(
            static_cast<std::size_t>(b3Body_GetShapeCount(eve::physics::box3d::Access::bodyId(*body))));
        const int               count = b3Body_GetShapes(eve::physics::box3d::Access::bodyId(*body), shapes.data(),
                                                         static_cast<int>(shapes.size()));
        std::vector<b3MassData> masses;
        float                   mass = 0;
        b3Vec3                  center{};
        for (int i = 0; i < count; ++i) {
            if (b3Shape_GetDensity(shapes[i]) == 0) continue;
            const auto data = b3Shape_ComputeMassData(shapes[i]);
            if (!std::isfinite(data.mass) || data.mass < 0 || !b3IsValidVec3(data.center) || !finite(data.inertia))
                return invalid<void>("Shape mass or inertia exceeds finite range");
            mass += data.mass;
            center = b3MulAdd(center, data.mass, data.center);
            if (!std::isfinite(mass) || !b3IsValidVec3(center)) return invalid<void>("Body mass accumulation overflow");
            masses.push_back(data);
        }
        if (mass > 0) {
            const float inverse = 1.f / mass;
            if (!std::isfinite(inverse)) return invalid<void>("Inverse body mass overflow");
            center = b3MulSV(inverse, center);
        }
        b3Matrix3 inertia = b3Mat3_zero;
        for (const auto& data : masses) {
            if (data.mass == 0) continue;
            inertia = b3AddMM(inertia, b3AddMM(data.inertia, b3Steiner(data.mass, b3Sub(center, data.center))));
            if (!finite(inertia)) return invalid<void>("Body inertia accumulation overflow");
        }
        const float determinant = b3Det(inertia);
        if (!std::isfinite(determinant) || determinant < 0 || (determinant > 0 && !finite(b3InvertT(inertia))))
            return invalid<void>("Body inertia cannot be inverted safely");
        if (b3Body_GetShapeCount(box3d::Access::bodyId(*body)) > 0)
            b3Body_ApplyMassFromShapes(box3d::Access::bodyId(*body));
    }
    return Result<void>::success();
}
}  // namespace eve::physics
