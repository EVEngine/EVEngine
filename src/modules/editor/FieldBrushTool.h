#pragma once

#include "editor/BrushKernel.h"
#include "editor/EditorTool.h"

#include <memory>

namespace eve::editor {

/** @brief Replaceable operation that converts brush samples into a command. */
class IFieldBrushOperation {
public:
    virtual ~IFieldBrushOperation() = default;

    /** @brief Create one reversible command, or nullptr for an incompatible target. */
    virtual std::unique_ptr<IEditCommand> createCommand(IEditableTarget *target,
                                                        const BrushSampleBuffer &samples,
                                                        float strength) const = 0;
};

/** @brief Paints a fixed integer into targets exposing IIntFieldTarget. */
class PaintIntFieldOperation final : public IFieldBrushOperation {
public:
    explicit PaintIntFieldOperation(int value = 1) : value_(value) {}
    /** @brief Change the integer written by subsequent stamps. */
    void setValue(int value) { value_ = value; }
    /** @brief Return the integer written by this operation. */
    int value() const { return value_; }
    std::unique_ptr<IEditCommand> createCommand(IEditableTarget *target,
                                                const BrushSampleBuffer &samples,
                                                float strength) const override;
private:
    int value_ = 1;
};

/** @brief Adds weighted strength to targets exposing IScalarFieldTarget. */
class AddScalarFieldOperation final : public IFieldBrushOperation {
public:
    std::unique_ptr<IEditCommand> createCommand(IEditableTarget *target,
                                                const BrushSampleBuffer &samples,
                                                float strength) const override;
};

/**
 * @brief Generic stroke tool composed from a brush kernel and field operation.
 *
 * Both collaborators are non-owning and may be replaced between strokes.
 */
class FieldBrushTool final : public IEditorTool {
public:
    FieldBrushTool(std::string id, std::string label, const IBrushKernel *kernel,
                   const IFieldBrushOperation *operation);
    const ToolDescriptor &descriptor() const override { return descriptor_; }
    /** @brief Replace the shape/falloff implementation used by later stamps. */
    void setKernel(const IBrushKernel *kernel) { kernel_ = kernel; }
    /** @brief Replace the edit semantics used by later stamps. */
    void setOperation(const IFieldBrushOperation *operation) { operation_ = operation; }
    /** @brief Set brush radius in target coordinates. */
    void setRadius(float radius);
    /** @brief Set the operation strength; negative values are permitted. */
    void setStrength(float strength) { strength_ = strength; }
    float radius() const { return radius_; }
    float strength() const { return strength_; }

    ToolResponse pointerEvent(EditorContext &context, const EditorPointerEvent &event) override;
    void cancel(EditorContext &context) override;
    void drawOverlay(EditorContext &context, IEditorOverlay &overlay) override;
    void inspect(EditorContext &context, IEditorInspector &inspector) override;

private:
    bool stamp(EditorContext &context, float x, float y);

    ToolDescriptor descriptor_;
    const IBrushKernel *kernel_ = nullptr;
    const IFieldBrushOperation *operation_ = nullptr;
    float radius_ = 1.f;
    float strength_ = 1.f;
    float cursorX_ = 0.f;
    float cursorY_ = 0.f;
    bool stroking_ = false;
    BrushSampleBuffer samples_;
};

}  // namespace eve::editor
