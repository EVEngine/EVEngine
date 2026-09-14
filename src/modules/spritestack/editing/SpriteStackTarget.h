#pragma once

#include "editing/EditableTarget.h"
#include "editing/EditingProperty.h"
#include "editing/EditingTargetOperations.h"
#include "spritestack/SpriteStack.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
}
namespace eve::image {
class ImageData;
}
namespace eve::model3d {
class ModelData;
}
namespace eve::spritestack {
class SpriteStack2D;
}

namespace eve::spritestack_editing {

using editing::CapabilityId;
using editing::DiagnosticSeverity;
using editing::DomainOperation;
using editing::EditRegion;
using editing::IDomainOperationTarget;
using editing::IDomainOperationTargetStaging;
using editing::IEditableTarget;
using editing::IPropertyProvider;
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

/** @brief Complete authored sprite-stack bake and presentation preset. */
struct SpriteStackAssetValue {
    std::string               sourceKind = "primitive";
    std::string               source     = "box";
    spritestack::SliceOptions bake;
    float                     displayWidth  = 64.f;
    float                     displayHeight = 64.f;
    float                     layerOffset   = 1.f;
    bool                      shadow        = true;
    float                     shadowOpacity = .3f;
    float                     outlineWidth  = 0.f;
};

/** @brief Revisioned SpriteStack bake preset with reusable Inspector metadata. */
class SpriteStackDocumentTarget final : public virtual IEditableTarget,
                                        public IDomainOperationTarget,
                                        public IDomainOperationTargetStaging,
                                        public IPropertyProvider {
public:
    explicit SpriteStackDocumentTarget(std::string id);
    TargetId         targetId() const override { return TargetId(id_); }
    std::uint64_t    revision() const override { return revision_; }
    EditRegion       dirtyRegion() const override { return dirty_; }
    void             clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime
     * Valid until this target is destroyed or mutated. */
    void*                                   queryCapability(const CapabilityId& capability) override;
    EditorResult<void>                      applyDomainOperation(const DomainOperation& operation) override;
    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    EditorResult<void>            commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override;
    eve::Result<eve::Revision>    currentRevision(const SelectionSnapshot& selection) const override;
    PropertySchema                schema(const SelectionSnapshot& selection) const override;
    PropertyReadResult            read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath&      path) const override;
    const SpriteStackAssetValue&  value() const { return value_; }
    /** @brief Validate source, sampling limits, output memory and presentation values. */
    std::vector<EditorDiagnostic> validate() const;
    EditorValue                   snapshotValue() const;
    EditorResult<void>            loadSnapshot(const EditorValue& snapshot);

private:
    bool                          matches(const SelectionSnapshot& selection) const;
    EditorValue                   contentValue() const;
    EditorResult<DomainOperation> replacement(EditorValue content, std::string property) const;
    std::string                   id_;
    Revision                      revision_ = 1;
    EditRegion                    dirty_;
    SpriteStackAssetValue         value_;
};

/** @brief Metadata for one immutable baked layer. */
struct SpriteStackLayerArtifact {
    int           index = 0, width = 0, height = 0;
    std::uint64_t checksum      = 0;
    double        alphaCoverage = 0;
};

/** @brief Resolves model assets without coupling the document to AssetDB ownership. */
class ISpriteStackModelResolver {
public:
    virtual ~ISpriteStackModelResolver() = default;
    /** @brief Resolve a ready, borrowed ModelData for the duration of bake(). */
    virtual EditorResult<model3d::ModelData*> resolveModel(const std::string& assetId) const = 0;
};

/** @brief Candidate-first CPU baker and optional live SpriteStack2D publisher. */
class SpriteStackBakeRuntime {
public:
    SpriteStackBakeRuntime();
    ~SpriteStackBakeRuntime();
    /** @brief Bake all layers in temporary ownership before replacing the generation. */
    EditorResult<std::vector<SpriteStackLayerArtifact>> bake(const SpriteStackDocumentTarget& document,
                                                             const ISpriteStackModelResolver* resolver = nullptr);
    /** @brief Create and populate a live stack from the current baked generation. */
    EditorResult<spritestack::SpriteStack2D*> publish(graphics::Graphics* graphics, Revision expectedRevision);
    const std::vector<std::unique_ptr<image::ImageData>>& layers() const { return layers_; }
    Revision                                              revision() const { return revision_; }

private:
    std::vector<std::unique_ptr<image::ImageData>> layers_;
    std::unique_ptr<spritestack::SpriteStack2D>    stack_;
    SpriteStackAssetValue                          value_;
    Revision                                       revision_ = 0;
};

}  // namespace eve::spritestack_editing
