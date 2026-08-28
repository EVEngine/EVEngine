#pragma once

#include "editor/EditorAuthority.h"
#include "editor/EditorProperty.h"
#include "editor/EditorTargetV2.h"
#include "editor/EditorOffscreenPreview.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eve::graphics { class Graphics; class Texture; class Canvas; }
namespace eve::stylize { class StyleInstance; class StyleRecipe; }

namespace eve::editor {

/** @brief Stable pass in an authored stylization recipe. */
struct StylizePassValue {
    ObjectId id;
    std::string style;
    bool enabled = true;
    int priority = 0;
    std::map<std::string,double> overrides;
};

/** @brief Serializable ordered style recipe with dynamic parameter Inspector. */
class StylizeRecipeTarget final : public IEditableTargetV2,
                                  public IDomainOperationTarget,
                                  public IDomainOperationTargetStaging,
                                  public IPropertyProvider {
public:
    explicit StylizeRecipeTarget(std::string id);
    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    void* queryCapability(const CapabilityId& capability) override;
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    EditorResult<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override;
    eve::Result<eve::Revision> currentRevision(const SelectionSnapshot& selection) const override;
    PropertySchema schema(const SelectionSnapshot& selection) const override;
    PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath& path) const override;
    /** @brief Plan insertion of a known style under a stable pass ID. */
    EditorResult<DomainOperation> makeCreate(const ObjectId& id, const std::string& style) const;
    /** @brief Plan removal of one pass. */
    EditorResult<DomainOperation> makeDelete(const ObjectId& id) const;
    /** @brief Plan stable reordering by destination index. */
    EditorResult<DomainOperation> makeMove(const ObjectId& id, std::size_t index) const;
    /** @brief Return ordered immutable pass values. */
    std::vector<StylizePassValue> passes() const;
    /** @brief Validate style existence, stage compatibility, parameters and budgets. */
    std::vector<EditorDiagnostic> validate() const;
    /** @brief Capture schema-version-one ordered recipe. */
    EditorValue snapshotValue() const;
    /** @brief Atomically load and validate a persisted recipe. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);
private:
    bool matches(const SelectionSnapshot& selection) const;
    EditorValue contentValue() const;
    EditorResult<DomainOperation> replacement(EditorValue payload, std::string property={}) const;
    std::string id_; Revision revision_=1; EditRegion dirty_;
    std::map<ObjectId,StylizePassValue> passes_; std::vector<ObjectId> order_;
};

/** @brief Compiled, owned StyleRecipe generation used for live and offscreen preview. */
class StylizeRecipeRuntime {
public:
    StylizeRecipeRuntime();
    ~StylizeRecipeRuntime();
    /** @brief Compile a complete candidate before replacing the active generation. */
    EditorResult<void> publish(const StylizeRecipeTarget& document, graphics::Graphics* graphics);
    /** @brief Apply the active post recipe into an explicit destination Canvas. */
    EditorResult<void> apply(graphics::Graphics* graphics, graphics::Texture* source,
                             graphics::Canvas* destination, Revision expectedRevision) const;
    /** @brief Revision of the compiled generation, or zero before publication. */
    Revision revision() const { return revision_; }
private:
    std::vector<std::unique_ptr<stylize::StyleInstance>> instances_;
    std::unique_ptr<stylize::StyleRecipe> recipe_;
    graphics::Graphics* graphics_=nullptr; Revision revision_=0;
};

/** @brief Compiles and applies an isolated recipe into shared offscreen Canvas readback. */
class StylizeOffscreenPreviewService {
public:
    StylizeOffscreenPreviewService(GraphicsOffscreenPreviewService* previews,
                                   graphics::Graphics* graphics)
        : previews_(previews), graphics_(graphics) {}
    /** @brief Render one current document generation without mutating a live recipe. */
    EditorResult<OffscreenPreviewArtifact> render(const StylizeRecipeTarget& document,
                                                   graphics::Texture* source,
                                                   const StableId& previewId,
                                                   int width, int height) const;
private:
    GraphicsOffscreenPreviewService* previews_=nullptr; graphics::Graphics* graphics_=nullptr;
};

} // namespace eve::editor
