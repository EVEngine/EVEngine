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
class EVENGINE_API_ORCHESTRATION TextureRecipeTarget final : public ::eve::editing::EditableTargetState,
                                                             public virtual IEditableTarget,
                                                             public IDomainOperationTarget,
                                                             public IDomainOperationTargetStaging,
                                                             public IPropertyProvider {
public:
    /** @brief Construct a registered procedural texture recipe target. @throws std::invalid_argument When recipe is not registered. */
    TextureRecipeTarget(std::string id, std::string recipe);
    /** @brief Target id. */
    TargetId         targetId() const override { return TargetId(id_); }
    /** @brief Describe. */
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime Valid until this target is destroyed or mutated. */
    void* queryCapability(const CapabilityId&) override;
    /** @brief Applies domain operation. */
    EditorResult<void> applyDomainOperation(const DomainOperation&) override;
    /** @brief Clones domain state. */
    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Commits domain state. */
    EditorResult<void> commitDomainState(std::unique_ptr<IDomainOperationTarget>) override;
    /** @brief Current revision. */
    eve::Result<eve::Revision> currentRevision(const SelectionSnapshot&) const override;
    /** @brief Schema. */
    PropertySchema schema(const SelectionSnapshot&) const override;
    /** @brief Reads . */
    PropertyReadResult read(const SelectionSnapshot&, const PropertyPath&) const override;
    /** @brief Make set. */
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot&, const PropertyPath&,
                                          const EditorValue&, PropertySetMode) const override;
    /** @brief Make reset. */
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot&, const PropertyPath&) const override;
    /** @brief Recipe. */
    const std::string& recipe() const { return recipe_; }
    /** @brief Values. */
    const EditorValue::Object& values() const { return values_; }
    /** @brief Validate. */
    std::vector<EditorDiagnostic> validate() const;
    /** @brief Snapshot value. */
    EditorValue snapshotValue() const;
    /** @brief Loads snapshot. */
    EditorResult<void> loadSnapshot(const EditorValue&);
private:
    /** @brief Matches. */
    bool matches(const SelectionSnapshot&) const;
    /** @brief Initialize defaults. */
    EditorResult<void> initializeDefaults();
    /** @brief Content value. */
    EditorValue contentValue() const;
    std::string         id_, recipe_;
    EditorValue::Object values_;
};
/** @brief TextureRecipePreviewArtifact public API. */
struct TextureRecipePreviewArtifact { editing::Revision sourceRevision=0; int width=0,height=0; std::uint64_t checksum=0; };
/** @brief Generates a candidate image and publishes it only after complete success. */
class EVENGINE_API_ORCHESTRATION TextureRecipePreviewRuntime {
public:
    /** @brief Texture recipe preview runtime. */
    TextureRecipePreviewRuntime();
    /** @brief Texture recipe preview runtime. */
    ~TextureRecipePreviewRuntime();
    /** @brief Generate. */
    EditorResult<TextureRecipePreviewArtifact> generate(const TextureRecipeTarget&);
    /** @brief Access the generated image. @return Borrowed pointer owned by this runtime, or null. @lifetime Valid until the next generation or runtime destruction. */
    const image::ImageData* image() const { return image_.get(); }
    /** @brief Revision. */
    editing::Revision revision() const { return revision_; }
private:
    std::unique_ptr<image::ImageData> image_;
    editing::Revision revision_=0;
};
} // namespace eve::procgen_editing
