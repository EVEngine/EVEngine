#include "scene/editing/SceneAuthoring.h"
#include "scene/editing/SceneTarget.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <set>

namespace eve::scene_editing {
namespace {
using namespace eve::editing;
const Value* field(const Value& value, const char* key) {
    const auto* object = value.getIf<Value::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}
bool number(const Value* value, double& out) {
    if (!value) return false;
    if (const auto* real = value->getIf<double>())
        out = *real;
    else if (const auto* integer = value->getIf<std::int64_t>())
        out = static_cast<double>(*integer);
    else
        return false;
    return std::isfinite(out);
}
bool vector(const Value* value, glm::dvec3& out) {
    const auto* values = value ? value->getIf<Value::Array>() : nullptr;
    return values && values->size() == 3 && number(&(*values)[0], out.x) && number(&(*values)[1], out.y) &&
           number(&(*values)[2], out.z);
}
bool boundsOf(const Value* value, AuthoringBounds& out) {
    const auto* values = value ? value->getIf<Value::Array>() : nullptr;
    if (!values || values->size() != 6) return false;
    for (std::size_t i = 0; i < 6; ++i)
        if (!number(&(*values)[i], out[i])) return false;
    return out[0] <= out[3] && out[1] <= out[4] && out[2] <= out[5];
}
Value              point(const glm::dvec3& p) { return Value::Array{p.x, p.y, p.z}; }
Result<glm::dmat4> worldMatrix(const ISceneHierarchyEditTarget& target, ObjectId id) {
    glm::dmat4         world(1.0);
    std::set<ObjectId> visited;
    while (!id.empty()) {
        if (!visited.insert(id).second)
            return failed<glm::dmat4>(Status::Rejected, RuleId("scene.authoring.cycle"), "Hierarchy contains a cycle");
        auto object = target.sceneObject(id);
        if (!object.ok()) return Result<glm::dmat4>::failure(object.status());
        const auto& t     = object.value().transform;
        auto        local = glm::translate(glm::dmat4(1.0), glm::dvec3(t.x, t.y, t.z));
        local             = glm::rotate(local, t.rotationY, glm::dvec3(0, 1, 0));
        local             = glm::rotate(local, t.rotationX, glm::dvec3(1, 0, 0));
        local             = glm::rotate(local, t.rotationZ, glm::dvec3(0, 0, 1));
        local             = glm::scale(local, glm::dvec3(t.scaleX, t.scaleY, t.scaleZ));
        world             = local * world;
        id                = object.value().parent;
    }
    return applied<glm::dmat4>(world);
}
struct Geometry {
    glm::dvec3   minimum, maximum;
    Value::Array corners;
    glm::dmat4   matrix;
};
Result<Geometry> geometry(const ISceneHierarchyEditTarget& target, const ObjectId& id, const AuthoringBounds& bounds) {
    if (id.empty())
        return failed<Geometry>(Status::Rejected, RuleId("scene.authoring.object"), "Object id is required");
    for (double value : bounds)
        if (!std::isfinite(value))
            return failed<Geometry>(Status::Rejected, RuleId("scene.authoring.bounds"), "Bounds must be finite");
    for (std::size_t axis = 0; axis < 3; ++axis)
        if (bounds[axis] > bounds[axis + 3])
            return failed<Geometry>(Status::Rejected, RuleId("scene.authoring.bounds"), "Bounds must be ordered");
    auto world = worldMatrix(target, id);
    if (!world.ok()) return Result<Geometry>::failure(world.status());
    Geometry result{glm::dvec3(std::numeric_limits<double>::max()),
                    glm::dvec3(std::numeric_limits<double>::lowest()),
                    {},
                    world.value()};
    for (int i = 0; i < 8; ++i) {
        const glm::dvec3 local(bounds[(i & 1) ? 3 : 0], bounds[(i & 2) ? 4 : 1], bounds[(i & 4) ? 5 : 2]);
        const auto       p = glm::dvec3(result.matrix * glm::dvec4(local, 1));
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(p[axis]))
                return failed<Geometry>(Status::Rejected, RuleId("scene.authoring.world-overflow"),
                                        "World transform overflow");
        result.minimum = glm::min(result.minimum, p);
        result.maximum = glm::max(result.maximum, p);
        result.corners.push_back(point(p));
    }
    return applied<Geometry>(std::move(result));
}
Result<CommandPlan> planPlacement(IEditableTarget& target, const CommandRequest& request, const std::string& mode) {
    auto        hierarchy  = target.capability<ISceneHierarchyEditTarget>();
    auto        transforms = target.capability<ITransformEditTarget>();
    const auto* idValue    = field(request.payload, "object");
    const auto* id         = idValue ? idValue->getIf<std::string>() : nullptr;
    const auto  reject     = [](const char* message) {
        return failed<CommandPlan>(Status::Rejected, RuleId("scene.authoring.placement-payload"), message);
    };
    if (!hierarchy || !transforms)
        return failed<CommandPlan>(Status::Unsupported, RuleId("scene.authoring.target"),
                                   "Hierarchy and transform editing capabilities are required");
    if (!id || id->empty()) return reject("Object id is required");
    const auto*           payload = request.payload.getIf<Value::Object>();
    std::set<std::string> keys{"object", "bounds"};
    if (mode == "ground") keys.insert("height");
    if (mode == "align") keys.insert({"reference", "referenceBounds", "axis", "gap"});
    if (mode == "screen") keys = {"object", "anchor", "eye", "target", "up", "fov", "aspect", "screen"};
    for (const auto& [key, value] : *payload) {
        (void)value;
        if (!keys.contains(key)) return reject("Unknown placement field");
    }
    auto current = hierarchy->get().sceneObject(ObjectId(*id));
    if (!current.ok()) return Result<CommandPlan>::failure(current.status());
    auto world = worldMatrix(hierarchy->get(), ObjectId(*id));
    if (!world.ok()) return Result<CommandPlan>::failure(world.status());
    glm::dvec3 delta(0);
    if (mode == "screen") {
        glm::dvec3  eye, targetPoint, up, anchor;
        double      fov = 0, aspect = 0;
        const auto* screenValue = field(request.payload, "screen");
        const auto* screen      = screenValue ? screenValue->getIf<Value::Array>() : nullptr;
        double      u = 0, v = 0;
        if (!vector(field(request.payload, "eye"), eye) || !vector(field(request.payload, "target"), targetPoint) ||
            !vector(field(request.payload, "up"), up) || !vector(field(request.payload, "anchor"), anchor) ||
            !number(field(request.payload, "fov"), fov) || !number(field(request.payload, "aspect"), aspect) ||
            !screen || screen->size() != 2 || !number(&(*screen)[0], u) || !number(&(*screen)[1], v) || fov <= 0 ||
            fov >= 179 || aspect <= 0 || u < 0 || u > 1 || v < 0 || v > 1)
            return reject("Screen placement requires a valid perspective camera and normalized UV");
        auto forward = targetPoint - eye;
        if (glm::length(forward) < 1e-8 || glm::length(up) < 1e-8) return reject("Camera basis is degenerate");
        forward    = glm::normalize(forward);
        auto right = glm::cross(forward, glm::normalize(up));
        if (glm::length(right) < 1e-8) return reject("Camera up is parallel to viewing direction");
        right              = glm::normalize(right);
        up                 = glm::cross(right, forward);
        const auto   p     = glm::dvec3(world.value() * glm::dvec4(anchor, 1));
        const double depth = glm::dot(p - eye, forward);
        if (depth <= 1e-8) return reject("Anchor must be in front of the camera");
        const double halfHeight = depth * std::tan(glm::radians(fov) * 0.5);
        delta =
            eye + forward * depth + right * ((2 * u - 1) * halfHeight * aspect) + up * ((1 - 2 * v) * halfHeight) - p;
    } else {
        AuthoringBounds bounds{};
        if (!boundsOf(field(request.payload, "bounds"), bounds)) return reject("Asset bounds are required");
        auto objectGeometry = geometry(hierarchy->get(), ObjectId(*id), bounds);
        if (!objectGeometry.ok()) return Result<CommandPlan>::failure(objectGeometry.status());
        if (mode == "ground") {
            double height = 0;
            if (!number(field(request.payload, "height"), height)) return reject("Ground height must be finite");
            delta.y = height - objectGeometry.value().minimum.y;
        } else {
            const auto*     refValue  = field(request.payload, "reference");
            const auto*     ref       = refValue ? refValue->getIf<std::string>() : nullptr;
            const auto*     axisValue = field(request.payload, "axis");
            const auto*     axis      = axisValue ? axisValue->getIf<std::string>() : nullptr;
            double          gap       = 0;
            AuthoringBounds referenceBounds{};
            if (!ref || *ref == *id || !axis || (*axis != "x" && *axis != "y" && *axis != "z") ||
                !number(field(request.payload, "gap"), gap) ||
                !boundsOf(field(request.payload, "referenceBounds"), referenceBounds))
                return reject("Alignment requires a different reference, its bounds, axis and finite gap");
            // Moving an ancestor would move the reference too, invalidating the solved gap.
            auto ancestor = ObjectId(*ref);
            while (!ancestor.empty()) {
                if (ancestor == ObjectId(*id)) return reject("Reference cannot descend from the moved object");
                auto entry = hierarchy->get().sceneObject(ancestor);
                if (!entry.ok()) return Result<CommandPlan>::failure(entry.status());
                ancestor = entry.value().parent;
            }
            auto referenceGeometry = geometry(hierarchy->get(), ObjectId(*ref), referenceBounds);
            if (!referenceGeometry.ok()) return Result<CommandPlan>::failure(referenceGeometry.status());
            const int index = *axis == "x" ? 0 : *axis == "y" ? 1 : 2;
            delta[index]    = referenceGeometry.value().maximum[index] + gap - objectGeometry.value().minimum[index];
        }
    }
    auto parent = worldMatrix(hierarchy->get(), current.value().parent);
    if (!parent.ok()) return Result<CommandPlan>::failure(parent.status());
    const double determinant = glm::determinant(parent.value());
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12) return reject("Parent transform is singular");
    const auto localDelta = glm::dvec3(glm::inverse(parent.value()) * glm::dvec4(delta, 0));
    auto       transform  = current.value().transform;
    transform.x += localDelta.x;
    transform.y += localDelta.y;
    transform.z += localDelta.z;
    auto operation = transforms->get().makeSetTransform(ObjectId(*id), transform);
    if (!operation.ok()) return Result<CommandPlan>::failure(operation.status());
    CommandPlan plan;
    plan.operations.push_back(std::move(operation.value()));
    plan.summary = Value::Object{{"object", *id}, {"constraint", mode}, {"worldDisplacement", point(delta)}};
    return applied<CommandPlan>(std::move(plan));
}
}  // namespace

Result<EditorValue> inspectAuthoringObject(const ISceneHierarchyEditTarget& target, const ObjectId& object,
                                           const AuthoringBounds& bounds) {
    auto result = geometry(target, object, bounds);
    if (!result.ok()) return Result<EditorValue>::failure(result.status());
    const auto&        g = result.value();
    EditorValue::Array matrix;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row) matrix.emplace_back(g.matrix[column][row]);
    const auto center = (g.minimum + g.maximum) * 0.5;
    return editing::applied<EditorValue>(
        EditorValue::Object{{"worldMatrix", std::move(matrix)},
                            {"worldMinimum", point(g.minimum)},
                            {"worldMaximum", point(g.maximum)},
                            {"worldSize", point(g.maximum - g.minimum)},
                            {"worldCorners", g.corners},
                            {"groundAnchor", point(glm::dvec3(center.x, g.minimum.y, center.z))}});
}
Result<void> registerAuthoringCommands(editing::IEditingCommandRegistry& registry) {
    for (const std::string mode : {"ground", "align", "screen"}) {
        editing::EditingCommandDescriptor descriptor;
        descriptor.id                = editing::CommandId("scene.placement." + mode + ".v1");
        descriptor.ownerModule       = "scene_editing";
        descriptor.displayName       = "Place scene object: " + mode;
        descriptor.category          = "Scene";
        descriptor.automationAllowed = true;
        auto registered              = registry.registerPlannedCommand(
            std::move(descriptor), [mode](editing::IEditableTarget& target, const editing::CommandRequest& request) {
                return planPlacement(target, request, mode);
            });
        if (!registered.ok()) return registered;
    }
    return editing::applied<void>();
}
}  // namespace eve::scene_editing
