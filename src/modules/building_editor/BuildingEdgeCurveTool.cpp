#include "building_editor/EditorBuildingTarget.h"

#include "editor/EditorSession.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <span>

namespace eve::editor {
namespace {

editing::Result<void> toolFailure(editing::Status status, const char* rule,
                                  std::string message) {
    return editing::failed<void>(status, editing::RuleId(rule), std::move(message));
}

OverlayStyle styleFor(const editing::GizmoPrimitive& primitive) {
    const auto channel = [&](size_t index) {
        return static_cast<unsigned int>(std::clamp(primitive.color[index], 0.0, 1.0) *
                                         255.0 + 0.5);
    };
    const unsigned int color = (channel(3) << 24U) | (channel(0) << 16U) |
                               (channel(1) << 8U) | channel(2);
    return {color, primitive.dashed ? 1.f : 2.f, false};
}

}  // namespace

BuildingEdgeCurveTool::BuildingEdgeCurveTool(IBuildingViewportAdapter* viewport,
                                             editing::IEditAuthority* authority)
    : viewport_(viewport), authority_(authority) {}

void BuildingEdgeCurveTool::setViewportAdapter(IBuildingViewportAdapter* viewport) {
    if (drag_) drag_->cancelDrag();
    drag_.reset();
    viewport_ = viewport;
}

void BuildingEdgeCurveTool::setAuthority(editing::IEditAuthority* authority) {
    if (drag_) drag_->cancelDrag();
    drag_.reset();
    authority_ = authority;
}

editing::Result<void> BuildingEdgeCurveTool::setSelection(
    BuildingEdgeCurveToolSelection selection) {
    if (isDragging())
        return toolFailure(editing::Status::Conflict,
                           "editor.building.curve-tool-gesture-active",
                           "Cannot replace curve selection during an active drag");
    if (selection.buildingId.empty() || selection.memberInstanceId <= 0 ||
        selection.controlPoints.size() != 4 || selection.subdivisions <= 0)
        return toolFailure(editing::Status::Rejected,
                           "editor.building.curve-tool-selection-invalid",
                           "Curve selection requires a building, member, four controls and positive subdivisions");
    selection_ = std::move(selection);
    preview_.reset();
    lastReceipt_.reset();
    diagnostics_.clear();
    lastStatus_ = editing::Status::Applied;
    return editing::applied<void>();
}

void BuildingEdgeCurveTool::clearSelection() {
    if (drag_) drag_->cancelDrag();
    drag_.reset();
    selection_.reset();
    preview_.reset();
    diagnostics_.clear();
    lastStatus_ = editing::Status::NotFound;
}

bool BuildingEdgeCurveTool::isDragging() const { return drag_ && drag_->isDragging(); }

void BuildingEdgeCurveTool::recordFailure(
    const editing::Status& status,
    const std::vector<editing::Diagnostic>& diagnostics) {
    lastStatus_ = status;
    diagnostics_ = diagnostics;
}

void BuildingEdgeCurveTool::recordPreview(
    building_editing::BuildingEdgeCurveDragPreview preview) {
    lastStatus_ = preview.status;
    diagnostics_ = preview.diagnostics;
    preview_ = std::move(preview);
}

ToolResponse BuildingEdgeCurveTool::begin(EditorContext& context,
                                          const EditorPointerEvent& event) {
    auto* target = dynamic_cast<BuildingPlacementTarget*>(context.target());
    if (!target || !selection_ || !viewport_) return ToolResponse::ignored();
    auto ray = viewport_->pointerRay(event);
    if (!ray.ok()) {
        recordFailure(ray.code(), ray.diagnostics());
        return ToolResponse::ignored();
    }
    drag_ = std::make_unique<building_editing::BuildingEdgeCurveDragSession>(
        target, selection_->buildingId, selection_->memberInstanceId,
        selection_->controlPoints, selection_->subdivisions, selection_->surfaceName);
    const BuildingViewportRay& value = ray.value();
    auto begun = drag_->beginDrag(value.origin[0], value.origin[1], value.origin[2],
                                  value.direction[0], value.direction[1],
                                  value.direction[2]);
    if (!begun.ok()) {
        recordFailure(begun.code(), begun.diagnostics());
        drag_.reset();
        return ToolResponse::ignored();
    }
    recordPreview(std::move(begun.value()));
    return ToolResponse::capture();
}

ToolResponse BuildingEdgeCurveTool::move(const EditorPointerEvent& event) {
    if (!isDragging()) return ToolResponse::ignored();
    if (!viewport_) {
        auto failure = editing::failed<BuildingViewportRay>(
            editing::Status::Failed,
            editing::RuleId("editor.building.curve-tool-viewport-missing"),
            "Curve tool viewport adapter is unavailable");
        recordFailure(failure.code(), failure.diagnostics());
        return ToolResponse::consumed();
    }
    auto ray = viewport_->pointerRay(event);
    if (!ray.ok()) {
        recordFailure(ray.code(), ray.diagnostics());
        return ToolResponse::consumed();
    }
    const BuildingViewportRay& value = ray.value();
    auto moved = drag_->updateDrag(value.origin[0], value.origin[1], value.origin[2],
                                   value.direction[0], value.direction[1],
                                   value.direction[2]);
    if (!moved.ok()) {
        recordFailure(moved.code(), moved.diagnostics());
        if (!drag_->isDragging()) {
            drag_.reset();
            return ToolResponse::release();
        }
        return ToolResponse::consumed();
    }
    recordPreview(std::move(moved.value()));
    return ToolResponse::consumed();
}

ToolResponse BuildingEdgeCurveTool::finish(EditorContext& context,
                                           const EditorPointerEvent& event) {
    if (!isDragging()) return ToolResponse::ignored();
    (void)move(event);
    if (!isDragging()) return ToolResponse::release();
    auto operation = drag_->finishDrag();
    drag_.reset();
    if (!operation.ok()) {
        recordFailure(operation.code(), operation.diagnostics());
        return ToolResponse::release();
    }
    auto* target = dynamic_cast<BuildingPlacementTarget*>(context.target());
    if (!target || !authority_) {
        auto failure = toolFailure(editing::Status::Failed,
                                   "editor.building.curve-tool-authority-missing",
                                   "Curve commit requires its selected target and authority");
        recordFailure(failure.code(), failure.diagnostics());
        return ToolResponse::release();
    }
    editing::TransactionSpec transaction;
    transaction.id = editing::TransactionId(
        descriptor_.id + "." + std::to_string(++transactionSequence_));
    transaction.label = descriptor_.label;
    transaction.origin = editing::ActionOrigin::User;
    transaction.target = target->targetId();
    transaction.baseRevision = target->revision();
    transaction.mergeKey = "building.edge-curve.handle";
    const std::array<editing::DomainOperation, 1> operations{std::move(operation.value())};
    auto plan = authority_->preflight(transaction, std::span<const editing::DomainOperation>(operations));
    if (!plan.ok()) {
        recordFailure(plan.code(), plan.diagnostics());
        return ToolResponse::release();
    }
    auto receipt = authority_->commit(plan.value());
    if (!receipt.ok()) {
        recordFailure(receipt.code(), receipt.diagnostics());
        return ToolResponse::release();
    }
    lastStatus_ = editing::Status::Applied;
    diagnostics_ = receipt.value().diagnostics;
    lastReceipt_ = std::move(receipt.value());
    if (selection_ && preview_) {
        selection_->controlPoints = preview_->curve.controlPoints;
        if (!lastReceipt_->affectedObjects.empty()) {
            const std::string& value = lastReceipt_->affectedObjects.front().object.value();
            int member = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), member);
            if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() &&
                member > 0)
                selection_->memberInstanceId = member;
        }
    }
    return ToolResponse::release();
}

ToolResponse BuildingEdgeCurveTool::pointerEvent(EditorContext& context,
                                                 const EditorPointerEvent& event) {
    if (event.phase == EditorPointerEvent::Phase::Down)
        return event.button == 0 ? begin(context, event) : ToolResponse::ignored();
    if (event.phase == EditorPointerEvent::Phase::Move) return move(event);
    if (event.phase == EditorPointerEvent::Phase::Up) return finish(context, event);
    if (!isDragging()) return ToolResponse::ignored();
    cancel(context);
    return ToolResponse::release();
}

void BuildingEdgeCurveTool::deactivate(EditorContext& context) { cancel(context); }

void BuildingEdgeCurveTool::cancel(EditorContext&) {
    if (drag_) drag_->cancelDrag();
    drag_.reset();
}

void BuildingEdgeCurveTool::drawOverlay(EditorContext&, IEditorOverlay& overlay) {
    if (!preview_ || !viewport_) return;
    for (const editing::GizmoPrimitive& primitive : preview_->gizmo.primitives) {
        const OverlayStyle style = styleFor(primitive);
        if (primitive.kind == "sphere") {
            auto center = viewport_->projectWorld(primitive.position);
            if (center.ok()) overlay.circle(center.value(), 7.f, style);
            continue;
        }
        if (primitive.kind != "line") continue;
        std::array<double, 3> from = primitive.position;
        std::array<double, 3> to = primitive.position;
        for (size_t axis = 0; axis < 3; ++axis) {
            const double half = primitive.direction[axis] * primitive.length * 0.5;
            from[axis] -= half;
            to[axis] += half;
        }
        auto projectedFrom = viewport_->projectWorld(from);
        auto projectedTo = viewport_->projectWorld(to);
        if (projectedFrom.ok() && projectedTo.ok())
            overlay.line(projectedFrom.value(), projectedTo.value(), style);
    }
}

}  // namespace eve::editor
