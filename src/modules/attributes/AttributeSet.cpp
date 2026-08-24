#include "attributes/AttributeSet.h"

#include <algorithm>
#include <utility>

namespace eve::attributes {

AttributeSet::AttributeSet(std::string subject) : subject_(std::move(subject)) {}

const std::string& AttributeSet::subject() const { return subject_; }

void AttributeSet::setBase(const std::string& attribute, double value) {
    if (!attribute.empty()) bases_[attribute] = value;
}

void AttributeSet::modifyBase(const std::string& attribute, double delta) {
    if (!attribute.empty()) bases_[attribute] += delta;
}

bool AttributeSet::has(const std::string& attribute) const {
    if (bases_.contains(attribute)) return true;
    for (const auto& [id, modifier] : modifiers_)
        if (modifier.attribute == attribute) return true;
    return false;
}

double AttributeSet::getBase(const std::string& attribute, double fallback) const {
    const auto it = bases_.find(attribute);
    return it == bases_.end() ? fallback : it->second;
}

double AttributeSet::getFinal(const std::string& attribute, double fallback) const {
    double                                value = getBase(attribute, fallback);
    std::vector<const AttributeModifier*> active;
    active.reserve(modifiers_.size());
    for (const auto& [id, modifier] : modifiers_)
        if (modifier.attribute == attribute) active.push_back(&modifier);
    std::sort(active.begin(), active.end(), [](const auto* a, const auto* b) {
        if (a->priority != b->priority) return a->priority < b->priority;
        return a->sequence < b->sequence;
    });
    for (const auto* modifier : active) {
        if (modifier->operation == "add")
            value += modifier->value;
        else if (modifier->operation == "multiply")
            value *= modifier->value;
        else if (modifier->operation == "override")
            value = modifier->value;
        else if (modifier->operation == "min")
            value = std::min(value, modifier->value);
        else if (modifier->operation == "max")
            value = std::max(value, modifier->value);
    }
    return value;
}

bool AttributeSet::addModifier(const std::string& id, const std::string& attribute, const std::string& source,
                               const std::string& operation, double value, int priority) {
    if (id.empty() || attribute.empty()) return false;
    if (operation != "add" && operation != "multiply" && operation != "override" && operation != "min" &&
        operation != "max")
        return false;
    modifiers_[id] = {id, attribute, source, operation, value, priority, nextSequence_++};
    orderDirty_    = true;
    return true;
}

bool AttributeSet::removeModifier(const std::string& id) {
    const bool removed = modifiers_.erase(id) != 0;
    orderDirty_        = orderDirty_ || removed;
    return removed;
}

int AttributeSet::removeBySource(const std::string& source, const std::string& attribute) {
    int removed = 0;
    for (auto it = modifiers_.begin(); it != modifiers_.end();) {
        if (it->second.source == source && (attribute.empty() || it->second.attribute == attribute)) {
            it = modifiers_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    orderDirty_ = orderDirty_ || removed != 0;
    return removed;
}

void AttributeSet::clearModifiers() {
    modifiers_.clear();
    order_.clear();
    orderDirty_ = false;
}

int AttributeSet::modifierCount() const { return int(modifiers_.size()); }

void AttributeSet::rebuildOrder() const {
    if (!orderDirty_) return;
    order_.clear();
    order_.reserve(modifiers_.size());
    for (const auto& [id, modifier] : modifiers_) order_.push_back(&modifier);
    std::sort(order_.begin(), order_.end(), [](const auto* a, const auto* b) { return a->sequence < b->sequence; });
    orderDirty_ = false;
}

const AttributeModifier* AttributeSet::modifierAt(int index) const {
    rebuildOrder();
    if (index < 0 || size_t(index) >= order_.size()) return nullptr;
    return order_[size_t(index)];
}

}  // namespace eve::attributes
