#pragma once

#include "editing/EditableTarget.h"
#include "editing/EditingProperty.h"
#include "editing/EditingTargetOperations.h"
#include "ui/Theme.h"

#include <string>
#include <vector>

namespace eve::ui_editing {

using editing::CapabilityId;
using editing::DiagnosticSeverity;
using editing::DomainOperation;
using editing::EditRegion;
using editing::IDomainOperationTarget;
using editing::IDomainOperationTargetStaging;
using editing::IEditableTarget;
using editing::IEditingSnapshotProvider;
using editing::IPropertyProvider;
using editing::ObjectId;
using editing::PropertyDescriptor;
using editing::PropertyFlag;
using editing::PropertyPath;
using editing::PropertyReadResult;
using editing::PropertyReadState;
using editing::PropertySchema;
using editing::PropertySetMode;
using editing::PropertyType;
using editing::Revision;
using editing::RuleId;
using editing::SelectionSnapshot;
using editing::TargetDescriptor;
using editing::TargetId;
template <class T>
using EditorResult     = editing::Result<T>;
using EditorStatus     = editing::Status;
using EditorValue      = editing::Value;
using EditorDiagnostic = editing::Diagnostic;

/** @brief Base preset used when resetting a named theme asset. */
enum class UiThemeBasePreset { Dark, Light, Custom };

/** @brief One named, fully snapshotted Theme asset. */
struct UiThemeAsset {
    ObjectId           id;
    std::string        name;
    UiThemeBasePreset  basePreset = UiThemeBasePreset::Dark;
    ui::Theme          tokens;
};

/** @brief Serializable catalog of named UI themes with one active publication slot. */
class UiThemeCatalogTarget final : public virtual IEditableTarget,
                                   public IDomainOperationTarget,
                                   public IDomainOperationTargetStaging,
                                   public IPropertyProvider,
                                   public IEditingSnapshotProvider {
public:
    /**
     * @brief Construct a catalog seeded with built-in dark and light assets.
     * @param id Stable editor target identity.
     */
    explicit UiThemeCatalogTarget(std::string id);

    /** @brief Inspector capability published by describe(). */
    static CapabilityId propertyCapabilityId() { return CapabilityId("eve.editor.target.ui-theme-properties"); }

    TargetId      targetId() const override { return TargetId(id_); }
    std::uint64_t revision() const override { return revision_; }
    EditRegion    dirtyRegion() const override { return dirty_; }
    void          clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    /**
     * @brief Query Inspector and snapshot capabilities.
     * @return Borrowed pointer owned by this catalog, or null.
     * @lifetime Valid until this target is destroyed or replaced by commitDomainState.
     * @thread Owner-thread only.
     */
    void* queryCapability(const CapabilityId& capability) override;

    EditorResult<void>                      applyDomainOperation(const DomainOperation& operation) override;
    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    EditorResult<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override;

    /** @brief Borrow authored themes in catalog order. */
    const std::vector<UiThemeAsset>& themes() const noexcept { return themes_; }
    /** @brief Active publication id; empty only if the catalog failed to seed. */
    const ObjectId& activeId() const noexcept { return activeId_; }

    /** @brief Copy one named theme, or NotFound. */
    EditorResult<UiThemeAsset> theme(const ObjectId& id) const;
    /** @brief Runtime name written to ui::globalThemeName(): dark, light, or custom. */
    std::string runtimeName(const ObjectId& id) const;

    [[nodiscard]] EditorResult<DomainOperation> makeCreateFromPreset(const ObjectId& id, std::string name,
                                                                     UiThemeBasePreset preset) const;
    [[nodiscard]] EditorResult<DomainOperation> makeDuplicate(const ObjectId& source, const ObjectId& id,
                                                              std::string name) const;
    [[nodiscard]] EditorResult<DomainOperation> makeRename(const ObjectId& id, std::string name) const;
    [[nodiscard]] EditorResult<DomainOperation> makeDelete(const ObjectId& id) const;
    [[nodiscard]] EditorResult<DomainOperation> makeSetActive(const ObjectId& id) const;
    [[nodiscard]] EditorResult<DomainOperation> makeResetToBase(const ObjectId& id) const;

    eve::Result<eve::Revision>    currentRevision(const SelectionSnapshot& selection) const override;
    PropertySchema                schema(const SelectionSnapshot& selection) const override;
    PropertyReadResult            read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath&      path) const override;

    EditorValue            snapshotValue() const override;
    EditorResult<void>     loadSnapshot(const EditorValue& snapshot);
    std::vector<EditorDiagnostic> validate() const;

private:
    EditorResult<DomainOperation> replacement(EditorValue content, std::string property = {}) const;
    EditorValue                   contentValue() const;
    bool                          matches(const SelectionSnapshot& selection) const;
    /**
     * @brief Locate a mutable theme asset by id.
     * @return Borrowed pointer owned by this catalog, or null.
     * @ownership this catalog
     * @lifetime Valid until the next catalog mutation or destruction.
     */
    UiThemeAsset* mutableTheme(const ObjectId& id);
    /**
     * @brief Locate a theme asset by id.
     * @return Borrowed pointer owned by this catalog, or null.
     * @ownership this catalog
     * @lifetime Valid until the next catalog mutation or destruction.
     */
    const UiThemeAsset* findTheme(const ObjectId& id) const;

    std::string                id_;
    unsigned long long         revision_ = 1;
    EditRegion                 dirty_;
    std::vector<UiThemeAsset>  themes_;
    ObjectId                   activeId_;
};

/** @brief Revision-bound Theme copy used by gallery hosts without mutating globalTheme. */
struct UiThemePreviewSnapshot {
    EditorStatus                  status = EditorStatus::Failed;
    Revision                      documentRevision = 0;
    ObjectId                      themeId;
    ui::Theme                     theme;
    std::string                   runtimeName;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Builds a Theme copy for a catalog revision without touching runtime globals. */
class UiThemePreviewService {
public:
    /**
     * @brief Copy tokens for @p themeId when @p expectedRevision matches the catalog.
     * @param expectedRevision Caller-held generation; mismatch yields Conflict and keeps diagnostics.
     */
    UiThemePreviewSnapshot build(const UiThemeCatalogTarget& catalog, const ObjectId& themeId,
                                 Revision expectedRevision) const;
};

/** @brief Publishes the active catalog theme through ui::setGlobalTheme. */
class UiThemeRuntimePublisher {
public:
    /**
     * @brief Replace the process global theme with the catalog's active asset.
     * @remarks Does not mutate the catalog. Failed validation leaves globalTheme unchanged.
     */
    [[nodiscard]] EditorResult<void> publish(const UiThemeCatalogTarget& catalog) const;
};

}  // namespace eve::ui_editing
