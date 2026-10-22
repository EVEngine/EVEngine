#include "graphics/material/editor/MaterialEditor.h"

#include <stdexcept>
#include <utility>
#include <vector>

namespace eve::material_editor {
using namespace eve::editing;
using editing::Result;
using editing::Status;
using editing::TransactionId;
using editing::Value;

MaterialEditor::MaterialEditor(std::string targetId)
    : target_(std::move(targetId)), authority_(&target_), transactions_(&authority_) {
    if (target_.targetId().value().empty()) throw std::invalid_argument("Material target id is required");
}

SelectionSnapshot MaterialEditor::selection() const {
    SelectionSnapshot result;
    result.channel = "asset";
    result.items.push_back(
        {SelectionDomain::Asset, target_.targetId(), StableId(target_.targetId().value()), "graphics.material"});
    result.primary = result.items.front();
    return result;
}

Result<void> MaterialEditor::configureWorkspace(editor::EditorWorkspace& workspace) const {
    auto candidate = workspace;
    struct Panel {
        const char* id;
        const char* title;
        const char* region;
    };
    constexpr Panel panels[] = {{"toolbar", "Material Studio", "top"},
                                {"preview", "Material Preview", "center"},
                                {"inspector", "Material Properties", "right"}};
    for (const auto& panel : panels) {
        editor::WorkspacePanelDescriptor descriptor;
        descriptor.id         = panel.id;
        descriptor.title      = panel.title;
        descriptor.region     = panel.region;
        descriptor.capability = std::string_view(panel.id) == "preview" ? "scene.viewport.3d" : "property.material";
        auto registered       = candidate.registerPanel(std::move(descriptor));
        if (!registered.ok()) return Result<void>::failure(registered.status());
    }
    auto activated = candidate.activatePanel(StableId("preview"));
    if (!activated.ok()) return Result<void>::failure(activated.status());
    workspace = std::move(candidate);
    return applied<void>();
}

Result<void> MaterialEditor::setProperty(std::string_view path, const Value& value) {
    return setProperties({{std::string(path), value}});
}

Result<void> MaterialEditor::setProperties(const Value::Object& properties) {
    auto                         candidate = target_;
    std::vector<DomainOperation> operations;
    const auto                   metadata = target_.schema(selection());
    for (const auto& [path, input] : properties) {
        auto       value      = input;
        const auto descriptor = metadata.find(PropertyPath(path));
        if (descriptor && descriptor->type == PropertyType::Float) {
            if (const auto* integer = value.getIf<std::int64_t>()) value = static_cast<double>(*integer);
        }
        if (descriptor && (descriptor->type == PropertyType::Color || descriptor->type == PropertyType::Vec2 ||
                           descriptor->type == PropertyType::Vec3 || descriptor->type == PropertyType::Vec4)) {
            if (auto* components = value.getIf<Value::Array>()) {
                for (auto& component : *components)
                    if (const auto* integer = component.getIf<std::int64_t>())
                        component = static_cast<double>(*integer);
            }
        }
        auto current = target_.read(selection(), PropertyPath(path));
        if (current.state == PropertyReadState::Value && current.value == value) continue;
        auto operation = candidate.makeSet(selection(), PropertyPath(path), value, PropertySetMode::Absolute);
        if (!operation.ok()) return Result<void>::failure(operation.status());
        auto staged = candidate.applyDomainOperation(operation.value());
        if (!staged.ok()) return staged;
        operations.push_back(std::move(operation).takeValue());
    }
    const auto diagnostics = candidate.validate();
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity() == DiagnosticSeverity::Error)
            return Result<void>::failure(eve::Status(Status::Rejected, diagnostics));
    }
    if (operations.empty()) return noOp(diagnostics);
    editor::TransactionSpec spec;
    spec.id           = TransactionId("material.tx." + std::to_string(++sequence_));
    spec.label        = "Edit material";
    spec.target       = target_.targetId();
    spec.baseRevision = target_.revision();
    auto begun        = transactions_.begin(std::move(spec));
    if (!begun.ok()) return Result<void>::failure(begun.status());
    for (auto& operation : operations) {
        auto appended = transactions_.append(std::move(operation));
        if (!appended.ok()) {
            auto discarded = transactions_.discard();
            if (!discarded.ok()) return discarded;
            return appended;
        }
    }
    auto committed = transactions_.commit();
    if (!committed.ok()) {
        auto discarded = transactions_.discard();
        if (!discarded.ok()) return discarded;
        return Result<void>::failure(committed.status());
    }
    return applied<void>(diagnostics);
}

Result<Value> MaterialEditor::read(std::string_view path) const {
    auto property = target_.read(selection(), PropertyPath(std::string(path)));
    if (property.state != PropertyReadState::Value)
        return failed<Value>(Status::NotFound, RuleId("editor.material.property"),
                             "Unknown material property: " + std::string(path));
    return applied<Value>(std::move(property.value));
}

Value MaterialEditor::schema() const {
    const auto   metadata = target_.schema(selection());
    Value::Array properties;
    for (const auto& field : metadata.properties) {
        Value::Object item{{"path", field.path.value()},
                           {"label", field.displayNameKey},
                           {"category", field.category},
                           {"type", static_cast<std::int64_t>(field.type)},
                           {"defaultValue", field.defaultValue}};
        Value::Array  choices;
        for (const auto& choice : field.enumItems) choices.emplace_back(choice);
        item["choices"] = std::move(choices);
        if (field.numeric.minimum) item["minimum"] = *field.numeric.minimum;
        if (field.numeric.maximum) item["maximum"] = *field.numeric.maximum;
        properties.emplace_back(std::move(item));
    }
    return Value::Object{{"typeId", metadata.typeId},
                         {"version", static_cast<std::int64_t>(metadata.version)},
                         {"properties", std::move(properties)}};
}

Value MaterialEditor::snapshot() const { return target_.snapshotValue(); }

Result<void> MaterialEditor::restore(const Value& snapshot) {
    auto candidate = target_;
    auto loaded    = candidate.loadSnapshot(snapshot);
    if (!loaded.ok()) return loaded;
    const auto normalized = candidate.snapshotValue();
    return setProperties(*normalized.getIf<Value::Object>()->at("properties").getIf<Value::Object>());
}

Result<editor::TransactionReceipt> MaterialEditor::undo() { return transactions_.undo(); }
Result<editor::TransactionReceipt> MaterialEditor::redo() { return transactions_.redo(); }

}  // namespace eve::material_editor
