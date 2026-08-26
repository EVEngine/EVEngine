#include "presentation/PropertyModel.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::presentation {

const PropertyDescriptor *PropertySchema::find(const std::string &path) const {
    const auto found = std::find_if(properties.begin(), properties.end(),
                                    [&path](const PropertyDescriptor &property) {
                                        return property.path == path;
                                    });
    return found == properties.end() ? nullptr : &*found;
}

Subscription::Subscription(std::function<void()> dispose) : dispose_(std::move(dispose)) {}

Subscription::Subscription(Subscription &&other) noexcept
    : dispose_(std::move(other.dispose_)), disposed_(other.disposed_) {
    other.disposed_ = true;
}

Subscription &Subscription::operator=(Subscription &&other) noexcept {
    if (this == &other) return *this;
    dispose();
    dispose_ = std::move(other.dispose_);
    disposed_ = other.disposed_;
    other.disposed_ = true;
    return *this;
}

Subscription::~Subscription() { dispose(); }

void Subscription::dispose() {
    if (disposed_) return;
    disposed_ = true;
    if (dispose_) dispose_();
    dispose_ = {};
}

struct DynamicPropertyModel::ObserverState {
    struct Entry {
        std::uint64_t id = 0;
        ChangeCallback callback;
    };
    std::uint64_t nextId = 1;
    std::vector<Entry> entries;
};

DynamicPropertyModel::DynamicPropertyModel(PropertySchema schema)
    : schema_(std::move(schema)), observers_(std::make_shared<ObserverState>()) {
    for (const PropertyDescriptor &property : schema_.properties)
        values_.emplace(property.path, property.defaultValue);
}

DynamicPropertyModel::~DynamicPropertyModel() = default;

std::optional<Value> DynamicPropertyModel::read(const std::string &path) const {
    const auto found = values_.find(path);
    if (found == values_.end()) return std::nullopt;
    return found->second;
}

WriteResult DynamicPropertyModel::validate(const PropertyDescriptor &property,
                                           const Value &value) const {
    if (hasFlag(property.flags, PropertyFlag::ReadOnly))
        return WriteResult::reject("presentation.property.read-only", "Property is read-only");

    const Value::Type type = value.type();
    bool compatible = false;
    switch (property.kind) {
        case PropertyKind::Auto: compatible = true; break;
        case PropertyKind::Bool: compatible = type == Value::Type::Bool; break;
        case PropertyKind::Integer: compatible = type == Value::Type::Integer; break;
        case PropertyKind::Number:
            compatible = type == Value::Type::Number || type == Value::Type::Integer;
            break;
        case PropertyKind::String:
        case PropertyKind::Enum:
        case PropertyKind::AssetRef:
        case PropertyKind::ObjectRef:
        case PropertyKind::ReadOnlyText: compatible = type == Value::Type::String; break;
        case PropertyKind::Color:
        case PropertyKind::Vec2:
        case PropertyKind::Vec3:
        case PropertyKind::Vec4:
        case PropertyKind::Array: compatible = type == Value::Type::Array; break;
        case PropertyKind::Struct:
        case PropertyKind::Map: compatible = type == Value::Type::Object; break;
        case PropertyKind::Action: compatible = true; break;
    }
    if (!compatible)
        return WriteResult::reject("presentation.property.type", "Property value type does not match schema");

    if (property.kind == PropertyKind::Enum) {
        const auto *selected = value.getIf<std::string>();
        if (!selected || std::find(property.choices.begin(), property.choices.end(), *selected) ==
                             property.choices.end())
            return WriteResult::reject("presentation.property.choice", "Value is not an allowed choice");
    }

    double number = 0.0;
    bool hasNumber = false;
    if (const auto *integer = value.getIf<std::int64_t>()) {
        number = static_cast<double>(*integer);
        hasNumber = true;
    } else if (const auto *floating = value.getIf<double>()) {
        number = *floating;
        hasNumber = std::isfinite(number);
    }
    if (hasNumber && property.numeric.minimum && number < *property.numeric.minimum)
        return WriteResult::reject("presentation.property.minimum", "Value is below the minimum");
    if (hasNumber && property.numeric.maximum && number > *property.numeric.maximum)
        return WriteResult::reject("presentation.property.maximum", "Value is above the maximum");
    return WriteResult::success();
}

WriteResult DynamicPropertyModel::write(const std::string &path, const Value &value) {
    const PropertyDescriptor *property = schema_.find(path);
    if (!property)
        return WriteResult::reject("presentation.property.missing", "Property is not in the schema");
    WriteResult result = validate(*property, value);
    if (!result.accepted) return result;
    const auto found = values_.find(path);
    if (found != values_.end() && found->second == value) return result;
    values_[path] = value;
    emit(path, value);
    return result;
}

void DynamicPropertyModel::setSchema(PropertySchema schema) {
    schema_ = std::move(schema);
    std::map<std::string, Value> next;
    for (const PropertyDescriptor &property : schema_.properties) {
        const auto found = values_.find(property.path);
        next.emplace(property.path, found == values_.end() ? property.defaultValue : found->second);
    }
    values_ = std::move(next);
    ++revision_;
}

WriteResult DynamicPropertyModel::set(const std::string &path, Value value) {
    return write(path, value);
}

Subscription DynamicPropertyModel::subscribe(ChangeCallback callback) {
    const std::uint64_t id = observers_->nextId++;
    observers_->entries.push_back({id, std::move(callback)});
    std::weak_ptr<ObserverState> weak = observers_;
    return Subscription([weak, id]() {
        if (const auto state = weak.lock()) {
            std::erase_if(state->entries,
                          [id](const ObserverState::Entry &entry) { return entry.id == id; });
        }
    });
}

void DynamicPropertyModel::emit(const std::string &path, const Value &value) {
    const PropertyChange change{path, value, ++revision_};
    const auto snapshot = observers_->entries;
    for (const ObserverState::Entry &entry : snapshot)
        if (entry.callback) entry.callback(change);
}

}  // namespace eve::presentation
