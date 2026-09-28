#include "dialogue/DialogueSequence.h"

#include <string>
#include <utility>
#include <vector>

namespace eve::dialogue {
namespace {

template <class T = void>
eve::Result<T> payloadFailure(std::string message, std::string path) {
    return eve::Result<T>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, std::move(message), std::move(path), {}, "dialogue.sequence"));
}

std::string mutationKindName(eve::MutationKind kind) {
    switch (kind) {
        case eve::MutationKind::Set: return "set";
        case eve::MutationKind::Remove: return "remove";
        case eve::MutationKind::AddTag: return "addTag";
        case eve::MutationKind::RemoveTag: return "removeTag";
        case eve::MutationKind::AddNumber: return "addNumber";
    }
    return {};
}

eve::Result<eve::StateMutation> decodeMutation(const eve::Value& value, std::size_t index) {
    const std::string path = "stateMutations." + std::to_string(index);
    if (!value.isObject()) return payloadFailure<eve::StateMutation>("state mutation must be an object", path);
    for (const auto& key : value.keys())
        if (key != "subject" && key != "key" && key != "kind" && key != "value" && key != "persistent")
            return payloadFailure<eve::StateMutation>("unknown state mutation field '" + key + "'", path + "." + key);

    const eve::Value* subject    = value.find("subject");
    const eve::Value* key        = value.find("key");
    const eve::Value* kind       = value.find("kind");
    const eve::Value* rawValue   = value.find("value");
    const eve::Value* persistent = value.find("persistent");
    if (!subject || !subject->isString() || subject->asString().empty())
        return payloadFailure<eve::StateMutation>("state mutation subject must be a non-empty string",
                                                  path + ".subject");
    if (!key || !key->isString() || key->asString().empty())
        return payloadFailure<eve::StateMutation>("state mutation key must be a non-empty string", path + ".key");
    if (!kind || !kind->isString())
        return payloadFailure<eve::StateMutation>("state mutation kind must be a string", path + ".kind");

    eve::StateMutation mutation;
    mutation.subject = subject->asString();
    mutation.key     = key->asString();
    if (kind->asString() == "set")
        mutation.kind = eve::MutationKind::Set;
    else if (kind->asString() == "remove")
        mutation.kind = eve::MutationKind::Remove;
    else if (kind->asString() == "addTag")
        mutation.kind = eve::MutationKind::AddTag;
    else if (kind->asString() == "removeTag")
        mutation.kind = eve::MutationKind::RemoveTag;
    else if (kind->asString() == "addNumber")
        mutation.kind = eve::MutationKind::AddNumber;
    else
        return payloadFailure<eve::StateMutation>(
            "unsupported state mutation kind '" + kind->asString() + "'", path + ".kind");

    const bool needsValue = mutation.kind == eve::MutationKind::Set || mutation.kind == eve::MutationKind::AddNumber;
    if (needsValue && !rawValue)
        return payloadFailure<eve::StateMutation>("state mutation requires value", path + ".value");
    if (!needsValue && rawValue)
        return payloadFailure<eve::StateMutation>("state mutation kind does not accept value", path + ".value");
    if (rawValue) mutation.value = *rawValue;
    if (persistent) {
        if (!persistent->isBool())
            return payloadFailure<eve::StateMutation>("state mutation persistent must be boolean",
                                                      path + ".persistent");
        mutation.persistent = persistent->asBool();
    }
    return eve::Result<eve::StateMutation>::success(std::move(mutation));
}

eve::Result<void> validateChoice(const eve::dnut::SequenceNode& node) {
    for (const auto& route : node.routes) {
        auto payment = decodeSequencePayment(route.payload);
        if (!payment.ok()) return eve::Result<void>::failure(payment.status());
        auto mutations = decodeSequenceStateMutations(route.payload);
        if (!mutations.ok()) return eve::Result<void>::failure(mutations.status());
    }
    return eve::Result<void>::success();
}

eve::Result<void> validateCommand(const eve::dnut::SequenceNode& node) {
    const eve::Value* name = node.payload.find("name");
    if (!name) name = node.payload.find("target");
    if (!name || !name->isString() || name->asString().empty())
        return payloadFailure("dialogue command requires a non-empty name", "payload.name");
    const eve::Value* kind = node.payload.find("kind");
    if (kind && (!kind->isString() || (kind->asString() != "operation" && kind->asString() != "gameplay")))
        return payloadFailure("dialogue command kind must be operation or gameplay", "payload.kind");
    auto payment = decodeSequencePayment(node.payload);
    if (!payment.ok()) return eve::Result<void>::failure(payment.status());
    auto mutations = decodeSequenceStateMutations(node.payload);
    if (!mutations.ok()) return eve::Result<void>::failure(mutations.status());
    return eve::Result<void>::success();
}

}  // namespace

std::string sequencePayloadString(const eve::dnut::SequenceNode& node, std::string_view key) {
    const eve::Value* value = node.payload.find(std::string(key));
    return value && value->isString() ? value->asString() : std::string{};
}

std::string sequenceRoutePayloadString(const eve::dnut::SequenceRoute& route, std::string_view key) {
    const eve::Value* value = route.payload.find(std::string(key));
    return value && value->isString() ? value->asString() : std::string{};
}

eve::Result<PaymentSpec> decodeSequencePayment(const eve::Value& payload) {
    if (!payload.isObject()) return payloadFailure<PaymentSpec>("dialogue payload must be an object", "payload");
    const eve::Value* payment = payload.find("payment");
    if (!payment) return eve::Result<PaymentSpec>::success(PaymentSpec{});
    return PaymentSpec::fromValue(*payment);
}

eve::Result<std::vector<eve::StateMutation>> decodeSequenceStateMutations(const eve::Value& payload) {
    if (!payload.isObject())
        return payloadFailure<std::vector<eve::StateMutation>>("dialogue payload must be an object", "payload");
    const eve::Value* values = payload.find("stateMutations");
    if (!values) return eve::Result<std::vector<eve::StateMutation>>::success({});
    if (!values->isArray())
        return payloadFailure<std::vector<eve::StateMutation>>(
            "stateMutations must be an array", "payload.stateMutations");
    std::vector<eve::StateMutation> mutations;
    mutations.reserve(values->arraySize());
    for (std::size_t index = 0; index < values->arraySize(); ++index) {
        auto mutation = decodeMutation(values->at(index), index);
        if (!mutation.ok())
            return eve::Result<std::vector<eve::StateMutation>>::failure(mutation.status());
        mutations.push_back(std::move(mutation).takeValue());
    }
    return eve::Result<std::vector<eve::StateMutation>>::success(std::move(mutations));
}

eve::Value encodeSequenceStateMutations(std::span<const eve::StateMutation> mutations) {
    eve::Value::Array values;
    values.reserve(mutations.size());
    for (const auto& mutation : mutations) {
        eve::Value value = eve::Value::Object{};
        value.set("subject", eve::Value::string(mutation.subject));
        value.set("key", eve::Value::string(mutation.key));
        value.set("kind", eve::Value::string(mutationKindName(mutation.kind)));
        if (mutation.kind == eve::MutationKind::Set || mutation.kind == eve::MutationKind::AddNumber)
            value.set("value", mutation.value);
        if (mutation.persistent) value.set("persistent", eve::Value::boolean(true));
        values.push_back(std::move(value));
    }
    return eve::Value(std::move(values));
}

eve::Result<void> registerDialogueSequenceSteps(eve::dnut::StepKindRegistry& registry) {
    eve::dnut::StepKindRegistry candidate = registry;
    using eve::dnut::StepField;
    using eve::dnut::StepFieldType;
    using eve::dnut::StepKindDescriptor;
    using eve::dnut::StepShape;

    auto line = candidate.registerStep(
        StepKindDescriptor{"line", "Line", "dialogue", StepShape::Await,
                           {StepField{"speaker", StepFieldType::String, false},
                            StepField{"text", StepFieldType::String, false},
                            StepField{"pool", StepFieldType::String, false},
                            StepField{"i18n", StepFieldType::String, false},
                            StepField{"voice", StepFieldType::String, false},
                            StepField{"expression", StepFieldType::String, false}}});
    if (!line.ok()) return line;
    auto choice = candidate.registerStep(
        StepKindDescriptor{"choice", "Choice", "dialogue", StepShape::Await, {}, validateChoice});
    if (!choice.ok()) return choice;
    auto command = candidate.registerStep(
        StepKindDescriptor{"command", "Command", "dialogue", StepShape::Await,
                           {StepField{"name", StepFieldType::String, false},
                            StepField{"target", StepFieldType::String, false},
                            StepField{"kind", StepFieldType::String, false},
                            StepField{"arguments", StepFieldType::Any, false},
                            StepField{"payment", StepFieldType::Any, false},
                            StepField{"stateMutations", StepFieldType::Any, false},
                            StepField{"resultLocal", StepFieldType::String, false},
                            StepField{"result", StepFieldType::String, false}},
                           validateCommand});
    if (!command.ok()) return command;
    registry = std::move(candidate);
    return eve::Result<void>::success();
}

eve::Result<void> validateDialogueSequenceAsset(const eve::dnut::SequenceAsset& asset,
                                                const eve::dnut::StepKindRegistry& registry) {
    auto graph = asset.validate();
    if (!graph.ok()) return graph;
    for (const auto& node : asset.nodes) {
        if (registry.contains(node.type)) {
            auto validated = registry.validate(node);
            if (!validated.ok()) return validated;
        }
        if (node.type != "command" &&
            (node.payload.find("payment") || node.payload.find("stateMutations")))
            return payloadFailure("payment and stateMutations are valid only on command nodes",
                                  "nodes." + node.id + ".payload");
        for (const auto& route : node.routes)
            if (node.type != "choice" &&
                (route.payload.find("payment") || route.payload.find("stateMutations")))
                return payloadFailure("payment and stateMutations are valid only on choice routes",
                                      "nodes." + node.id + ".routes");
    }
    return eve::Result<void>::success();
}

eve::Result<CommandRequest> decodeDialogueCommandRequest(const eve::dnut::SequenceCommandRequest& request) {
    CommandRequest decoded;
    decoded.requestId = request.requestId;
    decoded.name      = request.name;
    decoded.arguments = request.arguments;
    decoded.bindings  = request.bindings;
    decoded.locals    = request.locals;
    const eve::Value* kind = request.payload.find("kind");
    decoded.kind = kind && kind->isString() && kind->asString() == "gameplay"
                       ? CommandRequestKind::GameplayAction
                       : CommandRequestKind::Operation;
    auto payment = decodeSequencePayment(request.payload);
    if (!payment.ok()) return eve::Result<CommandRequest>::failure(payment.status());
    decoded.payment = std::move(payment).takeValue();
    auto mutations = decodeSequenceStateMutations(request.payload);
    if (!mutations.ok()) return eve::Result<CommandRequest>::failure(mutations.status());
    decoded.stateMutations = std::move(mutations).takeValue();
    return eve::Result<CommandRequest>::success(std::move(decoded));
}

}  // namespace eve::dialogue
