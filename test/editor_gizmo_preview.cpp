#include "editor/EditorGizmoPreview.h"

#include "editor/EditorAudioTarget.h"
#include "editor/EditorLightingTarget.h"
#include "editor/EditorPhysicsTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

namespace {
SelectionSnapshot selection(const char* target) {
    SelectionSnapshot result;
    result.items.push_back({SelectionDomain::Custom, TargetId(target), StableId("self"), "object"});
    return result;
}

void set(IPropertyProvider& provider, IDomainOperationTarget& target,
         const SelectionSnapshot& selected, const char* path, EditorValue value) {
    auto operation = provider.makeSet(selected, PropertyPath(path), value, PropertySetMode::Absolute);
    REQUIRE(operation.value); CHECK(target.applyDomainOperation(*operation.value).isAccepted());
}
}

TEST_CASE("editor.gizmo.builds_physics_audio_and_light_overlays") {
    EditorGizmoPreviewBuilder builder;
    PhysicsColliderTarget collider("shape", 3);
    set(collider, collider, selection("shape"), "shape.kind", "capsule");
    set(collider, collider, selection("shape"), "shape.radius", 0.75);
    set(collider, collider, selection("shape"), "shape.capsule-height", 2.0);
    auto colliderGizmo = builder.collider(collider);
    CHECK_EQ(static_cast<int>(colliderGizmo.status), static_cast<int>(EditorStatus::Applied));
    REQUIRE_EQ(colliderGizmo.primitives.size(), 1U); CHECK_EQ(colliderGizmo.primitives[0].kind, "wire-capsule");

    PhysicsJointTarget joint("hinge");
    set(joint, joint, selection("hinge"), "body.a", "body-a");
    set(joint, joint, selection("hinge"), "body.b", "body-b");
    auto jointGizmo = builder.joint(joint, [](const std::string& body) {
        return EditorResult<std::array<double, 3>>::applied(
            body == "body-a" ? std::array<double, 3>{0.0, 0.0, 0.0}
                             : std::array<double, 3>{2.0, 0.0, 0.0});
    });
    CHECK_EQ(static_cast<int>(jointGizmo.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(jointGizmo.primitives.size(), 4U); CHECK_EQ(jointGizmo.primitives[2].length, 2.0);

    AudioSourceTarget audio("speaker");
    set(audio, audio, selection("speaker"), "spatial.reference-distance", 2.0);
    set(audio, audio, selection("speaker"), "spatial.maximum-distance", 12.0);
    auto audioGizmo = builder.audioSource(audio);
    CHECK_EQ(static_cast<int>(audioGizmo.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(audioGizmo.primitives.size(), 3U); CHECK_EQ(audioGizmo.primitives[1].radius, 12.0);

    Light3DDocumentTarget light("sun");
    set(light, light, selection("sun"), "light.type", "dir");
    auto lightGizmo = builder.light(light);
    CHECK_EQ(static_cast<int>(lightGizmo.status), static_cast<int>(EditorStatus::Applied));
    REQUIRE_EQ(lightGizmo.primitives.size(), 1U); CHECK_EQ(lightGizmo.primitives[0].kind, "arrow");
}
