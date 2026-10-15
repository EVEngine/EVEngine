#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "editor/Editor.h"
#include "physics/editing/PhysicsEditingProvider.h"
#include "physics/editor/PhysicsEditorModule.h"

#include <string>

TEST_CASE("physics.editor.bootPublishesEditingFactory") {
    auto* physicsEditor = requireModInst(eve::physics_editor, PhysicsEditorModule);
    REQUIRE(physicsEditor != nullptr);

    auto* editor = getModInst(eve::editor, Editor);
    REQUIRE(editor != nullptr);
    auto lease = editor->extensionProviders().acquire("physics.editing");
    REQUIRE(lease.ok());
    auto* factory = static_cast<eve::physics_editing::IPhysicsEditingFactory*>(
        lease.value().query(eve::physics_editing::IPhysicsEditingFactory::capabilityId()));
    REQUIRE(factory != nullptr);

    auto target = factory->createCollider("boot.collider", 3, nullptr);
    REQUIRE(target != nullptr);
    CHECK_EQ(target->targetId().value(), std::string("boot.collider"));
}
