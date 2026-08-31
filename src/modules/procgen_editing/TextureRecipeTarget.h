#pragma once
#include "editing/EditingAuthority.h"
#include "editing/EditingProperty.h"
#include "editing/EditableTarget.h"
#include <cstdint>
#include <memory>
#include <string>
namespace eve::image { class ImageData; }
namespace eve::procgen_editing {
using namespace eve::editing;
using EditorValue = eve::editing::Value;
using EditorStatus = eve::editing::Status;
using EditorDiagnostic = eve::editing::Diagnostic;
template<class T> using EditorResult = eve::editing::Result<T>;
/** @brief Revisioned, schema-driven procedural texture recipe asset. */
class TextureRecipeTarget final : public virtual IEditableTarget, public IDomainOperationTarget,
                                  public IDomainOperationTargetStaging, public IPropertyProvider {
public:
    /** @brief Construct a registered procedural texture recipe target. @throws std::invalid_argument When recipe is not registered. */
    TextureRecipeTarget(std::string id, std::string recipe);
    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime Valid until this target is destroyed or mutated. */
    void* queryCapability(const CapabilityId&) override;
    EditorResult<void> applyDomainOperation(const DomainOperation&) override;
    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    EditorResult<void> commitDomainState(std::unique_ptr<IDomainOperationTarget>) override;
    eve::Result<eve::Revision> currentRevision(const SelectionSnapshot&) const override;
    PropertySchema schema(const SelectionSnapshot&) const override;
    PropertyReadResult read(const SelectionSnapshot&, const PropertyPath&) const override;
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot&, const PropertyPath&,
                                          const EditorValue&, PropertySetMode) const override;
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot&, const PropertyPath&) const override;
    const std::string& recipe() const { return recipe_; }
    const EditorValue::Object& values() const { return values_; }
    std::vector<EditorDiagnostic> validate() const;
    EditorValue snapshotValue() const;
    EditorResult<void> loadSnapshot(const EditorValue&);
private:
    bool matches(const SelectionSnapshot&) const;
    EditorResult<void> initializeDefaults();
    EditorValue contentValue() const;
    std::string id_, recipe_;
    editing::Revision revision_ = 1;
    EditRegion dirty_;
    EditorValue::Object values_;
};
struct TextureRecipePreviewArtifact { editing::Revision sourceRevision=0; int width=0,height=0; std::uint64_t checksum=0; };
/** @brief Generates a candidate image and publishes it only after complete success. */
class TextureRecipePreviewRuntime {
public:
    TextureRecipePreviewRuntime();
    ~TextureRecipePreviewRuntime();
    EditorResult<TextureRecipePreviewArtifact> generate(const TextureRecipeTarget&);
    /** @brief Access the generated image. @return Borrowed pointer owned by this runtime, or null. @lifetime Valid until the next generation or runtime destruction. */
    const image::ImageData* image() const { return image_.get(); }
    editing::Revision revision() const { return revision_; }
private:
    std::unique_ptr<image::ImageData> image_;
    editing::Revision revision_=0;
};
} // namespace eve::procgen_editing
