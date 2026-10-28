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
using editing::Result;
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
class EVENGINE_API_DOMAINS SpriteStackDocumentTarget final : public ::eve::editing::EditableTargetState,
                                                             public virtual IEditableTarget,
                                                             public IDomainOperationTarget,
                                                             public IDomainOperationTargetStaging,
                                                             public IPropertyProvider {
public:
    /** @brief Sprite stack document target. */
    explicit SpriteStackDocumentTarget(std::string id);
    /** @brief Target id. */
    TargetId         targetId() const override { return TargetId(id_); }
    /** @brief Describe. */
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime
     * Valid until this target is destroyed or mutated. */
    void*                                   queryCapability(const CapabilityId& capability) override;
    /** @brief Applies domain operation. */
    Result<void>                      applyDomainOperation(const DomainOperation& operation) override;
    /** @brief Clones domain state. */
    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Commits domain state. */
    Result<void>            commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override;
    /** @brief Current revision. */
    eve::Result<eve::Revision>    currentRevision(const SelectionSnapshot& selection) const override;
    /** @brief Schema. */
    PropertySchema                schema(const SelectionSnapshot& selection) const override;
    /** @brief Reads read. */
    PropertyReadResult            read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    /** @brief Make set. */
    Result<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    /** @brief Make reset. */
    Result<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath&      path) const override;
    /** @brief Value. */
    const SpriteStackAssetValue&  value() const { return value_; }
    /** @brief Validate source, sampling limits, output memory and presentation values. */
    std::vector<EditorDiagnostic> validate() const;
    /** @brief Snapshot value. */
    EditorValue                   snapshotValue() const;
    /** @brief Loads snapshot. */
    Result<void>            loadSnapshot(const EditorValue& snapshot);

private:
    /** @brief Matches. */
    bool                          matches(const SelectionSnapshot& selection) const;
    /** @brief Content value. */
    EditorValue                   contentValue() const;
    /** @brief Replacement. */
    Result<DomainOperation> replacement(EditorValue content, std::string property) const;
    std::string                   id_;
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
    /** @brief Releases ISpriteStackModelResolver resources. */
    virtual ~ISpriteStackModelResolver() = default;
    /** @brief Resolve a ready, borrowed ModelData for the duration of bake(). */
    virtual Result<model3d::ModelData*> resolveModel(const std::string& assetId) const = 0;
};

/** @brief Candidate-first CPU baker and optional live SpriteStack2D publisher. */
class EVENGINE_API_DOMAINS SpriteStackBakeRuntime {
public:
    /** @brief Sprite stack bake runtime. */
    SpriteStackBakeRuntime();
    /** @brief Sprite stack bake runtime. */
    ~SpriteStackBakeRuntime();
    // Class-level dllexport instantiates every member, including the implicitly
    // declared copy assignment, whose body instantiates
    // `std::vector<std::unique_ptr<image::ImageData>>::operator=` and fails on the
    // non-copyable element (C2280). The member was already non-copyable in
    // practice; make that explicit instead of relying on the implicit definition.
    SpriteStackBakeRuntime(const SpriteStackBakeRuntime&)            = delete;
    SpriteStackBakeRuntime& operator=(const SpriteStackBakeRuntime&) = delete;
    /** @brief Bake all layers in temporary ownership before replacing the generation. */
    Result<std::vector<SpriteStackLayerArtifact>> bake(const SpriteStackDocumentTarget& document,
                                                             const ISpriteStackModelResolver* resolver = nullptr);
    /** @brief Create and populate a live stack from the current baked generation. */
    Result<spritestack::SpriteStack2D*> publish(graphics::Graphics* graphics, Revision expectedRevision);
    /** @brief Layers. */
    const std::vector<std::unique_ptr<image::ImageData>>& layers() const { return layers_; }
    /** @brief Revision. */
    Revision                                              revision() const { return revision_; }

private:
    std::vector<std::unique_ptr<image::ImageData>> layers_;
    std::unique_ptr<spritestack::SpriteStack2D>    stack_;
    SpriteStackAssetValue                          value_;
    Revision                                       revision_ = 0;
};

}  // namespace eve::spritestack_editing
