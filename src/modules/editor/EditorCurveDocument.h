#pragma once

#include "editor/EditorAuthority.h"
#include "editor/EditorTarget.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief Stable scalar curve key with editable Hermite tangents. */
struct EditorCurveKey {
    StableId id;
    double time = 0.0;
    double value = 0.0;
    double inTangent = 0.0;
    double outTangent = 0.0;
    std::string interpolation = "linear";
};

/** @brief Stable normalized RGBA gradient stop. */
struct EditorGradientStop {
    StableId id;
    double time = 0.0;
    std::array<double, 4> color{1.0, 1.0, 1.0, 1.0};
};

/** @brief Bounded revision-tagged curve/gradient lookup preview. */
struct EditorCurvePreview {
    EditorStatus status = EditorStatus::Failed;
    Revision documentRevision = 0;
    std::vector<double> curveSamples;
    std::vector<std::array<double, 4>> gradientSamples;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Stable key/gradient editing capability shared by effects and animation presenters. */
class ICurveDocumentEditTarget {
public:
    virtual ~ICurveDocumentEditTarget() = default;
    static CapabilityId editorCapabilityId() { return CapabilityId("eve.editor.target.curve-document"); }
    /** @brief Plan creation or replacement of a scalar key. */
    virtual EditorResult<DomainOperation> makeSetKey(const EditorCurveKey& key) const = 0;
    /** @brief Plan deletion of a scalar key. */
    virtual EditorResult<DomainOperation> makeDeleteKey(const StableId& key) const = 0;
    /** @brief Plan creation or replacement of a gradient stop. */
    virtual EditorResult<DomainOperation> makeSetStop(const EditorGradientStop& stop) const = 0;
    /** @brief Plan deletion of a gradient stop. */
    virtual EditorResult<DomainOperation> makeDeleteStop(const StableId& stop) const = 0;
};

/** @brief UI-neutral reversible curve and gradient timeline document. */
class EditorCurveDocument final : public virtual IEditableTarget,
                                  public IDomainOperationTarget,
                                  public IDomainOperationTargetStaging,
                                  public ICurveDocumentEditTarget {
public:
    explicit EditorCurveDocument(std::string id);
    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime Valid until this target is destroyed or mutated. */
    void* queryCapability(const CapabilityId& capability) override;
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    [[nodiscard]] EditorResult<void> commitDomainState(
        std::unique_ptr<IDomainOperationTarget> candidate) override;
    EditorResult<DomainOperation> makeSetKey(const EditorCurveKey& key) const override;
    EditorResult<DomainOperation> makeDeleteKey(const StableId& key) const override;
    EditorResult<DomainOperation> makeSetStop(const EditorGradientStop& stop) const override;
    EditorResult<DomainOperation> makeDeleteStop(const StableId& stop) const override;
    /** @brief Return scalar keys in stable timeline order. */
    std::vector<EditorCurveKey> keys() const;
    /** @brief Return gradient stops in stable timeline order. */
    std::vector<EditorGradientStop> stops() const;
    /** @brief Sample scalar curve at normalized time. */
    double sampleCurve(double time) const;
    /** @brief Sample gradient at normalized time. */
    std::array<double, 4> sampleGradient(double time) const;
    /** @brief Build a bounded lookup preview for viewport/material upload. */
    EditorCurvePreview preview(int sampleCount, int maximumSamples = 4096) const;
    /** @brief Capture deterministic schema-version-one timeline data. */
    EditorValue snapshotValue() const;
    /** @brief Atomically load a validated timeline snapshot. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);

private:
    std::string id_;
    Revision revision_ = 1;
    EditRegion dirty_;
    std::map<StableId, EditorCurveKey> keys_;
    std::map<StableId, EditorGradientStop> stops_;
};

}  // namespace eve::editor
