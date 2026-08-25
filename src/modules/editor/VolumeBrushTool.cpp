#include "editor/VolumeBrushTool.h"

#include "editor/BrushKernel.h"
#include "editor/EditorPresentation.h"
#include "editor/EditorSession.h"
#include "editor/EditorVolumeTarget.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::editor {
namespace {

float falloffWeight(const IBrushFalloff* falloff, float normalizedDistance) {
    return falloff ? falloff->evaluate(normalizedDistance) : (normalizedDistance <= 1.f ? 1.f : 0.f);
}

}  // namespace

void SphereVolumeBrushKernel::sample(float x, float y, float z, float radius,
                                     std::vector<VolumeBrushPoint>& out) const {
    out.clear();
    radius = std::max(0.5f, radius);
    const int minX = static_cast<int>(std::floor(x - radius));
    const int maxX = static_cast<int>(std::ceil(x + radius));
    const int minY = static_cast<int>(std::floor(y - radius));
    const int maxY = static_cast<int>(std::ceil(y + radius));
    const int minZ = static_cast<int>(std::floor(z - radius));
    const int maxZ = static_cast<int>(std::ceil(z + radius));
    for (int iz = minZ; iz <= maxZ; ++iz)
        for (int iy = minY; iy <= maxY; ++iy)
            for (int ix = minX; ix <= maxX; ++ix) {
                const float dx = static_cast<float>(ix) - x;
                const float dy = static_cast<float>(iy) - y;
                const float dz = static_cast<float>(iz) - z;
                const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (distance <= radius) out.push_back({ix, iy, iz, falloffWeight(falloff_, distance / radius)});
            }
}

void BoxVolumeBrushKernel::sample(float x, float y, float z, float radius,
                                  std::vector<VolumeBrushPoint>& out) const {
    out.clear();
    radius = std::max(0.5f, radius);
    const int minX = static_cast<int>(std::ceil(x - radius));
    const int maxX = static_cast<int>(std::floor(x + radius));
    const int minY = static_cast<int>(std::ceil(y - radius));
    const int maxY = static_cast<int>(std::floor(y + radius));
    const int minZ = static_cast<int>(std::ceil(z - radius));
    const int maxZ = static_cast<int>(std::floor(z + radius));
    for (int iz = minZ; iz <= maxZ; ++iz)
        for (int iy = minY; iy <= maxY; ++iy)
            for (int ix = minX; ix <= maxX; ++ix) {
                const float distance = std::max({std::fabs(static_cast<float>(ix) - x),
                                                 std::fabs(static_cast<float>(iy) - y),
                                                 std::fabs(static_cast<float>(iz) - z)});
                out.push_back({ix, iy, iz, falloffWeight(falloff_, distance / radius)});
            }
}

PaintIntVolumeOperation::PaintIntVolumeOperation(int value) { setValue(value); }

void PaintIntVolumeOperation::setValue(int value) { value_ = std::clamp(value, 0, 255); }

std::unique_ptr<IEditCommand> PaintIntVolumeOperation::createCommand(
    IEditableTarget* target, const std::vector<VolumeBrushPoint>& points) const {
    if (!target || !target->query<IIntVolumeTarget>()) return nullptr;
    auto command = std::make_unique<IntVolumeEditCommand>("paint integer volume", target);
    for (const auto& point : points) {
        if (point.weight > 0.f) command->record(point.x, point.y, point.z, value_);
    }
    return command;
}

VolumeBrushTool::VolumeBrushTool(std::string id, std::string label)
    : descriptor_{std::move(id), std::move(label), {}} {}

void VolumeBrushTool::setRadius(float radius) { radius_ = std::max(0.5f, radius); }

bool VolumeBrushTool::stamp(EditorContext& context, float x, float y, float z) {
    if (!kernel_ || !operation_ || !context.target()) return false;
    kernel_->sample(x, y, z, radius_, points_);
    auto command = operation_->createCommand(context.target(), points_);
    return command && context.execute(std::move(command));
}

ToolResponse VolumeBrushTool::pointerEvent(EditorContext& context, const EditorPointerEvent& event) {
    if (event.phase == EditorPointerEvent::Phase::Down) {
        if (!kernel_ || !operation_ || !context.target() ||
            !context.transactions().begin(descriptor_.label)) return ToolResponse::ignored();
        stroking_ = true;
        stamp(context, event.x, event.y, event.z);
        return ToolResponse::capture();
    }
    if (!stroking_) return ToolResponse::ignored();
    if (event.phase == EditorPointerEvent::Phase::Move) {
        stamp(context, event.x, event.y, event.z);
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

void VolumeBrushTool::cancel(EditorContext& context) {
    if (stroking_) context.transactions().rollback();
    stroking_ = false;
}

void VolumeBrushTool::inspect(EditorContext&, IEditorInspector& inspector) {
    inspector.beginGroup(descriptor_.id, descriptor_.label);
    inspector.scalar("radius", "Radius", radius_, 0.5f, 256.f);
    inspector.endGroup();
    setRadius(radius_);
}

}  // namespace eve::editor
