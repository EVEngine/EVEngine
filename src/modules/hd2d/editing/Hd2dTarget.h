#pragma once
#include <array>
#include <memory>
#include <string>
#include "editing/EditableTarget.h"
#include "editing/EditingAuthority.h"
#include "editing/EditingProperty.h"
namespace eve::graphics {
class Graphics;
class Texture;
class Camera3D;
}  // namespace eve::graphics
namespace eve::hd2d {
class Hd2D;
class Sprite3D;
class TileMap3D;
}  // namespace eve::hd2d
namespace eve::hd2d_editing {
using CapabilityId       = editing::CapabilityId;
using DiagnosticSeverity = editing::DiagnosticSeverity;
using DomainOperation    = editing::DomainOperation;
using EditRegion         = editing::EditRegion;
using EditorDiagnostic   = editing::Diagnostic;
using editing::Result;
using EditorStatus                  = editing::Status;
using EditorValue                   = editing::Value;
using IDomainOperationTarget        = editing::IDomainOperationTarget;
using IDomainOperationTargetStaging = editing::IDomainOperationTargetStaging;
using IEditableTarget               = editing::IEditableTarget;
using IPropertyProvider             = editing::IPropertyProvider;
using PropertyDescriptor            = editing::PropertyDescriptor;
using PropertyFlag                  = editing::PropertyFlag;
using PropertyPath                  = editing::PropertyPath;
using PropertyReadResult            = editing::PropertyReadResult;
using PropertyReadState             = editing::PropertyReadState;
using PropertySchema                = editing::PropertySchema;
using PropertySetMode               = editing::PropertySetMode;
using PropertyType                  = editing::PropertyType;
using Revision                      = editing::Revision;
using RuleId                        = editing::RuleId;
using SelectionSnapshot             = editing::SelectionSnapshot;
using TargetDescriptor              = editing::TargetDescriptor;
using TargetId                      = editing::TargetId;
using editing::validatePropertyValue;
/** @brief Authored HD-2D sprite-sheet and tile-extrusion preset. */
struct Hd2dAssetValue {
    std::string          kind = "sprite";
    std::string          sourceAsset;
    int                  columns = 1, rows = 1, frame = 0, animStart = 0, animEnd = 0;
    float                fps   = 12;
    bool                 flipX = false, flipY = false, visible = true;
    std::array<float, 2> size{1, 1};
    std::array<float, 4> tint{1, 1, 1, 1};
    float                sideDepth = 6, heightScale = 1;
    std::array<float, 4> wallUv{0, 0, .05f, .05f};
};
/** @brief Revisioned HD-2D sprite/tilemap presentation asset. */
class EVENGINE_API_ORCHESTRATION Hd2dDocumentTarget final : public ::eve::editing::EditableTargetState,
                                                            public virtual IEditableTarget,
                                                            public IDomainOperationTarget,
                                                            public IDomainOperationTargetStaging,
                                                            public IPropertyProvider {
public:
    /** @brief Hd 2 d document target. */
    explicit Hd2dDocumentTarget(std::string id);
    /** @brief Target id. */
    TargetId                                targetId() const override { return TargetId(id_); }
    /** @brief Describe. */
    TargetDescriptor                        describe() const override;
    /**
     * @brief Query an optional stable editing capability.
     * @return Borrowed pointer owned by this target, or null when unsupported.
     * @lifetime Valid until this target is destroyed or the capability is explicitly invalidated.
     */
    void*                                   queryCapability(const CapabilityId&) override;
    /** @brief Applies domain operation. */
    Result<void>                      applyDomainOperation(const DomainOperation&) override;
    /** @brief Clones domain state. */
    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Commits domain state. */
    Result<void>                      commitDomainState(std::unique_ptr<IDomainOperationTarget>) override;
    /** @brief Current revision. */
    eve::Result<eve::Revision>              currentRevision(const SelectionSnapshot&) const override;
    /** @brief Schema. */
    PropertySchema                          schema(const SelectionSnapshot&) const override;
    /** @brief Reads . */
    PropertyReadResult                      read(const SelectionSnapshot&, const PropertyPath&) const override;
    /** @brief Make set. */
    Result<DomainOperation>           makeSet(const SelectionSnapshot&, const PropertyPath&, const EditorValue&,
                                                    PropertySetMode) const override;
    /** @brief Make reset. */
    Result<DomainOperation>           makeReset(const SelectionSnapshot&, const PropertyPath&) const override;
    /** @brief Value. */
    const Hd2dAssetValue&                   value() const { return value_; }
    /** @brief Validate. */
    std::vector<EditorDiagnostic>           validate() const;
    /** @brief Snapshot value. */
    EditorValue                             snapshotValue() const;
    /** @brief Loads snapshot. */
    Result<void>                      loadSnapshot(const EditorValue&);

private:
    /** @brief Matches. */
    bool                          matches(const SelectionSnapshot&) const;
    /** @brief Content value. */
    EditorValue                   contentValue() const;
    /** @brief Replacement. */
    Result<DomainOperation> replacement(EditorValue, std::string) const;
    std::string                   id_;
    Hd2dAssetValue                value_;
};
/** @brief Deterministically sampled sprite-sheet frame and UV rectangle. */
struct Hd2dFramePreview {
    int                  frame = 0;
    std::array<float, 4> uv{0, 0, 1, 1};
};
/** @brief Pure sprite animation scrub evaluator. */
class EVENGINE_API_ORCHESTRATION Hd2dFramePreviewService {
public:
    /** @brief Evaluate. */
    Result<Hd2dFramePreview> evaluate(const Hd2dDocumentTarget&, float time) const;
};
/** @brief Resolves an HD-2D sprite texture. */
class IHd2dTextureResolver {
public:
    /** @brief Releases IHd2dTextureResolver resources. */
    virtual ~IHd2dTextureResolver()                                            = default;
    /** @brief Texture. */
    virtual Result<graphics::Texture*> texture(const std::string&) const = 0;
};
/** @brief Candidate-first live Sprite3D/TileMap3D preset publication. */
class EVENGINE_API_ORCHESTRATION Hd2dDocumentRuntime {
public:
    /** @brief Hd 2 d document runtime. */
    Hd2dDocumentRuntime();
    /** @brief Hd 2 d document runtime. */
    ~Hd2dDocumentRuntime();
    /** @brief Publish sprite. */
    Result<hd2d::Sprite3D*>  publishSprite(const Hd2dDocumentTarget&, hd2d::Hd2D*, graphics::Graphics*,
                                                 graphics::Camera3D*, const IHd2dTextureResolver&);
    /** @brief Publish tile map. */
    Result<hd2d::TileMap3D*> publishTileMap(const Hd2dDocumentTarget&, hd2d::Hd2D*);
    /** @brief Revision. */
    Revision                       revision() const { return revision_; }

private:
    std::unique_ptr<hd2d::Sprite3D>  sprite_;
    std::unique_ptr<hd2d::TileMap3D> tilemap_;
    Revision                         revision_ = 0;
};
}  // namespace eve::hd2d_editing
