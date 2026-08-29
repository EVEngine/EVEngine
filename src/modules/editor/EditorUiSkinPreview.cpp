#include "editor/EditorUiSkinPreview.h"

#include <algorithm>
#include <cmath>

namespace eve::editor {
namespace {
double aligned(double start, double available, double extent, const std::string& alignment) {
    if (alignment == "center") return start + (available - extent) * 0.5;
    if (alignment == "end") return start + available - extent;
    return start;
}

void intersect(double& x, double& y, double& width, double& height,
               double otherX, double otherY, double otherWidth, double otherHeight) {
    const double right = std::min(x + width, otherX + otherWidth);
    const double bottom = std::min(y + height, otherY + otherHeight);
    x = std::max(x, otherX); y = std::max(y, otherY);
    width = std::max(0.0, right - x); height = std::max(0.0, bottom - y);
}
}  // namespace

UiSkinDrawPlan UiSkinPreviewPlanner::build(const UiDocumentTarget& document,
                                            const UiPreviewSnapshot& preview,
                                            const IUiSkinAssetResolver& assets) const {
    UiSkinDrawPlan result;
    result.documentRevision = document.revision();
    if (preview.status != EditorStatus::Applied || preview.documentRevision != document.revision()) {
        result.status = EditorStatus::Conflict;
        result.diagnostics.push_back({RuleId("editor.ui.skin-stale-preview"), DiagnosticSeverity::Error,
                                      "UI skin planning requires a current layout preview"});
        return result;
    }
    std::map<ObjectId, UiPreviewWidget> rectangles;
    for (const auto& rectangle : preview.widgets) rectangles.emplace(rectangle.id, rectangle);
    for (const auto& rectangle : preview.widgets) {
        if (!rectangle.visible) continue;
        auto widgetResult = document.widget(rectangle.id);
        if (!widgetResult.value) { result.status = EditorStatus::Conflict; return result; }
        const UiWidgetSnapshot& widget = *widgetResult.value;
        double contentX = rectangle.x + widget.style.paddingLeft;
        double contentY = rectangle.y + widget.style.paddingTop;
        double contentWidth = std::max(0.0, rectangle.width - widget.style.paddingLeft - widget.style.paddingRight);
        double contentHeight = std::max(0.0, rectangle.height - widget.style.paddingTop - widget.style.paddingBottom);
        double clipX = contentX, clipY = contentY, clipWidth = contentWidth, clipHeight = contentHeight;
        ObjectId ancestor = widget.parent;
        while (widget.content.clip && !ancestor.empty()) {
            auto parentWidget = document.widget(ancestor);
            auto parentRect = rectangles.find(ancestor);
            if (!parentWidget.value || parentRect == rectangles.end()) break;
            intersect(clipX, clipY, clipWidth, clipHeight, parentRect->second.x, parentRect->second.y,
                      parentRect->second.width, parentRect->second.height);
            ancestor = parentWidget.value->parent;
        }
        if (!widget.content.textureAsset.empty()) {
            auto metadata = assets.texture(widget.content.textureAsset);
            if (!metadata.value || metadata.value->width <= 0.0 || metadata.value->height <= 0.0) {
                result.diagnostics.push_back({RuleId("editor.ui.texture-unresolved"), DiagnosticSeverity::Error,
                    "Could not resolve UI texture: " + widget.content.textureAsset});
            } else {
                UiSkinDrawCommand command; command.kind = UiSkinDrawKind::Image; command.widget = widget.id;
                command.asset = widget.content.textureAsset; command.x = contentX; command.y = contentY;
                command.width = contentWidth; command.height = contentHeight;
                command.clipX = clipX; command.clipY = clipY; command.clipWidth = clipWidth; command.clipHeight = clipHeight;
                const double sourceAspect = metadata.value->width / metadata.value->height;
                const double targetAspect = contentHeight > 0.0 ? contentWidth / contentHeight : sourceAspect;
                if (widget.content.imageFit == "contain") {
                    if (sourceAspect > targetAspect) command.height = contentWidth / sourceAspect;
                    else command.width = contentHeight * sourceAspect;
                    command.x = aligned(contentX, contentWidth, command.width, widget.content.horizontalAlign);
                    command.y = aligned(contentY, contentHeight, command.height, widget.content.verticalAlign);
                } else if (widget.content.imageFit == "cover") {
                    if (sourceAspect > targetAspect) {
                        const double visible = targetAspect / sourceAspect;
                        command.u0 = (1.0 - visible) * 0.5; command.u1 = 1.0 - command.u0;
                    } else {
                        const double visible = sourceAspect / targetAspect;
                        command.v0 = (1.0 - visible) * 0.5; command.v1 = 1.0 - command.v0;
                    }
                }
                result.commands.push_back(std::move(command));
            }
        }
        if (!widget.text.empty()) {
            auto font = assets.font(widget.content.fontAsset);
            if (widget.content.fontAsset.empty() || !font.accepted()) {
                result.diagnostics.push_back({RuleId("editor.ui.font-unresolved"), DiagnosticSeverity::Error,
                    "Could not resolve UI font: " + widget.content.fontAsset});
            } else {
                UiSkinDrawCommand command; command.kind = UiSkinDrawKind::Text; command.widget = widget.id;
                command.asset = widget.content.fontAsset; command.text = widget.text;
                command.x = contentX; command.y = aligned(contentY, contentHeight, widget.content.fontSize,
                                                            widget.content.verticalAlign);
                command.width = contentWidth; command.height = widget.content.fontSize;
                command.clipX = clipX; command.clipY = clipY; command.clipWidth = clipWidth; command.clipHeight = clipHeight;
                command.r = widget.content.textR; command.g = widget.content.textG;
                command.b = widget.content.textB; command.a = widget.content.textA;
                command.fontSize = widget.content.fontSize;
                result.commands.push_back(std::move(command));
            }
        }
    }
    result.status = std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [](const EditorDiagnostic& diagnostic) { return diagnostic.severity == DiagnosticSeverity::Error; })
        ? EditorStatus::Rejected : EditorStatus::Applied;
    return result;
}

}  // namespace eve::editor
