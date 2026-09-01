#pragma once

#include "ui_editor/EditorUiDocumentTarget.h"

#include <map>

namespace eve::editor {

/** @brief Dimensions known for an editor-resolved UI texture. */
struct UiTextureMetadata {
    double width = 0.0;
    double height = 0.0;
};

/** @brief Asset metadata boundary used by renderer-neutral UI preview planning. */
class IUiSkinAssetResolver {
public:
    virtual ~IUiSkinAssetResolver() = default;
    /** @brief Resolve texture dimensions, returning NotFound for missing assets. */
    virtual EditorResult<UiTextureMetadata> texture(const std::string& asset) const = 0;
    /** @brief Report whether a font asset can be resolved. */
    virtual EditorResult<void> font(const std::string& asset) const = 0;
};

enum class UiSkinDrawKind { Image, Text };

/** @brief One deterministic text or image command, including effective clipping. */
struct UiSkinDrawCommand {
    UiSkinDrawKind kind = UiSkinDrawKind::Image;
    ObjectId widget;
    std::string asset;
    std::string text;
    double x = 0.0, y = 0.0, width = 0.0, height = 0.0;
    double clipX = 0.0, clipY = 0.0, clipWidth = 0.0, clipHeight = 0.0;
    double u0 = 0.0, v0 = 0.0, u1 = 1.0, v1 = 1.0;
    double r = 1.0, g = 1.0, b = 1.0, a = 1.0;
    double fontSize = 16.0;
};

/** @brief Revision-bound content plan suitable for graphics or UI-host presenters. */
struct UiSkinDrawPlan {
    EditorStatus status = EditorStatus::Failed;
    Revision documentRevision = 0;
    std::vector<UiSkinDrawCommand> commands;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Builds deterministic text/texture commands without touching runtime graphics. */
class UiSkinPreviewPlanner {
public:
    /** @brief Resolve assets and fit/clamp widget content against a layout preview. */
    UiSkinDrawPlan build(const UiDocumentTarget& document, const UiPreviewSnapshot& preview,
                         const IUiSkinAssetResolver& assets) const;
};

/** @brief Narrow presentation boundary consuming a renderer-neutral UI skin plan. */
class IUiSkinPlanRenderer {
public:
    virtual ~IUiSkinPlanRenderer() = default;
    /** @brief Draw all commands in document order into the active target. */
    virtual EditorResult<void> render(const UiSkinDrawPlan& plan) = 0;
};

}  // namespace eve::editor
