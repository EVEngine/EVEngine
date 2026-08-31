#pragma once
#include "editor/EditorProperty.h"
#include "editor/EditorProtocol.h"
#include "ui_editing/UiDocument.h"
namespace eve::editor {
using UiLayoutValue = ui_editing::UiLayoutValue;
using UiStyleValue = ui_editing::UiStyleValue;
using UiContentValue = ui_editing::UiContentValue;
using UiWidgetSnapshot = ui_editing::UiWidgetSnapshot;
using CreateUiWidgetRequest = ui_editing::CreateUiWidgetRequest;
using IUiDocumentEditTarget = ui_editing::IUiDocumentEditTarget;
using UiDocumentTarget = ui_editing::UiDocumentTarget;
using UiPreviewWidget = ui_editing::UiPreviewWidget;
using UiPreviewSnapshot = ui_editing::UiPreviewSnapshot;
using UiDocumentPreviewService = ui_editing::UiDocumentPreviewService;
using UiDocumentRuntimeBridge = ui_editing::UiDocumentRuntimeBridge;
}  // namespace eve::editor
