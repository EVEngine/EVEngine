#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editing/EditingCommandTypes.h"
#include "editing/EditingIds.h"
#include "editing/EditingProperty.h"
#include "editing/EditingProtocol.h"
#include "editing/EditingResult.h"
#include "editing/EditingSelection.h"
#include "editing/EditingTargetOperations.h"
#include "editing/EditingValue.h"
#include "editing/EditableTarget.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorCommandTypes.h"
#include "editor/EditorIds.h"
#include "editor/EditorProperty.h"
#include "editor/EditorProtocol.h"
#include "editor/EditorPhysicsAsset.h"
#include "editor/EditorPhysicsTarget.h"
#include "editor/EditorResult.h"
#include "editor/EditorSelection.h"
#include "editor/EditorTarget.h"
#include "editor/EditorValue.h"
#include "physics_editing/PhysicsColliderAsset.h"
#include "physics_editing/PhysicsTarget.h"

#include <string>
#include <type_traits>

static_assert(!std::is_convertible_v<eve::editing::CommandId, eve::editing::ToolId>);
static_assert(std::is_same_v<eve::editor::CommandId, eve::editing::CommandId>);
static_assert(std::is_same_v<eve::editor::EditorValue, eve::editing::Value>);
static_assert(std::is_same_v<eve::editor::EditorStatus, eve::editing::Status>);
static_assert(std::is_same_v<eve::editor::CommandSource, eve::editing::CommandSource>);
static_assert(std::is_same_v<eve::editor::CommandRequest, eve::editing::CommandRequest>);
static_assert(std::is_same_v<eve::editor::TransactionReceipt, eve::editing::TransactionReceipt>);
static_assert(std::is_same_v<eve::editor::SelectionSnapshot, eve::editing::SelectionSnapshot>);
static_assert(std::is_same_v<eve::editor::IEditableTarget, eve::editing::IEditableTarget>);
static_assert(std::is_same_v<eve::editor::IDomainOperationTarget, eve::editing::IDomainOperationTarget>);
static_assert(
    std::is_same_v<eve::editor::IDomainOperationTargetStaging, eve::editing::IDomainOperationTargetStaging>);
static_assert(std::is_same_v<eve::editor::PropertySchema, eve::editing::PropertySchema>);
static_assert(std::is_same_v<eve::editor::IPropertyProvider, eve::editing::IPropertyProvider>);
static_assert(std::is_same_v<eve::editor::PhysicsColliderTarget, eve::physics_editing::PhysicsColliderTarget>);
static_assert(
    std::is_same_v<eve::editor::IPhysicsColliderAssetResolver, eve::physics_editing::IPhysicsColliderAssetResolver>);

TEST_CASE("editing.contracts.ids_values_and_editor_compatibility") {
    using namespace eve::editing;

    const CommandId command("scene.object.create");
    CHECK_EQ(command.value(), std::string("scene.object.create"));
    CHECK_EQ(eve::editor::CommandId("scene.object.create"), command);

    Value::Object root;
    root["name"]     = "Station";
    root["position"] = Value::Array{1.0, 2.0, 3.0};
    const Value value(std::move(root));
    CHECK_EQ(static_cast<int>(value.type()), static_cast<int>(Value::Type::Object));
    CHECK(value.isWithinLimits(4, 16, 128));
    CHECK(!value.isWithinLimits(1, 16, 128));
}

TEST_CASE("editing.contracts.structured_result") {
    using namespace eve::editing;

    const auto accepted = Result<Value>::applied(Value("ready"));
    CHECK(accepted.isAccepted());
    REQUIRE(accepted.value.has_value());
    CHECK_EQ(*accepted.value->getIf<std::string>(), std::string("ready"));

    const auto rejected = Result<void>::error(Status::Rejected, RuleId("authoring.invalid"), "invalid input");
    CHECK(!rejected.isAccepted());
    CHECK_EQ(rejected.diagnostics.size(), size_t{1});
    CHECK_EQ(rejected.diagnostics.front().rule.value(), std::string("authoring.invalid"));
}
