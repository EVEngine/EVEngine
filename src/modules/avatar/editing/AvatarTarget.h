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
using editing::Result;
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
    explicit AvatarDocumentTarget(std::string id);
    TargetId                                targetId() const override { return TargetId(id_); }
    TargetDescriptor                        describe() const override;
    /**
     * @brief Query an optional stable editing capability.
     * @return Borrowed pointer owned by this target, or null when unsupported.
     * @lifetime Valid until this target is destroyed or the capability is explicitly invalidated.
     */
    void*                                   queryCapability(const CapabilityId&) override;
    Result<void>                      applyDomainOperation(const DomainOperation&) override;
    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    Result<void>                      commitDomainState(std::unique_ptr<IDomainOperationTarget>) override;
    eve::Result<eve::Revision>              currentRevision(const SelectionSnapshot&) const override;
    PropertySchema                          schema(const SelectionSnapshot&) const override;
    PropertyReadResult                      read(const SelectionSnapshot&, const PropertyPath&) const override;
    Result<DomainOperation>           makeSet(const SelectionSnapshot&, const PropertyPath&, const EditorValue&,
                                                    PropertySetMode) const override;
    Result<DomainOperation>           makeReset(const SelectionSnapshot&, const PropertyPath&) const override;
    /** @brief Change backend kind and model source. */
    Result<DomainOperation>             makeSetSource(std::string kind, std::string asset) const;
    Result<DomainOperation>             makeCreateLayer(const AvatarLayerValue&) const;
    Result<DomainOperation>             makeDeleteLayer(const ObjectId&) const;
    Result<DomainOperation>             makeCreateParameter(const AvatarParameterValue&) const;
    Result<DomainOperation>             makeDeleteParameter(const ObjectId&) const;
    Result<DomainOperation>             makeCreateExpression(const AvatarExpressionValue&) const;
    Result<DomainOperation>             makeDeleteExpression(const ObjectId&) const;
    const std::string&                        kind() const { return kind_; }
    const std::string&                        sourceAsset() const { return sourceAsset_; }
    const std::vector<AvatarLayerValue>&      layers() const { return layers_; }
    const std::vector<AvatarParameterValue>&  parameters() const { return parameters_; }
    const std::vector<AvatarExpressionValue>& expressions() const { return expressions_; }
    std::vector<EditorDiagnostic>             validate() const;
    EditorValue                               snapshotValue() const;
    Result<void>                        loadSnapshot(const EditorValue&);

private:
    bool                               matches(const SelectionSnapshot&) const;
    EditorValue                        contentValue() const;
    Result<DomainOperation>      replacement(EditorValue, std::string = {}) const;
    std::string                        id_, kind_ = "image", sourceAsset_;
    std::vector<AvatarLayerValue>      layers_;
    std::vector<AvatarParameterValue>  parameters_;
    std::vector<AvatarExpressionValue> expressions_;
};

/** @brief Resolves image-layer textures for candidate publication. */
class IAvatarTextureResolver {
public:
    virtual ~IAvatarTextureResolver()                                          = default;
    virtual Result<graphics::Texture*> texture(const std::string&) const = 0;
};

/** @brief Candidate-first live AvatarInstance generation. */
class EVENGINE_API_ORCHESTRATION AvatarDocumentRuntime {
public:
    AvatarDocumentRuntime();
    ~AvatarDocumentRuntime();
    /** @brief Build all layers, metadata and expressions before replacing the live generation.
     * @param textures Optional borrowed resolver; it is used only during this call and is never retained.
     * @lifetime The caller must keep textures alive for the duration of publish.
     */
    Result<void>      publish(const AvatarDocumentTarget&, const IAvatarTextureResolver* textures = nullptr);
    avatar::AvatarInstance* instance() const { return instance_.get(); }
    Revision                revision() const { return revision_; }

private:
    std::unique_ptr<avatar::AvatarInstance> instance_;
    Revision                                revision_ = 0;
};

}  // namespace eve::avatar_editing
