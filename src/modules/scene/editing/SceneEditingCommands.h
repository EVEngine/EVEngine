#pragma once
#include "common/Export.h"


#include "editing/EditingCommandRegistry.h"

namespace eve::scene_editing {

/** @brief Serializable transform exchanged across the scene editing boundary. */
struct SceneTransformValue {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double rotationX = 0.0;
    double rotationY = 0.0;
    double rotationZ = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double scaleZ = 1.0;

    /** @brief Operator <=>. */
    auto operator<=>(const SceneTransformValue&) const = default;
};

/** @brief Capability implemented by document and live scene targets. */
class ITransformEditTarget {
public:
    /** @brief Releases ITransformEditTarget resources. */
    virtual ~ITransformEditTarget() = default;
    /** @brief Editing capability id. */
    static editing::CapabilityId editingCapabilityId() {
        /** @brief Capability id. */
        return editing::CapabilityId("eve.editor.target.transform");
    }
    /** @brief Compatibility spelling retained for existing editor tools. */
    static editing::CapabilityId editorCapabilityId() { return editingCapabilityId(); }
    /** @brief Reads transform. */
    [[nodiscard]] virtual editing::Result<SceneTransformValue> readTransform(
        const editing::ObjectId& id) const = 0;
    /** @brief Make set transform. */
    [[nodiscard]] virtual editing::Result<editing::DomainOperation> makeSetTransform(
        const editing::ObjectId& id, const SceneTransformValue& transform) const = 0;
};

/** @brief Register Scene-owned commands with a generic editing host. */
[[nodiscard]] EVENGINE_API_BACKENDS editing::Result<void> registerEditingCommands(
    editing::IEditingCommandRegistry& registry);

}  // namespace eve::scene_editing
