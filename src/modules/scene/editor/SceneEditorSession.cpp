#include "scene/editor/SceneEditorSession.h"

#include "editing/EditingValueJson.h"
#include "editor/EditorCommandService.h"
#include "editor/EditorTargetCoordinator.h"
#include "scene/editing/SceneTarget.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

namespace eve::scene_editor {
namespace {
struct SceneGraphNode {
    std::string                        parent;
    scene_editing::SceneTransformValue local;
};

using SceneGraph = std::unordered_map<std::string, SceneGraphNode>;

const editing::Value* valueField(const editing::Value& value, const char* key) {
    const auto* object = value.getIf<editing::Value::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

editing::Result<SceneGraph> sceneGraphOf(const editing::Value& snapshot) {
    const auto* root = snapshot.getIf<editing::Value::Object>();
    if (!root)
        return editing::failed<SceneGraph>(editing::Status::Failed, editing::RuleId("scene.physics-placement.snapshot"),
                                           "Scene snapshot is not an object");
    const auto  found   = root->find("objects");
    const auto* objects = found != root->end() ? found->second.getIf<editing::Value::Array>() : nullptr;
    if (!objects)
        return editing::failed<SceneGraph>(editing::Status::Failed, editing::RuleId("scene.physics-placement.snapshot"),
                                           "Scene snapshot has no objects array");
    SceneGraph graph;
    for (const auto& value : *objects) {
        const auto* idValue        = valueField(value, "id");
        const auto* parentValue    = valueField(value, "parent");
        const auto* transformValue = valueField(value, "transform");
        const auto* id             = idValue ? idValue->getIf<std::string>() : nullptr;
        const auto* parent         = parentValue ? parentValue->getIf<std::string>() : nullptr;
        const auto* transform      = transformValue ? transformValue->getIf<editing::Value::Object>() : nullptr;
        if (!id || !parent || !transform)
            return editing::failed<SceneGraph>(editing::Status::Failed,
                                               editing::RuleId("scene.physics-placement.snapshot-object"),
                                               "Scene snapshot contains an incomplete object");
        SceneGraphNode node;
        node.parent       = *parent;
        const auto assign = [&](const char* key, double& destination) {
            const auto  entry  = transform->find(key);
            const auto* number = entry != transform->end() ? entry->second.getIf<double>() : nullptr;
            if (!number) return false;
            destination = *number;
            return true;
        };
        if (!assign("x", node.local.x) || !assign("y", node.local.y) || !assign("z", node.local.z) ||
            !assign("rotationX", node.local.rotationX) || !assign("rotationY", node.local.rotationY) ||
            !assign("rotationZ", node.local.rotationZ) || !assign("scaleX", node.local.scaleX) ||
            !assign("scaleY", node.local.scaleY) || !assign("scaleZ", node.local.scaleZ))
            return editing::failed<SceneGraph>(editing::Status::Failed,
                                               editing::RuleId("scene.physics-placement.snapshot-transform"),
                                               "Scene snapshot contains an incomplete transform");
        graph.emplace(*id, std::move(node));
    }
    return editing::applied<SceneGraph>(std::move(graph));
}

glm::dmat4 matrixOf(const scene_editing::SceneTransformValue& value) {
    const auto rotation = glm::dquat(glm::dvec3(value.rotationX, value.rotationY, value.rotationZ));
    return glm::translate(glm::dmat4(1.0), glm::dvec3(value.x, value.y, value.z)) * glm::mat4_cast(rotation) *
           glm::scale(glm::dmat4(1.0), glm::dvec3(value.scaleX, value.scaleY, value.scaleZ));
}

editing::Result<glm::dmat4> worldMatrixOf(const std::string& id, const SceneGraph& graph,
                                          std::unordered_map<std::string, glm::dmat4>& cache,
                                          std::unordered_set<std::string>&             visiting) {
    if (const auto cached = cache.find(id); cached != cache.end()) return editing::applied<glm::dmat4>(cached->second);
    const auto node = graph.find(id);
    if (node == graph.end())
        return editing::failed<glm::dmat4>(editing::Status::NotFound,
                                           editing::RuleId("scene.physics-placement.object-parent"),
                                           "Placement object is missing from the scene hierarchy");
    if (!visiting.insert(id).second)
        return editing::failed<glm::dmat4>(editing::Status::Failed,
                                           editing::RuleId("scene.physics-placement.hierarchy-cycle"),
                                           "Scene hierarchy contains a cycle");
    glm::dmat4 world = matrixOf(node->second.local);
    if (!node->second.parent.empty()) {
        auto parent = worldMatrixOf(node->second.parent, graph, cache, visiting);
        if (!parent.ok()) return editing::Result<glm::dmat4>::failure(parent.status());
        world = parent.value() * world;
    }
    visiting.erase(id);
    cache.insert_or_assign(id, world);
    return editing::applied<glm::dmat4>(world);
}

editing::Result<scene_editing::SceneTransformValue> transformOf(const glm::dmat4& matrix) {
    glm::dvec3 scale;
    glm::dquat rotation;
    glm::dvec3 translation;
    glm::dvec3 skew;
    glm::dvec4 perspective;
    if (!glm::decompose(matrix, scale, rotation, translation, skew, perspective) || glm::length(skew) > 1.0e-7 ||
        std::abs(perspective.x) > 1.0e-9 || std::abs(perspective.y) > 1.0e-9 || std::abs(perspective.z) > 1.0e-9 ||
        std::abs(perspective.w - 1.0) > 1.0e-9) {
        return editing::failed<scene_editing::SceneTransformValue>(
            editing::Status::Rejected, editing::RuleId("scene.physics-placement.sheared-transform"),
            "Physics placement cannot represent a sheared hierarchy transform as TRS");
    }
    const auto                         euler = glm::eulerAngles(glm::normalize(rotation));
    scene_editing::SceneTransformValue value;
    value.x         = translation.x;
    value.y         = translation.y;
    value.z         = translation.z;
    value.rotationX = euler.x;
    value.rotationY = euler.y;
    value.rotationZ = euler.z;
    value.scaleX    = scale.x;
    value.scaleY    = scale.y;
    value.scaleZ    = scale.z;
    return editing::applied<scene_editing::SceneTransformValue>(value);
}

bool spansVolume(const std::vector<float>& vertices) {
    constexpr double epsilonSquared = 1.0e-12;
    const auto       point          = [&](std::size_t index) {
        return std::array<double, 3>{vertices[index], vertices[index + 1], vertices[index + 2]};
    };
    const auto subtract = [](const auto& left, const auto& right) {
        return std::array<double, 3>{left[0] - right[0], left[1] - right[1], left[2] - right[2]};
    };
    const auto cross = [](const auto& left, const auto& right) {
        return std::array<double, 3>{left[1] * right[2] - left[2] * right[1], left[2] * right[0] - left[0] * right[2],
                                     left[0] * right[1] - left[1] * right[0]};
    };
    const auto dot = [](const auto& left, const auto& right) {
        return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
    };
    const auto            origin = point(0);
    std::array<double, 3> first{};
    std::array<double, 3> normal{};
    bool                  hasFirst  = false;
    bool                  hasNormal = false;
    for (std::size_t index = 3; index < vertices.size(); index += 3) {
        const auto edge = subtract(point(index), origin);
        if (!hasFirst) {
            if (dot(edge, edge) <= epsilonSquared) continue;
            first    = edge;
            hasFirst = true;
            continue;
        }
        if (!hasNormal) {
            const auto candidate = cross(first, edge);
            if (dot(candidate, candidate) <= epsilonSquared) continue;
            normal    = candidate;
            hasNormal = true;
            continue;
        }
        if (std::abs(dot(normal, edge)) > 1.0e-9) return true;
    }
    return false;
}

const char* shapeName(PhysicsPlacementShape shape) {
    switch (shape) {
        case PhysicsPlacementShape::Box: return "box";
        case PhysicsPlacementShape::Sphere: return "sphere";
        case PhysicsPlacementShape::Capsule: return "capsule";
        case PhysicsPlacementShape::ConvexHull: return "convex";
        case PhysicsPlacementShape::Auto: return "auto";
    }
    return "box";
}

editing::Value colliderValue(const PhysicsPlacementCollider& collider) {
    editing::Value::Array vertices;
    vertices.reserve(collider.convexVertices.size());
    for (const float value : collider.convexVertices) vertices.emplace_back(static_cast<double>(value));
    return editing::Value::Object{
        {"shape", shapeName(collider.shape)},
        {"source", collider.source == PhysicsPlacementColliderSource::Existing ? "existing" : "generated"},
        {"halfExtents", editing::Value::Array{collider.halfExtentX, collider.halfExtentY, collider.halfExtentZ}},
        {"localPosition", editing::Value::Array{collider.localX, collider.localY, collider.localZ}},
        {"localRotation",
         editing::Value::Array{collider.localRotationX, collider.localRotationY, collider.localRotationZ}},
        {"vertices", std::move(vertices)},
        {"maxVertices", static_cast<std::int64_t>(collider.convexMaxVertices)},
    };
}

bool valueNumber(const editing::Value& value, double& output) {
    if (const auto* real = value.getIf<double>())
        output = *real;
    else if (const auto* integer = value.getIf<std::int64_t>())
        output = static_cast<double>(*integer);
    else
        return false;
    return true;
}

editing::Result<PhysicsPlacementCollider> colliderOf(const editing::Value& value) {
    const auto*              shapeValue    = valueField(value, "shape");
    const auto*              sourceValue   = valueField(value, "source");
    const auto*              halfValue     = valueField(value, "halfExtents");
    const auto*              localValue    = valueField(value, "localPosition");
    const auto*              rotationValue = valueField(value, "localRotation");
    const auto*              verticesValue = valueField(value, "vertices");
    const auto*              maximumValue  = valueField(value, "maxVertices");
    const auto*              shape         = shapeValue ? shapeValue->getIf<std::string>() : nullptr;
    const auto*              source        = sourceValue ? sourceValue->getIf<std::string>() : nullptr;
    const auto*              half          = halfValue ? halfValue->getIf<editing::Value::Array>() : nullptr;
    const auto*              local         = localValue ? localValue->getIf<editing::Value::Array>() : nullptr;
    const auto*              rotation      = rotationValue ? rotationValue->getIf<editing::Value::Array>() : nullptr;
    const auto*              vertices      = verticesValue ? verticesValue->getIf<editing::Value::Array>() : nullptr;
    const auto*              maximum       = maximumValue ? maximumValue->getIf<std::int64_t>() : nullptr;
    PhysicsPlacementCollider collider;
    if (!shape || !source || !half || half->size() != 3 || !local || local->size() != 3 || !rotation ||
        rotation->size() != 3 || !vertices || !maximum || !valueNumber((*half)[0], collider.halfExtentX) ||
        !valueNumber((*half)[1], collider.halfExtentY) || !valueNumber((*half)[2], collider.halfExtentZ) ||
        !valueNumber((*local)[0], collider.localX) || !valueNumber((*local)[1], collider.localY) ||
        !valueNumber((*local)[2], collider.localZ) || !valueNumber((*rotation)[0], collider.localRotationX) ||
        !valueNumber((*rotation)[1], collider.localRotationY) || !valueNumber((*rotation)[2], collider.localRotationZ))
        return editing::failed<PhysicsPlacementCollider>(editing::Status::Rejected,
                                                         editing::RuleId("scene.physics-placement.cache-part"),
                                                         "Collider cache part has an invalid schema");
    if (*shape == "sphere")
        collider.shape = PhysicsPlacementShape::Sphere;
    else if (*shape == "capsule")
        collider.shape = PhysicsPlacementShape::Capsule;
    else if (*shape == "convex")
        collider.shape = PhysicsPlacementShape::ConvexHull;
    else if (*shape != "box")
        return editing::failed<PhysicsPlacementCollider>(editing::Status::Rejected,
                                                         editing::RuleId("scene.physics-placement.cache-shape"),
                                                         "Collider cache shape is unsupported");
    if (*source == "existing")
        collider.source = PhysicsPlacementColliderSource::Existing;
    else if (*source != "generated")
        return editing::failed<PhysicsPlacementCollider>(editing::Status::Rejected,
                                                         editing::RuleId("scene.physics-placement.cache-source"),
                                                         "Collider cache source is unsupported");
    collider.convexMaxVertices = static_cast<int>(*maximum);
    collider.convexVertices.reserve(vertices->size());
    for (const auto& component : *vertices) {
        double numeric = 0.0;
        if (!valueNumber(component, numeric))
            return editing::failed<PhysicsPlacementCollider>(editing::Status::Rejected,
                                                             editing::RuleId("scene.physics-placement.cache-vertex"),
                                                             "Collider cache vertices must be numeric");
        collider.convexVertices.push_back(static_cast<float>(numeric));
    }
    return editing::applied<PhysicsPlacementCollider>(std::move(collider));
}
}  // namespace

struct SceneEditorSession::Impl {
    struct CachedHull {
        std::string                           resourceKey;
        std::vector<PhysicsPlacementCollider> colliders;
    };
    explicit Impl(std::unique_ptr<scene_editing::SceneTargetBase> value)
        : target(std::move(value)), coordinator(commands) {
        if (!target || target->targetId().empty()) throw std::invalid_argument("Scene target must have an identity");
        if (!scene_editing::registerEditingCommands(coordinator).ok() || !coordinator.registerTarget(*target).ok())
            throw std::runtime_error("Could not register scene editing target");
    }
    std::unique_ptr<scene_editing::SceneTargetBase> target;
    editor::EditorCommandService                    commands;
    editor::EditorTargetCoordinator                 coordinator;
    editor::HostProfile                             profile = editor::HostProfile::developer();
    std::unique_ptr<ScenePhysicsPlacementBackend>   placement;
    std::unordered_map<std::string, CachedHull>     placementHulls;
    SceneGraph                                      placementGraph;
    std::unordered_map<std::string, glm::dmat4>     placementInitialWorld;
    std::uint64_t                                   placementTick = 0;
};
SceneEditorSession::SceneEditorSession(std::string id)
    : SceneEditorSession(std::make_unique<scene_editing::SceneDocumentTarget>(std::move(id))) {}
SceneEditorSession::SceneEditorSession(std::unique_ptr<scene_editing::SceneTargetBase> target)
    : impl_(std::make_unique<Impl>(std::move(target))) {}
SceneEditorSession::~SceneEditorSession() = default;

editing::Result<editing::TransactionReceipt> SceneEditorSession::execute(std::string command, editing::Value payload) {
    editing::CommandRequest request;
    request.id                     = editing::CommandId(std::move(command));
    request.payload                = std::move(payload);
    request.context.target         = impl_->target->targetId();
    request.context.targetRevision = impl_->target->revision();
    const auto& profile            = impl_->profile;
    auto        plan               = impl_->commands.plan(request, profile);
    if (!plan.ok()) return editing::Result<editing::TransactionReceipt>::failure(plan.status());
    // Planning resolves the target generation; execution immediately validates it again.
    request.context.targetGeneration = plan.value().targetGeneration;
    return impl_->commands.executePlan(request, plan.value(), profile);
}
editing::Result<void> SceneEditorSession::restrictCommands(const std::vector<std::string>& commands) {
    auto profile = editor::HostProfile::runtimeBuilder();
    for (const auto& command : commands) {
        if (command.empty())
            return editing::failed<void>(editing::Status::Rejected, editing::RuleId("scene.session.empty-command"),
                                         "Allowed command id must not be empty");
        profile.allowCommand(editing::CommandId(command));
    }
    impl_->profile = std::move(profile);
    return editing::applied<void>();
}
editing::Result<editing::TransactionReceipt> SceneEditorSession::undo() {
    return impl_->coordinator.undo(impl_->target->targetId());
}
editing::Result<editing::TransactionReceipt> SceneEditorSession::redo() {
    return impl_->coordinator.redo(impl_->target->targetId());
}
editing::Value    SceneEditorSession::snapshot() const { return impl_->target->snapshotValue(); }
std::string       SceneEditorSession::saveJson() const { return editing::editorValueToJson(snapshot()); }
editing::Revision SceneEditorSession::revision() const { return impl_->target->revision(); }

editing::Result<void> SceneEditorSession::cachePhysicsPlacementHull(const editing::ObjectId& object,
                                                                    std::vector<float> vertices, int maxVertices) {
    if (object.empty() || vertices.size() < 12 || vertices.size() % 3 != 0 || vertices.size() / 3 > 100000 ||
        maxVertices < 4 || maxVertices > 254 ||
        !std::all_of(vertices.begin(), vertices.end(), [](float value) { return std::isfinite(value); }) ||
        !spansVolume(vertices))
        return editing::failed<void>(
            editing::Status::Rejected, editing::RuleId("scene.physics-placement.hull-cache-input"),
            "Cached hulls require an object id, finite non-coplanar XYZ points and budget [4, 254]");
    PhysicsPlacementCollider collider;
    collider.shape             = PhysicsPlacementShape::ConvexHull;
    collider.convexVertices    = std::move(vertices);
    collider.convexMaxVertices = maxVertices;
    return cachePhysicsPlacementCompound(object, {}, {std::move(collider)});
}

editing::Result<void> SceneEditorSession::cachePhysicsPlacementCompound(
    const editing::ObjectId& object, std::string resourceKey, std::vector<PhysicsPlacementCollider> colliders) {
    if (object.empty() || colliders.empty() || colliders.size() > 64)
        return editing::failed<void>(editing::Status::Rejected,
                                     editing::RuleId("scene.physics-placement.compound-cache-input"),
                                     "Compound collider cache requires an object and between 1 and 64 parts");
    for (auto& collider : colliders) {
        collider.source = PhysicsPlacementColliderSource::Generated;
        if (!std::isfinite(collider.halfExtentX) || !std::isfinite(collider.halfExtentY) ||
            !std::isfinite(collider.halfExtentZ) || collider.halfExtentX <= 0.0 || collider.halfExtentY <= 0.0 ||
            collider.halfExtentZ <= 0.0 || !std::isfinite(collider.localX) || !std::isfinite(collider.localY) ||
            !std::isfinite(collider.localZ) || !std::isfinite(collider.localRotationX) ||
            !std::isfinite(collider.localRotationY) || !std::isfinite(collider.localRotationZ) ||
            collider.shape == PhysicsPlacementShape::Auto ||
            (collider.shape == PhysicsPlacementShape::ConvexHull &&
             (collider.convexVertices.size() < 12 || collider.convexVertices.size() % 3 != 0 ||
              collider.convexMaxVertices < 4 || collider.convexMaxVertices > 254 ||
              !std::all_of(collider.convexVertices.begin(), collider.convexVertices.end(),
                           [](float value) { return std::isfinite(value); }) ||
              !spansVolume(collider.convexVertices))))
            return editing::failed<void>(editing::Status::Rejected,
                                         editing::RuleId("scene.physics-placement.compound-cache-part"),
                                         "Compound collider cache contains malformed local geometry");
    }
    impl_->placementHulls.insert_or_assign(object.value(),
                                           Impl::CachedHull{std::move(resourceKey), std::move(colliders)});
    return editing::applied<void>();
}

editing::Result<void> SceneEditorSession::removePhysicsPlacementHull(const editing::ObjectId& object) {
    if (object.empty())
        return editing::failed<void>(editing::Status::Rejected,
                                     editing::RuleId("scene.physics-placement.hull-cache-object"),
                                     "Cached hull removal requires an object id");
    if (impl_->placementHulls.erase(object.value()) == 0)
        return editing::failed<void>(editing::Status::NoOp,
                                     editing::RuleId("scene.physics-placement.hull-cache-missing"),
                                     "The object has no cached automatic placement hull");
    return editing::applied<void>();
}

std::string SceneEditorSession::savePhysicsPlacementColliderCacheJson() const {
    editing::Value::Array    entries;
    std::vector<std::string> objects;
    objects.reserve(impl_->placementHulls.size());
    for (const auto& [object, cached] : impl_->placementHulls) {
        (void)cached;
        objects.push_back(object);
    }
    std::sort(objects.begin(), objects.end());
    for (const auto& object : objects) {
        const auto&           cached = impl_->placementHulls.at(object);
        editing::Value::Array colliders;
        colliders.reserve(cached.colliders.size());
        for (const auto& collider : cached.colliders) colliders.emplace_back(colliderValue(collider));
        entries.emplace_back(editing::Value::Object{
            {"object", object}, {"resourceKey", cached.resourceKey}, {"colliders", std::move(colliders)}});
    }
    return editing::editorValueToJson(editing::Value::Object{
        {"schema", "eve.scene.physics-placement-collider-cache"},
        {"version", std::int64_t{2}},
        {"entries", std::move(entries)},
    });
}

editing::Result<void> SceneEditorSession::restorePhysicsPlacementColliderCacheJson(const std::string& json) {
    auto decoded = editing::editorValueFromJson(json);
    if (!decoded.ok()) return editing::Result<void>::failure(decoded.status());
    const auto* schemaValue  = valueField(decoded.value(), "schema");
    const auto* versionValue = valueField(decoded.value(), "version");
    const auto* entriesValue = valueField(decoded.value(), "entries");
    const auto* schema       = schemaValue ? schemaValue->getIf<std::string>() : nullptr;
    const auto* version      = versionValue ? versionValue->getIf<std::int64_t>() : nullptr;
    const auto* entries      = entriesValue ? entriesValue->getIf<editing::Value::Array>() : nullptr;
    if (!schema || *schema != "eve.scene.physics-placement-collider-cache" || !version || *version != 2 || !entries)
        return editing::failed<void>(editing::Status::Rejected, editing::RuleId("scene.physics-placement.cache-schema"),
                                     "Collider cache requires schema eve.scene.physics-placement-collider-cache v2");
    std::unordered_map<std::string, Impl::CachedHull> replacement;
    for (const auto& entry : *entries) {
        const auto* objectValue    = valueField(entry, "object");
        const auto* keyValue       = valueField(entry, "resourceKey");
        const auto* collidersValue = valueField(entry, "colliders");
        const auto* object         = objectValue ? objectValue->getIf<std::string>() : nullptr;
        const auto* key            = keyValue ? keyValue->getIf<std::string>() : nullptr;
        const auto* colliders      = collidersValue ? collidersValue->getIf<editing::Value::Array>() : nullptr;
        if (!object || object->empty() || !key || !colliders || colliders->empty() || colliders->size() > 64 ||
            replacement.contains(*object))
            return editing::failed<void>(editing::Status::Rejected,
                                         editing::RuleId("scene.physics-placement.cache-entry"),
                                         "Collider cache entry is malformed or duplicated");
        std::vector<PhysicsPlacementCollider> parsed;
        parsed.reserve(colliders->size());
        for (const auto& value : *colliders) {
            auto collider = colliderOf(value);
            if (!collider.ok()) return editing::Result<void>::failure(collider.status());
            parsed.push_back(std::move(collider.value()));
        }
        replacement.emplace(*object, Impl::CachedHull{*key, std::move(parsed)});
    }
    impl_->placementHulls = std::move(replacement);
    return editing::applied<void>();
}

editing::Result<void> SceneEditorSession::beginPhysicsPlacement(PhysicsPlacementRequest request) {
    if (impl_->placement)
        return editing::failed<void>(editing::Status::Conflict, editing::RuleId("scene.physics-placement.active"),
                                     "A physics placement preview is already active");
    auto graph = sceneGraphOf(impl_->target->snapshotValue());
    if (!graph.ok()) return editing::Result<void>::failure(graph.status());
    std::unordered_map<std::string, glm::dmat4> worldMatrices;
    std::unordered_set<std::string>             visiting;
    for (auto& object : request.objects) {
        if (object.shape == PhysicsPlacementShape::Auto) {
            const auto found = impl_->placementHulls.find(object.object.value());
            if (found == impl_->placementHulls.end())
                return editing::failed<void>(editing::Status::NotFound,
                                             editing::RuleId("scene.physics-placement.auto-shape-missing"),
                                             "Automatic placement hull is not cached for the object");
            if (!object.colliderResourceKey.empty() && object.colliderResourceKey != found->second.resourceKey)
                return editing::failed<void>(editing::Status::Conflict,
                                             editing::RuleId("scene.physics-placement.auto-shape-stale"),
                                             "Automatic placement collider cache is stale for the resource revision");
            object.shape     = PhysicsPlacementShape::Box;
            object.colliders = found->second.colliders;
        }
        auto world = worldMatrixOf(object.object.value(), graph.value(), worldMatrices, visiting);
        if (!world.ok()) return editing::Result<void>::failure(world.status());
        auto transform = transformOf(world.value());
        if (!transform.ok()) return editing::Result<void>::failure(transform.status());
        object.transform = transform.value();
    }
    request.sourceRevision = impl_->target->revision();
    auto created           = ScenePhysicsPlacementBackend::create(std::move(request));
    if (!created.ok()) return editing::Result<void>::failure(created.status());
    impl_->placement             = std::move(created.value());
    impl_->placementGraph        = std::move(graph.value());
    impl_->placementInitialWorld = std::move(worldMatrices);
    impl_->placementTick         = 0;
    return editing::applied<void>();
}

editing::Result<PhysicsPlacementFrame> SceneEditorSession::updatePhysicsPlacement(double x, double y, double z,
                                                                                  double fixedDelta) {
    if (!impl_->placement)
        return editing::failed<PhysicsPlacementFrame>(editing::Status::Rejected,
                                                      editing::RuleId("scene.physics-placement.inactive"),
                                                      "No physics placement preview is active");
    auto moved = impl_->placement->setHandlePosition(x, y, z);
    if (!moved.ok()) return editing::Result<PhysicsPlacementFrame>::failure(moved.status());
    const auto nextTick = impl_->placementTick + 1;
    auto       stepped  = impl_->placement->step(nextTick, fixedDelta);
    if (!stepped.ok()) return editing::Result<PhysicsPlacementFrame>::failure(stepped.status());
    impl_->placementTick = nextTick;
    return impl_->placement->placementFrame();
}

editing::Result<PhysicsPlacementFrame> SceneEditorSession::updatePhysicsPlacementPose(
    double x, double y, double z, double rotationX, double rotationY, double rotationZ, double fixedDelta) {
    if (!impl_->placement)
        return editing::failed<PhysicsPlacementFrame>(editing::Status::Rejected,
                                                      editing::RuleId("scene.physics-placement.inactive"),
                                                      "No physics placement preview is active");
    auto moved = impl_->placement->setHandlePosition(x, y, z);
    if (!moved.ok()) return editing::Result<PhysicsPlacementFrame>::failure(moved.status());
    auto rotated = impl_->placement->setHandleRotation(rotationX, rotationY, rotationZ);
    if (!rotated.ok()) return editing::Result<PhysicsPlacementFrame>::failure(rotated.status());
    const auto nextTick = impl_->placementTick + 1;
    auto       stepped  = impl_->placement->step(nextTick, fixedDelta);
    if (!stepped.ok()) return editing::Result<PhysicsPlacementFrame>::failure(stepped.status());
    impl_->placementTick = nextTick;
    return impl_->placement->placementFrame();
}

editing::Result<PhysicsPlacementFrame> SceneEditorSession::updatePhysicsPlacementTransform(
    double x, double y, double z, double rotationX, double rotationY, double rotationZ, double scaleX, double scaleY,
    double scaleZ, double fixedDelta) {
    if (!impl_->placement)
        return editing::failed<PhysicsPlacementFrame>(editing::Status::Rejected,
                                                      editing::RuleId("scene.physics-placement.inactive"),
                                                      "No physics placement preview is active");
    auto moved = impl_->placement->setHandlePosition(x, y, z);
    if (!moved.ok()) return editing::Result<PhysicsPlacementFrame>::failure(moved.status());
    auto rotated = impl_->placement->setHandleRotation(rotationX, rotationY, rotationZ);
    if (!rotated.ok()) return editing::Result<PhysicsPlacementFrame>::failure(rotated.status());
    auto scaled = impl_->placement->setHandleScale(scaleX, scaleY, scaleZ);
    if (!scaled.ok()) return editing::Result<PhysicsPlacementFrame>::failure(scaled.status());
    const auto nextTick = impl_->placementTick + 1;
    auto       stepped  = impl_->placement->step(nextTick, fixedDelta);
    if (!stepped.ok()) return editing::Result<PhysicsPlacementFrame>::failure(stepped.status());
    impl_->placementTick = nextTick;
    return impl_->placement->placementFrame();
}

editing::Result<PhysicsPlacementFrame> SceneEditorSession::alignPhysicsPlacementToSurface(
    double fromX, double fromY, double fromZ, double toX, double toY, double toZ, double offset, double fixedDelta) {
    if (!impl_->placement)
        return editing::failed<PhysicsPlacementFrame>(editing::Status::Rejected,
                                                      editing::RuleId("scene.physics-placement.inactive"),
                                                      "No physics placement preview is active");
    auto aligned = impl_->placement->alignHandleToSurface(fromX, fromY, fromZ, toX, toY, toZ, offset);
    if (!aligned.ok()) return editing::Result<PhysicsPlacementFrame>::failure(aligned.status());
    const auto nextTick = impl_->placementTick + 1;
    auto       stepped  = impl_->placement->step(nextTick, fixedDelta);
    if (!stepped.ok()) return editing::Result<PhysicsPlacementFrame>::failure(stepped.status());
    impl_->placementTick = nextTick;
    return impl_->placement->placementFrame();
}

editing::Result<editing::TransactionReceipt> SceneEditorSession::commitPhysicsPlacement() {
    if (!impl_->placement)
        return editing::failed<editing::TransactionReceipt>(editing::Status::Rejected,
                                                            editing::RuleId("scene.physics-placement.inactive"),
                                                            "No physics placement preview is active");
    if (impl_->target->revision() != impl_->placement->sourceRevision())
        return editing::failed<editing::TransactionReceipt>(editing::Status::Conflict,
                                                            editing::RuleId("scene.physics-placement.stale"),
                                                            "Scene changed while physics placement was active");
    auto captured = impl_->placement->placementFrame();
    if (!captured.ok()) return editing::Result<editing::TransactionReceipt>::failure(captured.status());
    std::unordered_map<std::string, glm::dmat4> desiredWorld;
    for (const auto& object : captured.value().objects)
        if (object.selected) desiredWorld.insert_or_assign(object.object.value(), matrixOf(object.transform));
    editing::Value::Array transforms;
    for (const auto& object : captured.value().objects) {
        if (!object.selected) continue;
        const auto graphNode = impl_->placementGraph.find(object.object.value());
        if (graphNode == impl_->placementGraph.end())
            return editing::failed<editing::TransactionReceipt>(editing::Status::Conflict,
                                                                editing::RuleId("scene.physics-placement.graph-stale"),
                                                                "Placement hierarchy state is no longer available");
        glm::dmat4 localMatrix = desiredWorld.at(object.object.value());
        if (!graphNode->second.parent.empty()) {
            const auto desiredParent = desiredWorld.find(graphNode->second.parent);
            if (desiredParent != desiredWorld.end())
                localMatrix = glm::inverse(desiredParent->second) * localMatrix;
            else {
                const auto initialParent = impl_->placementInitialWorld.find(graphNode->second.parent);
                if (initialParent == impl_->placementInitialWorld.end())
                    return editing::failed<editing::TransactionReceipt>(
                        editing::Status::Conflict, editing::RuleId("scene.physics-placement.parent-stale"),
                        "Placement parent world transform is no longer available");
                localMatrix = glm::inverse(initialParent->second) * localMatrix;
            }
        }
        auto local = transformOf(localMatrix);
        if (!local.ok()) return editing::Result<editing::TransactionReceipt>::failure(local.status());
        const auto& value = local.value();
        transforms.emplace_back(editing::Value::Object{
            {"object", object.object.value()},
            {"position", editing::Value::Array{value.x, value.y, value.z}},
            {"rotation", editing::Value::Array{value.rotationX, value.rotationY, value.rotationZ}},
            {"scale", editing::Value::Array{value.scaleX, value.scaleY, value.scaleZ}},
        });
    }
    auto committed =
        execute("scene.transform.batch.set.v1", editing::Value::Object{{"transforms", std::move(transforms)}});
    if (committed.ok()) {
        impl_->placement.reset();
        impl_->placementGraph.clear();
        impl_->placementInitialWorld.clear();
    }
    return committed;
}

editing::Result<void> SceneEditorSession::cancelPhysicsPlacement() {
    if (!impl_->placement)
        return editing::failed<void>(editing::Status::NoOp, editing::RuleId("scene.physics-placement.inactive"),
                                     "No physics placement preview is active");
    impl_->placement.reset();
    impl_->placementGraph.clear();
    impl_->placementInitialWorld.clear();
    impl_->placementTick = 0;
    return editing::applied<void>();
}

bool SceneEditorSession::physicsPlacementActive() const noexcept { return impl_->placement != nullptr; }
editing::Result<editing::TransactionReceipt> SceneEditorSession::restoreJson(const std::string& json) {
    auto value = editing::editorValueFromJson(json);
    if (!value.ok()) return editing::Result<editing::TransactionReceipt>::failure(value.status());
    return execute("scene.snapshot.restore.v1", std::move(value.value()));
}
}  // namespace eve::scene_editor
