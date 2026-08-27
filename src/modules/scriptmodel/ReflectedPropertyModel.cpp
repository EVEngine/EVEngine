#include "scriptmodel/ReflectedPropertyModel.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace eve::scriptmodel {
namespace {

using property_access::PropertyDescriptor;
using property_access::PropertyFlag;
using property_access::PropertyKind;
using eve::Value;

PropertyKind propertyKind(const ReflectedMember &member) {
    if (member.attrString("editor") == "combo" && !member.attrOptions("options").empty())
        return PropertyKind::Enum;
    switch (member.value.kind) {
        case ReflectedValueKind::Bool: return PropertyKind::Bool;
        case ReflectedValueKind::Integer: return PropertyKind::Integer;
        case ReflectedValueKind::Float: return PropertyKind::Number;
        case ReflectedValueKind::String: return PropertyKind::String;
        case ReflectedValueKind::Array: return PropertyKind::Array;
        case ReflectedValueKind::Table: return PropertyKind::Map;
        case ReflectedValueKind::Instance: return PropertyKind::ObjectRef;
        case ReflectedValueKind::None:
        case ReflectedValueKind::Other: return PropertyKind::ReadOnlyText;
    }
    return PropertyKind::Auto;
}

bool scalar(ReflectedValueKind kind) {
    return kind == ReflectedValueKind::Bool || kind == ReflectedValueKind::Integer ||
           kind == ReflectedValueKind::Float || kind == ReflectedValueKind::String;
}

std::string ownerClass(const Runtime &runtime, const std::string &className,
                       const std::string &memberName) {
    std::string current = className;
    while (!current.empty()) {
        const ReflectedClass *reflected = runtime.reflectedClass(current);
        if (!reflected) break;
        const auto found = std::find_if(reflected->members.begin(), reflected->members.end(),
                                        [&memberName](const ReflectedMember &member) {
                                            return member.name == memberName;
                                        });
        if (found != reflected->members.end()) return current;
        current = reflected->base;
    }
    return className;
}

PropertyFlag propertyFlags(const ReflectedMember &member) {
    PropertyFlag flags = PropertyFlag::Runtime;
    if (!scalar(member.value.kind) || member.attrBool("read_only") ||
        member.attrBool("readonly"))
        flags = flags | PropertyFlag::ReadOnly;
    if (member.attrBool("advanced")) flags = flags | PropertyFlag::Advanced;
    if (member.attrBool("editor_only")) flags = flags | PropertyFlag::EditorOnly;
    if (member.attrBool("transient")) flags = flags | PropertyFlag::Transient;
    if (member.attrBool("dangerous")) flags = flags | PropertyFlag::Dangerous;
    return flags;
}

ReflectedValue toReflectedValue(const Value &value) {
    ReflectedValue result;
    if (const auto *boolean = value.getIf<bool>()) {
        result.kind = ReflectedValueKind::Bool;
        result.boolean = *boolean;
    } else if (const auto *integer = value.getIf<std::int64_t>()) {
        result.kind = ReflectedValueKind::Integer;
        result.integer = *integer;
    } else if (const auto *number = value.getIf<double>()) {
        result.kind = ReflectedValueKind::Float;
        result.floating = *number;
    } else if (const auto *text = value.getIf<std::string>()) {
        result.kind = ReflectedValueKind::String;
        result.text = *text;
    }
    return result;
}

const char *scriptValidationCode(const std::string &presentationCode) {
    if (presentationCode == "property_access.property.read-only")
        return "scriptmodel.property.read-only";
    if (presentationCode == "property_access.property.type")
        return "scriptmodel.property.type";
    if (presentationCode == "property_access.property.choice")
        return "scriptmodel.property.choice";
    if (presentationCode == "property_access.property.finite")
        return "scriptmodel.property.finite";
    if (presentationCode == "property_access.property.minimum")
        return "scriptmodel.property.minimum";
    if (presentationCode == "property_access.property.maximum")
        return "scriptmodel.property.maximum";
    return "scriptmodel.property.validation";
}

property_access::WriteResult validationFailure(const property_access::WriteResult &validation) {
    return property_access::WriteResult::reject(scriptValidationCode(validation.code), validation.message);
}

}  // namespace

struct ReflectedPropertyModel::ObserverState {
    struct Entry {
        std::uint64_t id = 0;
        ChangeCallback callback;
    };
    std::uint64_t nextId = 1;
    std::vector<Entry> entries;
};

ReflectedPropertyModel::ReflectedPropertyModel(Runtime &runtime, ssq::Object instance)
    : runtime_(&runtime), instance_(std::move(instance)),
      observers_(std::make_shared<ObserverState>()) {
    rebuildSchema();
    refresh();
}

ReflectedPropertyModel::~ReflectedPropertyModel() = default;

void ReflectedPropertyModel::rebuildSchema() {
    schema_ = {};
    cachedValues_.clear();
    if (!runtime_ || instance_.getType() != ssq::Type::INSTANCE) return;
    schema_.typeId = runtime_->classNameOf(instance_);
    for (const ReflectedMember &member : runtime_->reflectInstance(instance_)) {
        if (member.method) continue;
        PropertyDescriptor descriptor;
        descriptor.path = member.name;
        descriptor.displayName = member.attrString("label", member.name);
        descriptor.description = member.attrString(
            "tooltip", member.attrString("description"));
        descriptor.category = member.attrString(
            "category", ownerClass(*runtime_, schema_.typeId, member.name));
        descriptor.kind = propertyKind(member);
        descriptor.flags = propertyFlags(member);
        descriptor.defaultValue = convertValue(member.name, member.value);
        descriptor.numeric.minimum = member.findAttribute("min")
                                         ? std::optional<double>(member.attrFloat("min"))
                                         : std::nullopt;
        descriptor.numeric.maximum = member.findAttribute("max")
                                         ? std::optional<double>(member.attrFloat("max"))
                                         : std::nullopt;
        descriptor.numeric.step = member.findAttribute("step")
                                      ? std::optional<double>(member.attrFloat("step"))
                                      : std::nullopt;
        descriptor.numeric.units = member.attrString("units");
        descriptor.numeric.precision = static_cast<int>(member.attrFloat("precision", 3.f));
        descriptor.choices = member.attrOptions("options");
        descriptor.presenterHint = member.attrString("editor");
        schema_.properties.push_back(std::move(descriptor));
    }
    ++revision_;
}

Value ReflectedPropertyModel::convertValue(const std::string &path,
                                           const ReflectedValue &value) const {
    switch (value.kind) {
        case ReflectedValueKind::Bool: return Value(value.boolean);
        case ReflectedValueKind::Integer: return Value(value.integer);
        case ReflectedValueKind::Float: return Value(value.floating);
        case ReflectedValueKind::String: return Value(value.text);
        case ReflectedValueKind::Array: {
            Value::Array result;
            const std::size_t count = runtime_->arraySize(instance_, path);
            result.reserve(count);
            for (std::size_t index = 0; index < count; ++index) {
                const ReflectedValue entry = runtime_->arrayGet(instance_, path, index);
                if (scalar(entry.kind)) {
                    switch (entry.kind) {
                        case ReflectedValueKind::Bool: result.emplace_back(entry.boolean); break;
                        case ReflectedValueKind::Integer: result.emplace_back(entry.integer); break;
                        case ReflectedValueKind::Float: result.emplace_back(entry.floating); break;
                        case ReflectedValueKind::String: result.emplace_back(entry.text); break;
                        default: break;
                    }
                } else {
                    result.emplace_back();
                }
            }
            return Value(std::move(result));
        }
        case ReflectedValueKind::Table: {
            Value::Object result;
            for (const std::string &key : runtime_->tableKeys(instance_, path)) {
                const ReflectedValue entry = runtime_->tableGet(instance_, path, key);
                switch (entry.kind) {
                    case ReflectedValueKind::Bool: result.emplace(key, Value(entry.boolean)); break;
                    case ReflectedValueKind::Integer: result.emplace(key, Value(entry.integer)); break;
                    case ReflectedValueKind::Float: result.emplace(key, Value(entry.floating)); break;
                    case ReflectedValueKind::String: result.emplace(key, Value(entry.text)); break;
                    default: result.emplace(key, Value()); break;
                }
            }
            return Value(std::move(result));
        }
        case ReflectedValueKind::Instance: {
            const ssq::Object nested = runtime_->readObjectProperty(instance_, path);
            return Value(runtime_->classNameOf(nested));
        }
        case ReflectedValueKind::None:
        case ReflectedValueKind::Other: return {};
    }
    return {};
}

std::optional<Value> ReflectedPropertyModel::read(const std::string &path) const {
    if (!runtime_ || !schema_.find(path)) return std::nullopt;
    return convertValue(path, runtime_->readProperty(instance_, path));
}

property_access::WriteResult ReflectedPropertyModel::write(const std::string &path,
                                                        const Value &value) {
    auto descriptor = schema_.find(path);
    if (!descriptor)
        return property_access::WriteResult::reject("scriptmodel.property.missing",
                                                  "Property is not reflected");
    const property_access::WriteResult validation =
        property_access::validatePropertyValue(descriptor->get(), value);
    if (!validation.accepted) return validationFailure(validation);

    ReflectedValue reflected = toReflectedValue(value);
    if (reflected.empty())
        return property_access::WriteResult::reject("scriptmodel.property.type",
                                                  "Unsupported property value type");
    if (!runtime_->writeProperty(instance_, path, reflected))
        return property_access::WriteResult::reject("scriptmodel.property.write",
                                                  "Runtime rejected the property write");
    const Value applied = convertValue(path, runtime_->readProperty(instance_, path));
    const auto found = cachedValues_.find(path);
    if (found == cachedValues_.end() || found->second != applied) emit(path, applied);
    return property_access::WriteResult::success();
}

property_access::Subscription ReflectedPropertyModel::subscribe(ChangeCallback callback) {
    const std::uint64_t id = observers_->nextId++;
    observers_->entries.push_back({id, std::move(callback)});
    std::weak_ptr<ObserverState> weak = observers_;
    return property_access::Subscription([weak, id]() {
        if (const auto state = weak.lock())
            std::erase_if(state->entries,
                          [id](const ObserverState::Entry &entry) { return entry.id == id; });
    });
}

void ReflectedPropertyModel::refresh() {
    for (const PropertyDescriptor &descriptor : schema_.properties) {
        const std::optional<Value> current = read(descriptor.path);
        if (!current) continue;
        const auto found = cachedValues_.find(descriptor.path);
        if (found == cachedValues_.end() || found->second != *current)
            emit(descriptor.path, *current);
    }
}

void ReflectedPropertyModel::emit(const std::string &path, const Value &value) {
    cachedValues_[path] = value;
    const property_access::PropertyChange change{path, value, ++revision_};
    const auto snapshot = observers_->entries;
    for (const ObserverState::Entry &entry : snapshot)
        if (entry.callback) entry.callback(change);
}

}  // namespace eve::scriptmodel
