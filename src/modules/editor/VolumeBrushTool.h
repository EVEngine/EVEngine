#pragma once

#include "editor/EditorTool.h"

#include <string>
#include <vector>

namespace eve::editor {

class IBrushFalloff;

/** @brief One weighted integer coordinate emitted by a three-dimensional kernel. */
struct VolumeBrushPoint {
    int x = 0;
    int y = 0;
    int z = 0;
    float weight = 0.f;
};

/** @brief Shape protocol for sparse volume painting. */
class IVolumeBrushKernel {
public:
    virtual ~IVolumeBrushKernel() = default;
    /** @brief Emit weighted integer cells around one center. */
    virtual void sample(float x, float y, float z, float radius,
                        std::vector<VolumeBrushPoint>& out) const = 0;
};

/** @brief Spherical volume kernel using a non-owning falloff strategy. */
class SphereVolumeBrushKernel final : public IVolumeBrushKernel {
public:
    /** @brief Create a spherical kernel with an optional non-owning falloff. */
    explicit SphereVolumeBrushKernel(const IBrushFalloff* falloff = nullptr) : falloff_(falloff) {}
    /** @brief Replace the radial falloff used by later samples. */
    void setFalloff(const IBrushFalloff* falloff) { falloff_ = falloff; }
    void sample(float x, float y, float z, float radius,
                std::vector<VolumeBrushPoint>& out) const override;

private:
    const IBrushFalloff* falloff_ = nullptr;
};

/** @brief Axis-aligned cubic volume kernel using a non-owning falloff strategy. */
class BoxVolumeBrushKernel final : public IVolumeBrushKernel {
public:
    /** @brief Create a box kernel with an optional non-owning falloff. */
    explicit BoxVolumeBrushKernel(const IBrushFalloff* falloff = nullptr) : falloff_(falloff) {}
    /** @brief Replace the edge falloff used by later samples. */
    void setFalloff(const IBrushFalloff* falloff) { falloff_ = falloff; }
    void sample(float x, float y, float z, float radius,
                std::vector<VolumeBrushPoint>& out) const override;

private:
    const IBrushFalloff* falloff_ = nullptr;
};

/** @brief Paints one integer value into targets exposing IIntVolumeTarget. */
class PaintIntVolumeOperation {
public:
    /** @brief Create an operation that paints the supplied integer value. */
    explicit PaintIntVolumeOperation(int value = 1);
    /** @brief Set the value written by subsequent stamps. */
    void setValue(int value);
    /** @brief Return the value written by this operation. */
    int value() const { return value_; }
    /** @brief Build a reversible command for weighted cells. */
    std::unique_ptr<IEditCommand> createCommand(IEditableTarget* target,
                                                 const std::vector<VolumeBrushPoint>& points) const;

private:
    int value_ = 1;
};

/** @brief Generic 3D stroke tool composed from a volume kernel and paint operation. */
class VolumeBrushTool final : public IEditorTool {
public:
    /** @brief Create a reusable volume brush identified by a stable tool id. */
    VolumeBrushTool(std::string id, std::string label);
    const ToolDescriptor& descriptor() const override { return descriptor_; }
    /** @brief Replace the volume kernel used by later stamps. */
    void setKernel(const IVolumeBrushKernel* kernel) { kernel_ = kernel; }
    /** @brief Replace the integer operation used by later stamps. */
    void setOperation(const PaintIntVolumeOperation* operation) { operation_ = operation; }
    /** @brief Set radius in target voxel coordinates. */
    void setRadius(float radius);
    /** @brief Return radius in target voxel coordinates. */
    float radius() const { return radius_; }
    ToolResponse pointerEvent(EditorContext& context, const EditorPointerEvent& event) override;
    void cancel(EditorContext& context) override;
    void inspect(EditorContext& context, IEditorInspector& inspector) override;

private:
    bool stamp(EditorContext& context, float x, float y, float z);

    ToolDescriptor descriptor_;
    const IVolumeBrushKernel* kernel_ = nullptr;
    const PaintIntVolumeOperation* operation_ = nullptr;
    float radius_ = 0.5f;
    bool stroking_ = false;
    std::vector<VolumeBrushPoint> points_;
};

}  // namespace eve::editor
