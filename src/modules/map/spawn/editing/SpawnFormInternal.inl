#pragma once
// Private structured property projection; canonical Spawn compilers own all semantic validation.
namespace eve::spawn_editing::detail {
template <class Target>
bool matchesSelection(const Target& target, const editing::SelectionSnapshot& selection, std::string_view type) {
    if (selection.items.size() != 1) return false;
    const auto& item = selection.items.front();
    return item.domain == editing::SelectionDomain::Asset && item.target == target.targetId() &&
           item.item.value() == target.targetId().value() && item.type == type;
}
inline editing::PropertyType fieldType(const Value& value) {
    switch (value.type()) {
        case Value::Type::Bool: return editing::PropertyType::Bool;
        case Value::Type::Int64: return editing::PropertyType::Int;
        case Value::Type::Double: return editing::PropertyType::Float;
        case Value::Type::String: return editing::PropertyType::String;
        case Value::Type::Array: return editing::PropertyType::Array;
        case Value::Type::Object: return editing::PropertyType::Struct;
        default: return editing::PropertyType::ReadOnlyText;
    }
}
inline bool readOnlyField(std::string_view key) { return key == "schema" || key == "version"; }
template <class Target>
editing::PropertySchema formSchema(const Target& target, const editing::SelectionSnapshot& selection,
                                   std::string_view type) {
    editing::PropertySchema result;
    if (!matchesSelection(target, selection, type)) return result;
    result.typeId                      = type;
    const auto                  source = target.source();
    editing::PropertyDescriptor whole;
    whole.path           = editing::PropertyPath("$source");
    whole.displayNameKey = "Spawn configuration";
    whole.category       = "spawn";
    whole.type           = editing::PropertyType::Struct;
    whole.flags          = editing::PropertyFlag::EditorOnly;
    result.properties.push_back(std::move(whole));
    for (const auto& [key, value] : *source.template getIf<Value::Object>()) {
        editing::PropertyDescriptor descriptor;
        descriptor.path           = editing::PropertyPath(key);
        descriptor.displayNameKey = key;
        descriptor.category       = "spawn";
        descriptor.type           = fieldType(value);
        descriptor.flags          = editing::PropertyFlag::EditorOnly;
        if (readOnlyField(key) || descriptor.type == editing::PropertyType::ReadOnlyText)
            descriptor.flags = descriptor.flags | editing::PropertyFlag::ReadOnly;
        result.properties.push_back(std::move(descriptor));
    }
    return result;
}
template <class Target>
editing::PropertyReadResult readField(const Target& target, const editing::SelectionSnapshot& selection,
                                      const editing::PropertyPath& path, std::string_view type) {
    if (!matchesSelection(target, selection, type)) return {};
    const auto source = target.source();
    if (path.value() == "$source") return {editing::PropertyReadState::Value, editing::toEditingValue(source), {}};
    const auto* value = source.find(path.value());
    if (!value) return {};
    return {editing::PropertyReadState::Value, editing::toEditingValue(*value), {}};
}
template <class Target>
Result<editing::DomainOperation> planField(const Target& target, const editing::SelectionSnapshot& selection,
                                           const editing::PropertyPath& path, const editing::Value& value,
                                           editing::PropertySetMode mode, std::string_view type) {
    if (!matchesSelection(target, selection, type))
        return failure<editing::DomainOperation>(DiagnosticCode::InvalidArgument, "Spawn form selection mismatch");
    if (mode != editing::PropertySetMode::Absolute)
        return failure<editing::DomainOperation>(DiagnosticCode::Unsupported, "Spawn forms require absolute values");
    auto       source = target.source();
    const auto key    = path.value();
    if (key == "$source") return target.makeReplace(editing::toPresentationValue(value));
    if (!source.find(key))
        return failure<editing::DomainOperation>(DiagnosticCode::NotFound, "Unknown Spawn source field");
    if (readOnlyField(key))
        return failure<editing::DomainOperation>(DiagnosticCode::PreconditionViolation,
                                                 "Spawn schema identity is read-only");
    source.set(key, editing::toPresentationValue(value));
    return target.makeReplace(source);
}
}  // namespace eve::spawn_editing::detail
