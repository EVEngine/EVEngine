#include "attributes/AttributeSet.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::attributes {

namespace {

eve::Diagnostic invalidArgument(std::string message, std::string path = {}) {
    return eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move(message), std::move(path));
}

}  // namespace

const char* attributeOperationName(AttributeOperation operation) noexcept {
    switch (operation) {
        case AttributeOperation::Add: return "add";
        case AttributeOperation::AdditivePercent: return "additive_percent";
        case AttributeOperation::MultiplicativePercent: return "multiplicative_percent";
        case AttributeOperation::Override: return "override";
        case AttributeOperation::ClampMin: return "clamp_min";
        case AttributeOperation::ClampMax: return "clamp_max";
        case AttributeOperation::Custom: return "custom";
    }
    return "unknown";
}

eve::Result<ParsedAttributeOperation> parseAttributeOperation(std::string_view operation, double value) {
    ParsedAttributeOperation parsed;
    if (operation == "add") {
        parsed.operation = AttributeOperation::Add;
    } else if (operation == "additive_percent" || operation == "mulAdd") {
        parsed.operation = AttributeOperation::AdditivePercent;
    } else if (operation == "multiplicative_percent" || operation == "mulMul") {
        parsed.operation = AttributeOperation::MultiplicativePercent;
    } else if (operation == "multiply") {
        parsed.operation = AttributeOperation::MultiplicativePercent;
        value -= 1.0;
    } else if (operation == "override") {
        parsed.operation = AttributeOperation::Override;
    } else if (operation == "clamp_min" || operation == "clampMin" || operation == "min") {
        parsed.operation = AttributeOperation::ClampMin;
    } else if (operation == "clamp_max" || operation == "clampMax" || operation == "max") {
        parsed.operation = AttributeOperation::ClampMax;
    } else {
        return eve::Result<ParsedAttributeOperation>::failure(
            invalidArgument("unknown attribute operation", std::string(operation)));
    }
    parsed.value = value;
    return eve::Result<ParsedAttributeOperation>::success(std::move(parsed));
}

void AttributeOperationRegistry::registerOperation(const std::string& policyId, Function function) {
    if (!policyId.empty() && function) operations_[policyId] = std::move(function);
}

void AttributeOperationRegistry::unregisterOperation(const std::string& policyId) { operations_.erase(policyId); }

bool AttributeOperationRegistry::has(const std::string& policyId) const {
    return operations_.find(policyId) != operations_.end();
}

double AttributeOperationRegistry::apply(const std::string& policyId, double current, double value) const {
    const auto it = operations_.find(policyId);
    return it == operations_.end() ? current : it->second(current, value);
}

void AttributeOperationRegistry::registerOp(const std::string& policyId, Function function) {
    registerOperation(policyId, std::move(function));
}

void AttributeOperationRegistry::unregisterOp(const std::string& policyId) { unregisterOperation(policyId); }

AttributeModifier::AttributeModifier(ModifierId modifierId, SourceId sourceId, std::string_view operationName,
                                     double modifierValue, ModifierPriority priorityValue)
    : id(std::move(modifierId)), source(std::move(sourceId)), value(modifierValue), priority(priorityValue) {
    if (operationName == "add") {
        operation = AttributeOperation::Add;
    } else if (operationName == "mulAdd") {
        operation = AttributeOperation::AdditivePercent;
    } else if (operationName == "mulMul") {
        operation = AttributeOperation::MultiplicativePercent;
    } else if (operationName == "multiply") {
        operation = AttributeOperation::MultiplicativePercent;
        value -= 1.0;
    } else if (operationName == "override") {
        operation = AttributeOperation::Override;
    } else if (operationName == "min" || operationName == "clampMin") {
        operation = AttributeOperation::ClampMin;
    } else if (operationName == "max" || operationName == "clampMax") {
        operation = AttributeOperation::ClampMax;
    } else {
        operation = AttributeOperation::Custom;
        policyId  = std::string(operationName);
    }
}

double computeAttributeValue(const AttributeValue& attribute, const AttributeOperationRegistry* customOperations) {
    double                                additive              = 0.0;
    double                                additivePercent       = 0.0;
    double                                multiplicativePercent = 1.0;
    std::vector<const AttributeModifier*> ordered;
    ordered.reserve(attribute.modifiers.size());

    for (const auto& modifier : attribute.modifiers) {
        switch (modifier.operation) {
            case AttributeOperation::Add: additive += modifier.value; break;
            case AttributeOperation::AdditivePercent: additivePercent += modifier.value; break;
            case AttributeOperation::MultiplicativePercent: multiplicativePercent *= 1.0 + modifier.value; break;
            case AttributeOperation::Override:
            case AttributeOperation::ClampMin:
            case AttributeOperation::ClampMax:
            case AttributeOperation::Custom: ordered.push_back(&modifier); break;
        }
    }

    double result = (attribute.base + additive) * (1.0 + additivePercent);
    result *= multiplicativePercent;

    std::stable_sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
        if (left->priority != right->priority) return left->priority < right->priority;
        return left->sequence < right->sequence;
    });

    for (const auto* modifier : ordered) {
        switch (modifier->operation) {
            case AttributeOperation::Override: result = modifier->value; break;
            case AttributeOperation::ClampMin: result = std::max(result, modifier->value); break;
            case AttributeOperation::ClampMax: result = std::min(result, modifier->value); break;
            case AttributeOperation::Custom:
                if (customOperations && customOperations->has(modifier->policyId))
                    result = customOperations->apply(modifier->policyId, result, modifier->value);
                break;
            case AttributeOperation::Add:
            case AttributeOperation::AdditivePercent:
            case AttributeOperation::MultiplicativePercent: break;
        }
    }
    return result;
}

AttributeSet::AttributeSet(std::string subject) : subject_(std::move(subject)) {}

AttributeSet::AttributeSet(const AttributeSet& other)
    : subject_(other.subject_),
      values_(other.values_),
      nextSequence_(other.nextSequence_),
      nextModifierId_(other.nextModifierId_) {}

AttributeSet& AttributeSet::operator=(const AttributeSet& other) {
    if (this == &other) return *this;
    subject_        = other.subject_;
    values_         = other.values_;
    nextSequence_   = other.nextSequence_;
    nextModifierId_ = other.nextModifierId_;
    order_.clear();
    orderDirty_ = true;
    return *this;
}

AttributeSet::AttributeSet(AttributeSet&& other) noexcept
    : subject_(std::move(other.subject_)),
      values_(std::move(other.values_)),
      nextSequence_(other.nextSequence_),
      nextModifierId_(other.nextModifierId_) {}

AttributeSet& AttributeSet::operator=(AttributeSet&& other) noexcept {
    if (this == &other) return *this;
    subject_        = std::move(other.subject_);
    values_         = std::move(other.values_);
    nextSequence_   = other.nextSequence_;
    nextModifierId_ = other.nextModifierId_;
    order_.clear();
    orderDirty_ = true;
    return *this;
}

const std::string& AttributeSet::subject() const { return subject_; }

void AttributeSet::setBase(const AttributeId& attribute, double value) {
    if (!attribute.empty()) {
        auto& state = values_[attribute];
        state.base  = value;
        state.dirty = true;
    }
}

void AttributeSet::modifyBase(const AttributeId& attribute, double delta) {
    if (!attribute.empty()) {
        auto& state = values_[attribute];
        state.base += delta;
        state.dirty = true;
    }
}

bool AttributeSet::has(const AttributeId& attribute) const {
    return !attribute.empty() && values_.find(attribute) != values_.end();
}

double AttributeSet::getBase(const AttributeId& attribute, double fallback) const {
    const auto it = values_.find(attribute);
    return it == values_.end() ? fallback : it->second.base;
}

double AttributeSet::getFinal(const AttributeId& attribute, double fallback,
                              const AttributeOperationRegistry* customOperations) const {
    const auto it = values_.find(attribute);
    if (it == values_.end()) return fallback;
    auto& state = it->second;
    if (customOperations) return computeAttributeValue(state, customOperations);
    if (state.dirty) {
        state.cached = computeAttributeValue(state);
        state.dirty  = false;
    }
    return state.cached;
}

bool AttributeSet::isValidOperation(const AttributeModifier& modifier) noexcept {
    switch (modifier.operation) {
        case AttributeOperation::Add:
        case AttributeOperation::AdditivePercent:
        case AttributeOperation::MultiplicativePercent:
        case AttributeOperation::Override:
        case AttributeOperation::ClampMin:
        case AttributeOperation::ClampMax: return true;
        case AttributeOperation::Custom: return !modifier.policyId.empty();
    }
    return false;
}

eve::Result<ModifierId> AttributeSet::addModifier(AttributeModifier modifier) {
    if (modifier.attribute.empty())
        return eve::Result<ModifierId>::failure(
            invalidArgument("attribute modifier requires an attribute id", "attribute"));
    if (!isValidOperation(modifier))
        return eve::Result<ModifierId>::failure(
            invalidArgument("attribute modifier has an invalid operation", "operation"));
    if (!std::isfinite(modifier.value))
        return eve::Result<ModifierId>::failure(invalidArgument("attribute modifier value must be finite", "value"));

    if (modifier.id.empty()) {
        do {
            modifier.id = "attribute:modifier:";
            if (!subject_.empty()) modifier.id += subject_ + ":";
            modifier.id += std::to_string(nextModifierId_++);
        } while ([&] {
            for (const auto& [attribute, state] : values_)
                for (const auto& existing : state.modifiers)
                    if (existing.id == modifier.id) return true;
            return false;
        }());
    }
    const ModifierId  modifierId  = modifier.id;
    const AttributeId attributeId = modifier.attribute;
    removeModifier(modifierId).ignore("replace existing attribute modifier");
    modifier.sequence = nextSequence_++;
    auto& state       = values_[attributeId];
    state.modifiers.push_back(std::move(modifier));
    state.dirty = true;
    orderDirty_ = true;
    return eve::Result<ModifierId>::success(modifierId, eve::Status::success(eve::StatusCode::Applied));
}

bool AttributeSet::addModifier(const std::string& id, const std::string& attribute, const std::string& source,
                               const std::string& operation, double value, ModifierPriority priority) {
    if (id.empty()) return false;
    auto parsed = parseAttributeOperation(operation, value);
    if (!parsed.ok()) return false;
    AttributeModifier modifier{id, attribute, source, parsed.value().operation, parsed.value().value, priority};
    auto              result = addModifier(std::move(modifier));
    return result.ok();
}

eve::Result<void> AttributeSet::removeModifier(const ModifierId& id) {
    if (id.empty()) return eve::Result<void>::failure(invalidArgument("modifier id must not be empty", "id"));
    for (auto& [attribute, state] : values_) {
        const auto it = std::find_if(state.modifiers.begin(), state.modifiers.end(),
                                     [&](const auto& modifier) { return modifier.id == id; });
        if (it != state.modifiers.end()) {
            state.modifiers.erase(it);
            state.dirty = true;
            orderDirty_ = true;
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
        }
    }
    return eve::Result<void>::failure(eve::Status::failure(
        eve::StatusCode::NotFound,
        eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "attribute modifier was not found", "id")));
}

eve::Result<void> AttributeSet::removeModifier(const AttributeId& attribute, const ModifierId& id) {
    if (attribute.empty())
        return eve::Result<void>::failure(invalidArgument("attribute id must not be empty", "attribute"));
    if (id.empty()) return eve::Result<void>::failure(invalidArgument("modifier id must not be empty", "id"));
    const auto valueIt = values_.find(attribute);
    if (valueIt == values_.end())
        return eve::Result<void>::failure(eve::Status::failure(
            eve::StatusCode::NotFound,
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "attribute modifier was not found", "id")));
    auto&      modifiers = valueIt->second.modifiers;
    const auto modifierIt =
        std::find_if(modifiers.begin(), modifiers.end(), [&](const auto& modifier) { return modifier.id == id; });
    if (modifierIt == modifiers.end())
        return eve::Result<void>::failure(eve::Status::failure(
            eve::StatusCode::NotFound,
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "attribute modifier was not found", "id")));
    modifiers.erase(modifierIt);
    valueIt->second.dirty = true;
    orderDirty_           = true;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<int> AttributeSet::removeBySource(const SourceId& source, const AttributeId& attribute) {
    if (source.empty()) return eve::Result<int>::failure(invalidArgument("source id must not be empty", "source"));
    int removed = 0;
    for (auto& [attributeId, state] : values_) {
        if (!attribute.empty() && attributeId != attribute) continue;
        const auto oldSize = state.modifiers.size();
        state.modifiers.erase(std::remove_if(state.modifiers.begin(), state.modifiers.end(),
                                             [&](const auto& modifier) { return modifier.source == source; }),
                              state.modifiers.end());
        if (state.modifiers.size() != oldSize) {
            state.dirty = true;
            removed += static_cast<int>(oldSize - state.modifiers.size());
        }
    }
    if (removed != 0) orderDirty_ = true;
    const auto code = removed == 0 ? eve::StatusCode::NoOp : eve::StatusCode::Applied;
    return eve::Result<int>::success(removed, eve::Status::success(code));
}

void AttributeSet::invalidate(const AttributeId& attribute) {
    const auto it = values_.find(attribute);
    if (it != values_.end()) it->second.dirty = true;
}

void AttributeSet::clearModifiers() {
    for (auto& [attribute, state] : values_) {
        state.modifiers.clear();
        state.dirty = true;
    }
    order_.clear();
    orderDirty_ = false;
}

int AttributeSet::modifierCount() const {
    int count = 0;
    for (const auto& [attribute, state] : values_) count += static_cast<int>(state.modifiers.size());
    return count;
}

void AttributeSet::rebuildOrder() const {
    if (!orderDirty_) return;
    order_.clear();
    for (const auto& [attribute, state] : values_)
        for (const auto& modifier : state.modifiers) order_.push_back(&modifier);
    std::sort(order_.begin(), order_.end(),
              [](const auto* left, const auto* right) { return left->sequence < right->sequence; });
    orderDirty_ = false;
}

const AttributeModifier* AttributeSet::modifierAt(int index) const {
    rebuildOrder();
    if (index < 0 || static_cast<std::size_t>(index) >= order_.size()) return nullptr;
    return order_[static_cast<std::size_t>(index)];
}

}  // namespace eve::attributes
