#include "avatar_editor/AvatarDocumentEditor.h"

#include "editor/EditorProtocol.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace eve::avatar_editor {
namespace {

template <class T = void>
avatar_editing::EditorResult<T> editorError(avatar_editing::EditorStatus status, std::string rule,
                                            std::string message) {
    return eve::editing::failed<T>(status, avatar_editing::RuleId(std::move(rule)), std::move(message));
}

avatar_editing::EditorValue floats(float a, float b) { return avatar_editing::EditorValue::Array{a, b}; }
avatar_editing::EditorValue color(float r, float g, float b, float a) {
    return avatar_editing::EditorValue::Array{r, g, b, a};
}

avatar_editing::EditorValue layerValue(const char* id, const char* name, const char* texture, int z, bool visible,
                                       float ox, float oy, float w, float h, float r, float g, float bl, float a) {
    return avatar_editing::EditorValue::Object{{"id", std::string(id)},
                                               {"name", std::string(name)},
                                               {"texture", std::string(texture)},
                                               {"z", std::int64_t{z}},
                                               {"visible", visible},
                                               {"offset", floats(ox, oy)},
                                               {"size", floats(w, h)},
                                               {"color", color(r, g, bl, a)}};
}

}  // namespace

AvatarDocumentEditor::AvatarDocumentEditor(std::string targetId)
    : target_(std::move(targetId)), authority_(&target_), transactions_(&authority_) {
    seedPreviewDocument();
    auto previewed = refreshPreview();
    if (!previewed.ok())
        previewed.ignore("avatar editor keeps an empty composite when the seeded preview is rejected");
}

void AvatarDocumentEditor::seedPreviewDocument() {
    avatar_editing::EditorValue::Array layers;
    layers.push_back(layerValue("body", "body", "asset://preview/body.png", 0, true, 40.0f, 20.0f, 96.0f, 128.0f, 0.35f,
                                0.55f, 0.72f, 1.0f));
    layers.push_back(layerValue("eyes", "eyes", "asset://preview/eyes.png", 1, true, 62.0f, 78.0f, 52.0f, 22.0f, 0.95f,
                                0.92f, 0.88f, 1.0f));
    avatar_editing::EditorValue::Array parameters;
    parameters.push_back(avatar_editing::EditorValue::Object{{"id", std::string("smile")},
                                                             {"name", std::string("smile")},
                                                             {"default", 0.35},
                                                             {"minimum", 0.0},
                                                             {"maximum", 1.0},
                                                             {"value", 0.35}});
    avatar_editing::EditorValue::Object channels;
    channels["smile"] = 1.0;
    avatar_editing::EditorValue::Array expressions;
    expressions.push_back(avatar_editing::EditorValue::Object{
        {"id", std::string("happy")}, {"name", std::string("happy")}, {"channels", std::move(channels)}});
    avatar_editing::EditorValue::Object content;
    content["kind"]        = std::string("image");
    content["source"]      = std::string{};
    content["layers"]      = std::move(layers);
    content["parameters"]  = std::move(parameters);
    content["expressions"] = std::move(expressions);
    avatar_editing::EditorValue::Object root;
    root["schemaVersion"] = std::int64_t{1};
    root["content"]       = avatar_editing::EditorValue(std::move(content));
    auto loaded           = target_.loadSnapshot(avatar_editing::EditorValue(std::move(root)));
    if (!loaded.ok()) loaded.ignore("avatar editor keeps defaults when the preview snapshot is rejected");
}

avatar_editing::EditorResult<void> AvatarDocumentEditor::configureWorkspace(
    editor::EditorWorkspace& workspace) const {
    editor::EditorWorkspace candidate = workspace;
    struct Panel {
        const char* id;
        const char* title;
        const char* region;
        const char* context;
        int         order;
    };
    constexpr Panel panels[] = {
        {"avatar.layers", "Layers", "left", "list", 100},
        {"avatar.preview", "Avatar Preview", "center", "preview", 100},
        {"avatar.inspector", "Avatar Inspector", "right", "inspector", 100},
        {"avatar.parameters", "Parameters", "bottom", "parameters", 100},
        {"avatar.expressions", "Expressions", "right", "expressions", 200},
    };
    for (const auto& panel : panels) {
        if (!candidate.registerPanel(panel.id, panel.title, panel.region, panel.order) ||
            !candidate.setPanelCapability(panel.id, "avatar.document") ||
            !candidate.setPanelContext(panel.id, panel.context))
            return editorError(avatar_editing::EditorStatus::Rejected, "editor.avatar.workspace-conflict",
                               "Could not install the avatar workspace composition");
    }
    if (!candidate.activatePanel("avatar.preview"))
        return editorError(avatar_editing::EditorStatus::Rejected, "editor.avatar.workspace-activate",
                           "Could not activate the avatar preview panel");
    workspace = std::move(candidate);
    return eve::editing::applied<void>();
}

editor::SelectionSnapshot AvatarDocumentEditor::selection() const {
    editor::SelectionSnapshot snapshot;
    snapshot.channel = "avatar";
    editor::SelectionItem item;
    item.domain = editor::SelectionDomain::Asset;
    item.target = editor::TargetId(target_.targetId());
    item.item   = editor::StableId(selectedId_);
    item.type   = selectedType_;
    snapshot.items.push_back(item);
    snapshot.primary = item;
    return snapshot;
}

avatar_editing::EditorResult<void> AvatarDocumentEditor::commit(
    avatar_editing::EditorResult<avatar_editing::DomainOperation> operation, std::string label) {
    if (!operation.ok())
        return avatar_editing::EditorResult<void>::failure(operation.status());
    editor::TransactionSpec spec;
    spec.id           = editor::TransactionId("avatar.document.tx." + std::to_string(++txSequence_));
    spec.label        = std::move(label);
    spec.target       = editor::TargetId(target_.targetId());
    spec.baseRevision = target_.revision();
    auto begun        = transactions_.begin(std::move(spec));
    if (!begun.ok())
        return editorError(begun.code(), "editor.avatar.begin", "Could not begin the avatar transaction");
    auto appended = transactions_.append(std::move(operation).takeValue());
    if (!appended.ok()) {
        auto discarded = transactions_.discard();
        if (!discarded.ok()) discarded.ignore("pending avatar transaction already inactive");
        return avatar_editing::EditorResult<void>::failure(appended.status());
    }
    auto committed = transactions_.commit();
    if (!committed.ok())
        return avatar_editing::EditorResult<void>::failure(committed.status());
    return refreshPreview();
}

avatar_editing::EditorResult<void> AvatarDocumentEditor::refreshPreview() {
    std::vector<PreviewRect> next;
    auto                     layers = target_.layers();
    std::sort(layers.begin(), layers.end(), [](const auto& a, const auto& b) {
        return a.zIndex == b.zIndex ? a.id.value() < b.id.value() : a.zIndex < b.zIndex;
    });
    const float smile = smileAmount();
    for (const auto& layer : layers) {
        if (!layer.visible) continue;
        PreviewRect rect;
        rect.id       = layer.id.value();
        rect.name     = layer.name;
        rect.x        = layer.offset[0];
        rect.y        = layer.offset[1] + (layer.name == "eyes" ? smile * 10.0f : 0.0f);
        rect.w        = layer.size[0];
        rect.h        = layer.size[1];
        rect.r        = layer.color[0];
        rect.g        = layer.color[1];
        rect.b        = layer.color[2];
        rect.a        = layer.color[3];
        rect.selected = layer.id.value() == selectedId_;
        next.push_back(std::move(rect));
    }
    preview_          = std::move(next);
    previewRevision_  = target_.revision();
    return eve::editing::applied<void>();
}

float AvatarDocumentEditor::smileAmount() const {
    for (const auto& parameter : target_.parameters())
        if (parameter.name == "smile") return parameter.value;
    return 0.0f;
}

avatar_editing::EditorResult<void> AvatarDocumentEditor::selectLayer(std::string id) {
    if (id.empty())
        return editorError(avatar_editing::EditorStatus::Rejected, "editor.avatar.select-layer",
                           "Layer id must not be empty");
    bool found = false;
    for (const auto& layer : target_.layers())
        if (layer.id.value() == id) found = true;
    if (!found)
        return editorError(avatar_editing::EditorStatus::NotFound, "editor.avatar.layer", "Avatar layer was not found");
    selectedId_   = std::move(id);
    selectedType_ = "avatar.layer";
    return refreshPreview();
}

avatar_editing::EditorResult<void> AvatarDocumentEditor::selectParameter(std::string id) {
    if (id.empty())
        return editorError(avatar_editing::EditorStatus::Rejected, "editor.avatar.select-parameter",
                           "Parameter id must not be empty");
    bool found = false;
    for (const auto& parameter : target_.parameters())
        if (parameter.id.value() == id) found = true;
    if (!found)
        return editorError(avatar_editing::EditorStatus::NotFound, "editor.avatar.parameter",
                           "Avatar parameter was not found");
    selectedId_   = std::move(id);
    selectedType_ = "avatar.parameter";
    return refreshPreview();
}

avatar_editing::EditorResult<void> AvatarDocumentEditor::selectExpression(std::string id) {
    if (id.empty())
        return editorError(avatar_editing::EditorStatus::Rejected, "editor.avatar.select-expression",
                           "Expression id must not be empty");
    bool found = false;
    for (const auto& expression : target_.expressions())
        if (expression.id.value() == id) found = true;
    if (!found)
        return editorError(avatar_editing::EditorStatus::NotFound, "editor.avatar.expression",
                           "Avatar expression was not found");
    selectedId_   = std::move(id);
    selectedType_ = "avatar.expression";
    return refreshPreview();
}

avatar_editing::EditorResult<void> AvatarDocumentEditor::pointerDown(float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y))
        return editorError(avatar_editing::EditorStatus::Rejected, "editor.avatar.pointer",
                           "Pointer coordinates must be finite");
    for (int i = static_cast<int>(preview_.size()) - 1; i >= 0; --i) {
        const auto& rect = preview_[static_cast<std::size_t>(i)];
        if (x >= rect.x && y >= rect.y && x <= rect.x + rect.w && y <= rect.y + rect.h)
            return selectLayer(rect.id);
    }
    return eve::editing::applied<void>();
}

avatar_editing::EditorResult<void> AvatarDocumentEditor::setLayerVisible(bool visible) {
    if (selectedType_ != "avatar.layer")
        return editorError(avatar_editing::EditorStatus::Rejected, "editor.avatar.selection",
                           "A layer must be selected to change visibility");
    return commit(target_.makeSet(selection(), avatar_editing::PropertyPath("layer.visible"),
                                  avatar_editing::EditorValue(visible), avatar_editing::PropertySetMode::Absolute),
                  "Set layer visibility");
}

avatar_editing::EditorResult<void> AvatarDocumentEditor::setLayerZ(int zIndex) {
    if (selectedType_ != "avatar.layer")
        return editorError(avatar_editing::EditorStatus::Rejected, "editor.avatar.selection",
                           "A layer must be selected to change z-order");
    return commit(target_.makeSet(selection(), avatar_editing::PropertyPath("layer.z"),
                                  avatar_editing::EditorValue(std::int64_t{zIndex}),
                                  avatar_editing::PropertySetMode::Absolute),
                  "Set layer z");
}

avatar_editing::EditorResult<void> AvatarDocumentEditor::setParameterValue(double value) {
    if (selectedType_ != "avatar.parameter")
        return editorError(avatar_editing::EditorStatus::Rejected, "editor.avatar.selection",
                           "A parameter must be selected to change its value");
    return commit(target_.makeSet(selection(), avatar_editing::PropertyPath("parameter.value"),
                                  avatar_editing::EditorValue(value), avatar_editing::PropertySetMode::Absolute),
                  "Set parameter value");
}

avatar_editing::EditorResult<void> AvatarDocumentEditor::createLayer(std::string id, std::string name) {
    avatar_editing::AvatarLayerValue layer;
    layer.id           = avatar_editing::ObjectId(std::move(id));
    layer.name         = std::move(name);
    layer.textureAsset = "asset://preview/" + layer.name + ".png";
    layer.zIndex       = static_cast<int>(target_.layers().size());
    layer.offset       = {16.0f, 16.0f};
    layer.size         = {48.0f, 48.0f};
    auto created       = commit(target_.makeCreateLayer(layer), "Create layer");
    if (!created.ok()) return created;
    return selectLayer(layer.id.value());
}

avatar_editing::EditorResult<void> AvatarDocumentEditor::deleteSelectedLayer() {
    if (selectedType_ != "avatar.layer")
        return editorError(avatar_editing::EditorStatus::Rejected, "editor.avatar.selection",
                           "A layer must be selected to delete it");
    const auto previous = preview_;
    const auto revision = previewRevision_;
    auto       deleted  = commit(target_.makeDeleteLayer(avatar_editing::ObjectId(selectedId_)), "Delete layer");
    if (!deleted.ok()) {
        preview_         = previous;
        previewRevision_ = revision;
        return deleted;
    }
    if (!target_.layers().empty()) return selectLayer(target_.layers().front().id.value());
    selectedId_.clear();
    return eve::editing::applied<void>();
}

avatar_editing::EditorResult<void> AvatarDocumentEditor::createParameter(std::string id, std::string name) {
    avatar_editing::AvatarParameterValue parameter;
    parameter.id   = avatar_editing::ObjectId(std::move(id));
    parameter.name = std::move(name);
    auto created   = commit(target_.makeCreateParameter(parameter), "Create parameter");
    if (!created.ok()) return created;
    return selectParameter(parameter.id.value());
}

avatar_editing::EditorResult<void> AvatarDocumentEditor::deleteSelectedParameter() {
    if (selectedType_ != "avatar.parameter")
        return editorError(avatar_editing::EditorStatus::Rejected, "editor.avatar.selection",
                           "A parameter must be selected to delete it");
    const auto previous = preview_;
    const auto revision = previewRevision_;
    auto       deleted =
        commit(target_.makeDeleteParameter(avatar_editing::ObjectId(selectedId_)), "Delete parameter");
    if (!deleted.ok()) {
        preview_         = previous;
        previewRevision_ = revision;
        return deleted;
    }
    if (!target_.parameters().empty()) return selectParameter(target_.parameters().front().id.value());
    selectedId_.clear();
    return eve::editing::applied<void>();
}

avatar_editing::EditorResult<void> AvatarDocumentEditor::createExpression(std::string id, std::string name) {
    avatar_editing::AvatarExpressionValue expression;
    expression.id   = avatar_editing::ObjectId(std::move(id));
    expression.name = std::move(name);
    auto created    = commit(target_.makeCreateExpression(expression), "Create expression");
    if (!created.ok()) return created;
    return selectExpression(expression.id.value());
}

avatar_editing::EditorResult<void> AvatarDocumentEditor::deleteSelectedExpression() {
    if (selectedType_ != "avatar.expression")
        return editorError(avatar_editing::EditorStatus::Rejected, "editor.avatar.selection",
                           "An expression must be selected to delete it");
    auto deleted = commit(target_.makeDeleteExpression(avatar_editing::ObjectId(selectedId_)), "Delete expression");
    if (!deleted.ok()) return deleted;
    if (!target_.expressions().empty()) return selectExpression(target_.expressions().front().id.value());
    selectedId_.clear();
    return eve::editing::applied<void>();
}

avatar_editing::EditorResult<editor::TransactionReceipt> AvatarDocumentEditor::undo() {
    auto result = transactions_.undo();
    if (!result.ok()) return result;
    auto previewed = refreshPreview();
    if (!previewed.ok())
        return avatar_editing::EditorResult<editor::TransactionReceipt>::failure(previewed.status());
    return result;
}

avatar_editing::EditorResult<editor::TransactionReceipt> AvatarDocumentEditor::redo() {
    auto result = transactions_.redo();
    if (!result.ok()) return result;
    auto previewed = refreshPreview();
    if (!previewed.ok())
        return avatar_editing::EditorResult<editor::TransactionReceipt>::failure(previewed.status());
    return result;
}

std::string AvatarDocumentEditor::layerId(int index) const {
    const auto* layer = layerAt(index);
    return layer ? layer->id.value() : std::string{};
}
std::string AvatarDocumentEditor::layerName(int index) const {
    const auto* layer = layerAt(index);
    return layer ? layer->name : std::string{};
}
bool AvatarDocumentEditor::layerVisible(int index) const {
    const auto* layer = layerAt(index);
    return layer && layer->visible;
}
int AvatarDocumentEditor::layerZ(int index) const {
    const auto* layer = layerAt(index);
    return layer ? layer->zIndex : 0;
}
bool AvatarDocumentEditor::isLayerSelected(int index) const {
    const auto* layer = layerAt(index);
    return layer && selectedType_ == "avatar.layer" && layer->id.value() == selectedId_;
}

float AvatarDocumentEditor::previewX(int index) const {
    const auto* rect = previewAt(index);
    return rect ? rect->x : 0.0f;
}
float AvatarDocumentEditor::previewY(int index) const {
    const auto* rect = previewAt(index);
    return rect ? rect->y : 0.0f;
}
float AvatarDocumentEditor::previewW(int index) const {
    const auto* rect = previewAt(index);
    return rect ? rect->w : 0.0f;
}
float AvatarDocumentEditor::previewH(int index) const {
    const auto* rect = previewAt(index);
    return rect ? rect->h : 0.0f;
}
float AvatarDocumentEditor::previewR(int index) const {
    const auto* rect = previewAt(index);
    return rect ? rect->r : 0.0f;
}
float AvatarDocumentEditor::previewG(int index) const {
    const auto* rect = previewAt(index);
    return rect ? rect->g : 0.0f;
}
float AvatarDocumentEditor::previewB(int index) const {
    const auto* rect = previewAt(index);
    return rect ? rect->b : 0.0f;
}
float AvatarDocumentEditor::previewA(int index) const {
    const auto* rect = previewAt(index);
    return rect ? rect->a : 0.0f;
}
bool AvatarDocumentEditor::isPreviewSelected(int index) const {
    const auto* rect = previewAt(index);
    return rect && rect->selected;
}
std::string AvatarDocumentEditor::previewName(int index) const {
    const auto* rect = previewAt(index);
    return rect ? rect->name : std::string{};
}

std::string AvatarDocumentEditor::parameterId(int index) const {
    const auto* parameter = parameterAt(index);
    return parameter ? parameter->id.value() : std::string{};
}
std::string AvatarDocumentEditor::parameterName(int index) const {
    const auto* parameter = parameterAt(index);
    return parameter ? parameter->name : std::string{};
}
float AvatarDocumentEditor::parameterValue(int index) const {
    const auto* parameter = parameterAt(index);
    return parameter ? parameter->value : 0.0f;
}
float AvatarDocumentEditor::parameterMinimum(int index) const {
    const auto* parameter = parameterAt(index);
    return parameter ? parameter->minimum : 0.0f;
}
float AvatarDocumentEditor::parameterMaximum(int index) const {
    const auto* parameter = parameterAt(index);
    return parameter ? parameter->maximum : 1.0f;
}
bool AvatarDocumentEditor::isParameterSelected(int index) const {
    const auto* parameter = parameterAt(index);
    return parameter && selectedType_ == "avatar.parameter" && parameter->id.value() == selectedId_;
}

std::string AvatarDocumentEditor::expressionId(int index) const {
    const auto* expression = expressionAt(index);
    return expression ? expression->id.value() : std::string{};
}
std::string AvatarDocumentEditor::expressionName(int index) const {
    const auto* expression = expressionAt(index);
    return expression ? expression->name : std::string{};
}
bool AvatarDocumentEditor::isExpressionSelected(int index) const {
    const auto* expression = expressionAt(index);
    return expression && selectedType_ == "avatar.expression" && expression->id.value() == selectedId_;
}
int AvatarDocumentEditor::expressionChannelCount(int index) const {
    const auto* expression = expressionAt(index);
    return expression ? static_cast<int>(expression->channels.size()) : 0;
}
std::string AvatarDocumentEditor::expressionChannelName(int expression, int channel) const {
    const auto* value = expressionAt(expression);
    if (!value || channel < 0 || static_cast<std::size_t>(channel) >= value->channels.size()) return {};
    auto it = value->channels.begin();
    for (int i = 0; i < channel; ++i) ++it;
    return it->first;
}

const AvatarDocumentEditor::PreviewRect* AvatarDocumentEditor::previewAt(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= preview_.size()) return nullptr;
    return &preview_[static_cast<std::size_t>(index)];
}
const avatar_editing::AvatarLayerValue* AvatarDocumentEditor::layerAt(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= target_.layers().size()) return nullptr;
    return &target_.layers()[static_cast<std::size_t>(index)];
}
const avatar_editing::AvatarParameterValue* AvatarDocumentEditor::parameterAt(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= target_.parameters().size()) return nullptr;
    return &target_.parameters()[static_cast<std::size_t>(index)];
}
const avatar_editing::AvatarExpressionValue* AvatarDocumentEditor::expressionAt(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= target_.expressions().size()) return nullptr;
    return &target_.expressions()[static_cast<std::size_t>(index)];
}

}  // namespace eve::avatar_editor
