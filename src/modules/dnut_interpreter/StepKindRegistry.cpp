#include "dnut_interpreter/StepKindRegistry.h"

#include <utility>
#include <vector>

namespace eve::dnut {

namespace {

eve::Result<void> registryFailure(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<void>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "dnut.step-registry"));
}

bool isBareWord(const std::string& value) {
    if (value.empty()) return false;
    for (const char c : value) {
        const bool accepted = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                              c == '_' || c == '.' || c == ':';
        if (!accepted) return false;
    }
    return true;
}

bool matchesFieldType(const eve::Value& value, StepFieldType type) {
    switch (type) {
        case StepFieldType::String: return value.isString();
        case StepFieldType::Number: return value.isNumeric();
        case StepFieldType::Integer: return value.isInt64();
        case StepFieldType::Boolean: return value.isBool();
        case StepFieldType::Any: return !value.isNull();
    }
    return false;
}

const char* fieldTypeName(StepFieldType type) noexcept {
    switch (type) {
        case StepFieldType::String: return "string";
        case StepFieldType::Number: return "number";
        case StepFieldType::Integer: return "integer";
        case StepFieldType::Boolean: return "boolean";
        case StepFieldType::Any: return "value";
    }
    return "value";
}

}  // namespace

const char* stepShapeName(StepShape shape) noexcept {
    switch (shape) {
        case StepShape::Instant: return "instant";
        case StepShape::Await: return "await";
    }
    return "unknown";
}

eve::Result<void> StepKindRegistry::registerStep(StepKindDescriptor descriptor) {
    if (!isBareWord(descriptor.type))
        return registryFailure(eve::DiagnosticCode::InvalidArgument,
                               "step type must be a non-empty bare word", "type");
    if (descriptors_.contains(descriptor.type))
        return registryFailure(eve::DiagnosticCode::AlreadyExists,
                               "step type '" + descriptor.type + "' is already registered", descriptor.type);
    for (const auto& field : descriptor.fields) {
        if (!isBareWord(field.name))
            return registryFailure(eve::DiagnosticCode::InvalidArgument,
                                   "step '" + descriptor.type + "' declares an invalid field name", field.name);
    }
    if (descriptor.displayName.empty()) descriptor.displayName = descriptor.type;
    descriptors_.emplace(descriptor.type, std::move(descriptor));
    return eve::Result<void>::success();
}

eve::Result<void> StepKindRegistry::registerHandler(std::string_view type, StepHandler handler) {
    const std::string key(type);
    if (!descriptors_.contains(key))
        return registryFailure(eve::DiagnosticCode::NotFound,
                               "step type '" + key + "' must be declared before a handler is bound", key);
    if (!handler)
        return registryFailure(eve::DiagnosticCode::InvalidArgument,
                               "step handler for '" + key + "' must be callable", key);
    handlers_[key] = std::move(handler);
    return eve::Result<void>::success();
}

void StepKindRegistry::unregisterStep(std::string_view type) {
    const std::string key(type);
    descriptors_.erase(key);
    handlers_.erase(key);
}

bool StepKindRegistry::contains(std::string_view type) const { return descriptors_.contains(type); }

const StepKindDescriptor* StepKindRegistry::descriptor(std::string_view type) const {
    const auto found = descriptors_.find(type);
    return found == descriptors_.end() ? nullptr : &found->second;
}

std::vector<StepKindDescriptor> StepKindRegistry::descriptors() const {
    std::vector<StepKindDescriptor> out;
    out.reserve(descriptors_.size());
    for (const auto& entry : descriptors_) out.push_back(entry.second);
    return out;
}

bool StepKindRegistry::hasHandler(std::string_view type) const { return handlers_.contains(type); }

eve::Result<void> StepKindRegistry::validate(const SequenceNode& node) const {
    const auto* contract = descriptor(node.type);
    if (!contract)
        return registryFailure(eve::DiagnosticCode::NotFound,
                               "node '" + node.id + "' uses unregistered step type '" + node.type + "'",
                               "nodes." + node.id + ".type");
    if (!node.payload.isObject())
        return registryFailure(eve::DiagnosticCode::InvalidArgument,
                               "node '" + node.id + "' payload must be an object",
                               "nodes." + node.id + ".payload");

    for (const auto& field : contract->fields) {
        const eve::Value* value = node.payload.find(field.name);
        if (!value) {
            if (field.required)
                return registryFailure(eve::DiagnosticCode::InvalidArgument,
                                       "step '" + node.type + "' on node '" + node.id + "' requires field '" +
                                           field.name + "'",
                                       "nodes." + node.id + ".payload." + field.name);
            continue;
        }
        if (!matchesFieldType(*value, field.type))
            return registryFailure(eve::DiagnosticCode::InvalidArgument,
                                   "step '" + node.type + "' field '" + field.name + "' must be " +
                                       fieldTypeName(field.type),
                                   "nodes." + node.id + ".payload." + field.name);
    }

    for (const auto& key : node.payload.keys()) {
        bool declared = false;
        for (const auto& field : contract->fields) {
            if (field.name == key) {
                declared = true;
                break;
            }
        }
        if (!declared)
            return registryFailure(eve::DiagnosticCode::InvalidArgument,
                                   "step '" + node.type + "' does not accept field '" + key + "'",
                                   "nodes." + node.id + ".payload." + key);
    }

    if (contract->shape == StepShape::Instant && !hasHandler(node.type))
        return registryFailure(eve::DiagnosticCode::PreconditionViolation,
                               "step '" + node.type + "' is instant but has no handler bound",
                               "nodes." + node.id + ".type");
    if (contract->validate) {
        auto domainResult = contract->validate(node);
        if (!domainResult.ok()) return domainResult;
    }
    return eve::Result<void>::success();
}

StepOutcome StepKindRegistry::dispatch(const SequenceNode& node, const StepContext& context) const {
    const auto* contract = descriptor(node.type);
    if (!contract) {
        StepOutcome outcome;
        outcome.status = StepStatus::Failed;
        outcome.error  = "step type '" + node.type + "' is not registered";
        return outcome;
    }
    const auto handler = handlers_.find(node.type);
    if (handler == handlers_.end()) {
        if (contract->shape == StepShape::Await) {
            StepOutcome outcome;
            outcome.status = StepStatus::Blocked;
            return outcome;
        }
        StepOutcome outcome;
        outcome.status = StepStatus::Failed;
        outcome.error  = "step type '" + node.type + "' has no handler";
        return outcome;
    }
    return handler->second(node, context);
}

}  // namespace eve::dnut
