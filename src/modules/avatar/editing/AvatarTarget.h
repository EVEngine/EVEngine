#pragma once
#include "common/Export.h"


#include "editing/EditableTarget.h"
#include "editing/EditingAuthority.h"
#include "editing/EditingProperty.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eve::avatar {
class AvatarInstance;
}
namespace eve::graphics {
class Texture;
}

namespace eve::avatar_editing {

using CapabilityId       = editing::CapabilityId;
using DiagnosticSeverity = editing::DiagnosticSeverity;
using DomainOperation    = editing::DomainOperation;
using EditRegion         = editing::EditRegion;
using EditorDiagnostic   = editing::Diagnostic;
template <class T>
using EditorResult                  = editing::Result<T>;
using EditorStatus                  = editing::Status;
using EditorValue                   = editing::Value;
using IDomainOperationTarget        = editing::IDomainOperationTarget;
using IDomainOperationTargetStaging = editing::IDomainOperationTargetStaging;
using IEditableTarget               = editing::IEditableTarget;
using IPropertyProvider             = editing::IPropertyProvider;
using ObjectId                      = editing::ObjectId;
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

/** @brief Stable image layer in an Avatar asset. */
struct AvatarLayerValue {
    ObjectId             id;
    std::string          name, textureAsset;
    int                  zIndex  = 0;
    bool                 visible = true;
    std::array<float, 2> offset{0, 0}, size{64, 64};
    std::array<float, 4> color{1, 1, 1, 1};
};
/** @brief Reflected Avatar parameter and its initial value. */
struct AvatarParameterValue {
    ObjectId    id;
    std::string name;
    float       defaultValue = 0, minimum = 0, maximum = 1, value = 0;
};
/** @brief Named expression containing layer/parameter channel targets. */
struct AvatarExpressionValue {
    ObjectId                     id;
    std::string                  name;
    std::map<std::string, float> channels;
};

/** @brief Revisioned image/Live2D/VRoid Avatar authoring asset. */
class EVENGINE_API_ORCHESTRATION AvatarDocumentTarget final : public ::eve::editing::EditableTargetState,
                                                              public virtual IEditableTarget,
                                                              public IDomainOperationTarget,
                                                              public IDomainOperationTargetStaging,
                                                              public IPropertyProvider {
public:
    /** @brief Avatar document target. */
    explicit AvatarDocumentTarget(std::string id);
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
    EditorResult<void>                      applyDomainOperation(const DomainOperation&) override;
    /** @brief Clones domain state. */
    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Commits domain state. */
    EditorResult<void>                      commitDomainState(std::unique_ptr<IDomainOperationTarget>) override;
    /** @brief Current revision. */
    eve::Result<eve::Revision>              currentRevision(const SelectionSnapshot&) const override;
    /** @brief Schema. */
    PropertySchema                          schema(const SelectionSnapshot&) const override;
    /** @brief Reads . */
    PropertyReadResult                      read(const SelectionSnapshot&, const PropertyPath&) const override;
    /** @brief Make set. */
    EditorResult<DomainOperation>           makeSet(const SelectionSnapshot&, const PropertyPath&, const EditorValue&,
                                                    PropertySetMode) const override;
    /** @brief Make reset. */
    EditorResult<DomainOperation>           makeReset(const SelectionSnapshot&, const PropertyPath&) const override;
    /** @brief Change backend kind and model source. */
    EditorResult<DomainOperation>             makeSetSource(std::string kind, std::string asset) const;
    /** @brief Make create layer. */
    EditorResult<DomainOperation>             makeCreateLayer(const AvatarLayerValue&) const;
    /** @brief Make delete layer. */
    EditorResult<DomainOperation>             makeDeleteLayer(const ObjectId&) const;
    /** @brief Make create parameter. */
    EditorResult<DomainOperation>             makeCreateParameter(const AvatarParameterValue&) const;
    /** @brief Make delete parameter. */
    EditorResult<DomainOperation>             makeDeleteParameter(const ObjectId&) const;
    /** @brief Make create expression. */
    EditorResult<DomainOperation>             makeCreateExpression(const AvatarExpressionValue&) const;
    /** @brief Make delete expression. */
    EditorResult<DomainOperation>             makeDeleteExpression(const ObjectId&) const;
    /** @brief Kind. */
    const std::string&                        kind() const { return kind_; }
    /** @brief Source asset. */
    const std::string&                        sourceAsset() const { return sourceAsset_; }
    /** @brief Layers. */
    const std::vector<AvatarLayerValue>&      layers() const { return layers_; }
    /** @brief Parameters. */
    const std::vector<AvatarParameterValue>&  parameters() const { return parameters_; }
    /** @brief Expressions. */
    const std::vector<AvatarExpressionValue>& expressions() const { return expressions_; }
    /** @brief Validate. */
    std::vector<EditorDiagnostic>             validate() const;
    /** @brief Snapshot value. */
    EditorValue                               snapshotValue() const;
    /** @brief Loads snapshot. */
    EditorResult<void>                        loadSnapshot(const EditorValue&);

private:
    /** @brief Matches. */
    bool                               matches(const SelectionSnapshot&) const;
    /** @brief Content value. */
    EditorValue                        contentValue() const;
    /** @brief Replacement. */
    EditorResult<DomainOperation>      replacement(EditorValue, std::string = {}) const;
    std::string                        id_, kind_ = "image", sourceAsset_;
    std::vector<AvatarLayerValue>      layers_;
    std::vector<AvatarParameterValue>  parameters_;
    std::vector<AvatarExpressionValue> expressions_;
};

/** @brief Resolves image-layer textures for candidate publication. */
class IAvatarTextureResolver {
public:
    /** @brief Releases IAvatarTextureResolver resources. */
    virtual ~IAvatarTextureResolver()                                          = default;
    /** @brief Texture. */
    virtual EditorResult<graphics::Texture*> texture(const std::string&) const = 0;
};

/** @brief Candidate-first live AvatarInstance generation. */
class EVENGINE_API_ORCHESTRATION AvatarDocumentRuntime {
public:
    /** @brief Avatar document runtime. */
    AvatarDocumentRuntime();
    /** @brief Avatar document runtime. */
    ~AvatarDocumentRuntime();
    /** @brief Build all layers, metadata and expressions before replacing the live generation.
     * @param textures Optional borrowed resolver; it is used only during this call and is never retained.
     * @lifetime The caller must keep textures alive for the duration of publish.
     */
    EditorResult<void>      publish(const AvatarDocumentTarget&, const IAvatarTextureResolver* textures = nullptr);
    /** @brief Instance. */
    avatar::AvatarInstance* instance() const { return instance_.get(); }
    /** @brief Revision. */
    Revision                revision() const { return revision_; }

private:
    std::unique_ptr<avatar::AvatarInstance> instance_;
    Revision                                revision_ = 0;
};

}  // namespace eve::avatar_editing
