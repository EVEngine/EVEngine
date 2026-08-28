#include "editor/EditorDialogueGraph.h"

#include "dialogue/ConversationAuthoring.h"

#include <memory>
#include <utility>

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> runtimeError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

}  // namespace

EditorResult<dialogue::ConversationDocument*> DialogueGraphRuntimeBuilder::build(
    const GraphDocumentData& graph) const {
    DialogueGraphDomain domain;
    const DialogueGraphCompileResult compiled = domain.compile(graph);
    if (compiled.status != EditorStatus::Applied) {
        EditorResult<dialogue::ConversationDocument*> failed;
        failed.status = compiled.status;
        failed.diagnostics = compiled.diagnostics;
        return failed;
    }
    const auto* id = field(compiled.definition, "id")->getIf<std::string>();
    const auto* version = field(compiled.definition, "version")->getIf<int64_t>();
    const auto* entry = field(compiled.definition, "entry")->getIf<std::string>();
    const auto* parameters = field(compiled.definition, "parameters")->getIf<EditorValue::Array>();
    const auto* nodes = field(compiled.definition, "nodes")->getIf<EditorValue::Array>();
    auto document = std::make_unique<dialogue::ConversationDocument>(*id);
    document->removeNode("end");
    if (!document->setVersion(static_cast<int>(*version)))
        return runtimeError<dialogue::ConversationDocument*>(EditorStatus::Failed,
                                                              "editor.dialogue.version-rejected",
                                                              "Conversation version was rejected");
    for (const EditorValue& parameter : *parameters)
        document->addParameter(*parameter.getIf<std::string>());
    for (const EditorValue& nodeValue : *nodes) {
        const auto* nodeId = field(nodeValue, "id")->getIf<std::string>();
        const auto* kind = field(nodeValue, "kind")->getIf<std::string>();
        if (!document->addNode(*nodeId, *kind))
            return runtimeError<dialogue::ConversationDocument*>(EditorStatus::Failed,
                                                                  "editor.dialogue.node-rejected",
                                                                  "Conversation node was rejected: " + *nodeId);
    }
    for (const EditorValue& nodeValue : *nodes) {
        const auto* nodeId = field(nodeValue, "id")->getIf<std::string>();
        const auto* kind = field(nodeValue, "kind")->getIf<std::string>();
        const auto* properties = field(nodeValue, "properties")->getIf<EditorValue::Object>();
        for (const auto& [key, value] : *properties) {
            const auto* text = value.getIf<std::string>();
            if (!text || !document->setField(*nodeId, key, *text))
                return runtimeError<dialogue::ConversationDocument*>(EditorStatus::Failed,
                                                                      "editor.dialogue.field-rejected",
                                                                      "Conversation field was rejected: " +
                                                                          *nodeId + "." + key);
        }
        if (*kind != "branch" && *kind != "choice" && *kind != "end") {
            const auto* next = field(nodeValue, "next")->getIf<std::string>();
            if (!document->setField(*nodeId, "next", *next))
                return runtimeError<dialogue::ConversationDocument*>(EditorStatus::Failed,
                                                                      "editor.dialogue.next-rejected",
                                                                      "Conversation next target was rejected");
        }
        const auto* routes = field(nodeValue, "routes")->getIf<EditorValue::Array>();
        for (const EditorValue& route : *routes) {
            const auto* label = field(route, "label")->getIf<std::string>();
            const auto* target = field(route, "target")->getIf<std::string>();
            if (!document->addRoute(*nodeId, *label, *target))
                return runtimeError<dialogue::ConversationDocument*>(EditorStatus::Failed,
                                                                      "editor.dialogue.route-rejected",
                                                                      "Conversation route was rejected");
        }
    }
    if (!document->setEntry(*entry) || !document->validate()) {
        EditorResult<dialogue::ConversationDocument*> failed =
            runtimeError<dialogue::ConversationDocument*>(EditorStatus::Failed,
                                                           "editor.dialogue.runtime-validation",
                                                           "Conversation runtime validation failed");
        for (int index = 0; index < document->getDiagnosticCount(); ++index)
            failed.diagnostics.push_back({RuleId("editor.dialogue.runtime-diagnostic"),
                                          document->getDiagnosticSeverity(index) == "error"
                                              ? DiagnosticSeverity::Error
                                              : DiagnosticSeverity::Warning,
                                          document->getDiagnosticMessage(index)});
        return failed;
    }
    return EditorResult<dialogue::ConversationDocument*>::applied(document.release());
}

}  // namespace eve::editor
