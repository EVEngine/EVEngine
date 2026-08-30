#include "editor/FieldBrushTool.h"

#include "editor/EditCommand.h"
#include "editor/EditorPresentation.h"
#include "editor/EditorSession.h"

#include <algorithm>

namespace eve::editor {

std::unique_ptr<IEditCommand> PaintIntFieldOperation::createCommand(
    IEditableTarget *target, const BrushSampleBuffer &samples, float strength) const {
    if (!target || !target->query<IIntFieldTarget>()) return nullptr;
    auto command = std::make_unique<IntFieldEditCommand>("paint integer field", target);
    if (strength <= 0.f) return command;
    for (int i = 0; i < samples.size(); ++i) {
        const BrushPoint &point = samples.point(i);
        if (point.weight > 0.f) command->record(point.x, point.y, value_);
    }
    return command;
}

std::unique_ptr<IEditCommand> AddScalarFieldOperation::createCommand(
    IEditableTarget *target, const BrushSampleBuffer &samples, float strength) const {
    auto *field = target ? target->query<IScalarFieldTarget>() : nullptr;
    if (!field) return nullptr;
    auto command = std::make_unique<ScalarFieldEditCommand>("add scalar field", target);
    for (int i = 0; i < samples.size(); ++i) {
        const BrushPoint &point = samples.point(i);
        if (point.weight > 0.f && field->containsCell(point.x, point.y)) {
            command->record(point.x, point.y,
                            field->readScalar(point.x, point.y) + strength * point.weight);
        }
    }
    return command;
}

FieldBrushTool::FieldBrushTool(std::string id, std::string label, const IBrushKernel *kernel,
                               const IFieldBrushOperation *operation)
    : descriptor_{std::move(id), std::move(label), {}}, kernel_(kernel), operation_(operation) {}

void FieldBrushTool::setRadius(float radius) { radius_ = std::max(0.5f, radius); }

bool FieldBrushTool::stamp(EditorContext &context, float x, float y) {
    if (!kernel_ || !operation_ || !context.target()) return false;
    samples_.clear();
    kernel_->sample({x, y, radius_, 0.f}, samples_);
    auto command = operation_->createCommand(context.target(), samples_, strength_);
    return command && context.execute(std::move(command));
}

ToolResponse FieldBrushTool::pointerEvent(EditorContext &context, const EditorPointerEvent &event) {
    cursorX_ = event.x;
    cursorY_ = event.y;
    if (event.phase == EditorPointerEvent::Phase::Down) {
        if (!kernel_ || !operation_ || !context.target() ||
            !context.transactions().begin(descriptor_.label)) return ToolResponse::ignored();
        stroking_ = true;
        stamp(context, event.x, event.y);
        return ToolResponse::capture();
    }
    if (!stroking_) return ToolResponse::ignored();
    if (event.phase == EditorPointerEvent::Phase::Move) {
        stamp(context, event.x, event.y);
        return ToolResponse::consumed();
    }
    if (event.phase == EditorPointerEvent::Phase::Up) {
        context.transactions().commit();
        stroking_ = false;
        return ToolResponse::release();
    }
    cancel(context);
    return ToolResponse::release();
}

void FieldBrushTool::cancel(EditorContext &context) {
    if (stroking_) context.transactions().rollback();
    stroking_ = false;
}

void FieldBrushTool::drawOverlay(EditorContext &, IEditorOverlay &overlay) {
    overlay.circle({cursorX_, cursorY_, 0.f}, radius_, {0x55ffffffU, 1.f, false});
}

void FieldBrushTool::inspect(EditorContext &, IEditorInspector &inspector) {
    inspector.beginGroup(descriptor_.id, descriptor_.label);
    inspector.scalar("radius", "Radius", radius_, 0.5f, 256.f);
    inspector.scalar("strength", "Strength", strength_, -100.f, 100.f);
    inspector.endGroup();
    setRadius(radius_);
}

}  // namespace eve::editor
