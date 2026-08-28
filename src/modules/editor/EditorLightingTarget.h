#pragma once

#include "editor/EditorAuthority.h"
#include "editor/EditorProperty.h"
#include "editor/EditorTargetV2.h"

#include <map>
#include <string>
#include <vector>

namespace eve::graphics {
class Light3D;
}
namespace eve::daynight {
class DayNight;
}
namespace eve::weather {
class Weather;
}

namespace eve::editor {

/** @brief Shared property-document implementation for light and environment targets. */
class LightingPropertyTargetBase : public IEditableTargetV2,
                                   public IDomainOperationTarget,
                                   public IPropertyProvider {
public:
    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime Valid until this target is destroyed or mutated. */
    void* queryCapability(const CapabilityId& capability) override;
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    eve::Result<eve::Revision> currentRevision(const SelectionSnapshot& selection) const override;
    PropertySchema schema(const SelectionSnapshot& selection) const override;
    PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath& path) const override;
    /** @brief Capture deterministic values for document persistence. */
    EditorValue snapshotValue() const;
    /** @brief Atomically load a schema-version-one property snapshot. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);
    /** @brief Read a typed runtime-neutral property value by path. @return Borrowed pointer into this target, or null. @lifetime Valid until the target is mutated or destroyed. */
    const EditorValue* value(const std::string& path) const;

protected:
    /** @brief Construct a concrete property target from its schema and defaults. */
    LightingPropertyTargetBase(std::string id, std::string targetType, PropertySchema schema);

private:
    bool selectionMatches(const SelectionSnapshot& selection) const;
    std::string id_;
    std::string targetType_;
    PropertySchema schema_;
    std::map<std::string, EditorValue> values_;
    unsigned long long revision_ = 1;
    EditRegion dirty_;
};

/** @brief Serializable property target matching graphics::Light3D. */
class Light3DDocumentTarget final : public LightingPropertyTargetBase {
public:
    explicit Light3DDocumentTarget(std::string id);
    /** @brief Validate direction, shadow and point/directional cross-field rules. */
    std::vector<EditorDiagnostic> validate() const;
};

/** @brief Serializable shared environment target for static, DayNight and Weather modes. */
class EnvironmentDocumentTarget final : public LightingPropertyTargetBase {
public:
    explicit EnvironmentDocumentTarget(std::string id);
    /** @brief Validate mode-dependent atmosphere and weather settings. */
    std::vector<EditorDiagnostic> validate() const;
};

/** @brief Optional bridge applying a light document to graphics::Light3D. */
class Light3DRuntimeApplier {
public:
    EditorResult<void> apply(const Light3DDocumentTarget& document, graphics::Light3D* light) const;
};

/** @brief Optional bridge applying environment properties to DayNight or Weather. */
class EnvironmentRuntimeApplier {
public:
    EditorResult<void> applyDayNight(const EnvironmentDocumentTarget& document,
                                     daynight::DayNight* environment) const;
    EditorResult<void> applyWeather(const EnvironmentDocumentTarget& document,
                                    weather::Weather* environment) const;
};

}  // namespace eve::editor
