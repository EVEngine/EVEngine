#include "editor/EditorPropertyModel.h"

#include <algorithm>
#include <utility>

namespace eve::editor {
namespace {

presentation::PropertyKind convertKind(PropertyType type) {
    using Kind = presentation::PropertyKind;
    switch (type) {
        case PropertyType::Bool: return Kind::Bool;
        case PropertyType::Int: return Kind::Integer;
        case PropertyType::Float: return Kind::Number;
        case PropertyType::String: return Kind::String;
        case PropertyType::Enum: return Kind::Enum;
        case PropertyType::Color: return Kind::Color;
        case PropertyType::Vec2: return Kind::Vec2;
        case PropertyType::Vec3: return Kind::Vec3;
        case PropertyType::Vec4:
        case PropertyType::Transform: return Kind::Vec4;
        case PropertyType::AssetRef: return Kind::AssetRef;
        case PropertyType::ObjectRef: return Kind::ObjectRef;
        case PropertyType::Struct: return Kind::Struct;
        case PropertyType::Array: return Kind::Array;
        case PropertyType::Map: return Kind::Map;
        case PropertyType::Action: return Kind::Action;
        case PropertyType::ReadOnlyText: return Kind::ReadOnlyText;
    }
    return Kind::Auto;
}

presentation::PropertyFlag convertFlags(PropertyFlag flags) {
    using Flag = presentation::PropertyFlag;
    Flag result = Flag::None;
    if (hasPropertyFlag(flags, PropertyFlag::ReadOnly)) result = result | Flag::ReadOnly;
    if (hasPropertyFlag(flags, PropertyFlag::Advanced)) result = result | Flag::Advanced;
    if (hasPropertyFlag(flags, PropertyFlag::EditorOnly)) result = result | Flag::EditorOnly;
    if (hasPropertyFlag(flags, PropertyFlag::Runtime)) result = result | Flag::Runtime;
    if (hasPropertyFlag(flags, PropertyFlag::Transient)) result = result | Flag::Transient;
    if (hasPropertyFlag(flags, PropertyFlag::Dangerous)) result = result | Flag::Dangerous;
    if (hasPropertyFlag(flags, PropertyFlag::MultiEdit)) result = result | Flag::MultiEdit;
    return result;
}

presentation::PropertyDescriptor convertDescriptor(const PropertyDescriptor &source) {
    presentation::PropertyDescriptor result;
    result.path = source.path.value();
    result.displayName = source.displayNameKey;
    result.description = source.descriptionKey;
    result.category = source.category;
    result.kind = convertKind(source.type);
    result.flags = convertFlags(source.flags);
    result.defaultValue = toPresentationValue(source.defaultValue);
    result.numeric.minimum = source.numeric.minimum;
    result.numeric.maximum = source.numeric.maximum;
    result.numeric.step = source.numeric.step;
    result.numeric.units = source.numeric.units;
    result.numeric.precision = source.numeric.precision;
    result.choices = source.enumItems;
    result.presenterHint = source.presenterHint;
    return result;
}

}  // namespace

presentation::Value toPresentationValue(const EditorValue &value) {
    return std::visit(
        [](const auto &current) -> presentation::Value {
            using T = std::decay_t<decltype(current)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return {};
            } else if constexpr (std::is_same_v<T, EditorValue::Array>) {
                presentation::Value::Array result;
                result.reserve(current.size());
                for (const EditorValue &entry : current) result.push_back(toPresentationValue(entry));
                return result;
            } else if constexpr (std::is_same_v<T, EditorValue::Object>) {
                presentation::Value::Object result;
                for (const auto &[key, entry] : current) result.emplace(key, toPresentationValue(entry));
                return result;
            } else {
                return presentation::Value(current);
            }
        },
        value.storage());
}

EditorValue toEditorValue(const presentation::Value &value) {
    return std::visit(
        [](const auto &current) -> EditorValue {
            using T = std::decay_t<decltype(current)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return {};
            } else if constexpr (std::is_same_v<T, presentation::Value::Array>) {
                EditorValue::Array result;
                result.reserve(current.size());
                for (const presentation::Value &entry : current) result.push_back(toEditorValue(entry));
                return result;
            } else if constexpr (std::is_same_v<T, presentation::Value::Object>) {
                EditorValue::Object result;
                for (const auto &[key, entry] : current) result.emplace(key, toEditorValue(entry));
                return result;
            } else {
                return EditorValue(current);
            }
        },
        value.storage());
}

struct EditorPropertyModel::ObserverState {
    struct Entry {
        std::uint64_t id = 0;
        ChangeCallback callback;
    };
    std::uint64_t nextId = 1;
    std::vector<Entry> entries;
};

EditorPropertyModel::EditorPropertyModel(PropertySchema schema, SelectionSnapshot selection,
                                         const IPropertyProvider *provider,
                                         PropertyModelSurface surface, HostProfile profile)
    : editorSchema_(std::move(schema)), selection_(std::move(selection)), provider_(provider),
      surface_(surface), profile_(std::move(profile)), observers_(std::make_shared<ObserverState>()) {
    rebuildSchema();
    refresh();
}

EditorPropertyModel::~EditorPropertyModel() = default;

void EditorPropertyModel::rebuildSchema() {
    presentationSchema_.typeId = editorSchema_.typeId;
    presentationSchema_.version = editorSchema_.version;
    presentationSchema_.properties.clear();
    for (const PropertyDescriptor &property : editorSchema_.properties) {
        if (surface_ == PropertyModelSurface::Runtime &&
            (!hasPropertyFlag(property.flags, PropertyFlag::Runtime) ||
             hasPropertyFlag(property.flags, PropertyFlag::EditorOnly)))
            continue;
        presentationSchema_.properties.push_back(convertDescriptor(property));
    }
}

std::optional<presentation::Value> EditorPropertyModel::read(const std::string &path) const {
    if (!provider_ || !presentationSchema_.find(path)) return std::nullopt;
    const PropertyReadResult result = provider_->read(selection_, PropertyPath(path));
    if (result.state != PropertyReadState::Value) return std::nullopt;
    return toPresentationValue(result.value);
}

presentation::WriteResult EditorPropertyModel::write(const std::string &path,
                                                       const presentation::Value &value) {
    if (!provider_) return presentation::WriteResult::reject("editor.property.provider", "No property provider");
    EditorResult<PropertyEditIntent> intent;
    if (surface_ == PropertyModelSurface::Runtime) {
        RuntimePropertyPresenter presenter;
        intent = presenter.editIntent(editorSchema_, selection_, PropertyPath(path),
                                      toEditorValue(value), profile_);
    } else {
        DeveloperPropertyPresenter presenter;
        intent = presenter.editIntent(editorSchema_, selection_, PropertyPath(path), toEditorValue(value));
    }
    if (!intent.accepted() || !intent.value)
        return presentation::WriteResult::reject("editor.property.intent", "Property edit was rejected");
    if (!sink_) return presentation::WriteResult::reject("editor.property.sink", "No command sink is connected");
    const EditorResult<void> applied = sink_(*intent.value);
    if (!applied.accepted())
        return presentation::WriteResult::reject("editor.property.command", "Property command failed");
    emit(path, value);
    return presentation::WriteResult::success();
}

presentation::Subscription EditorPropertyModel::subscribe(ChangeCallback callback) {
    const std::uint64_t id = observers_->nextId++;
    observers_->entries.push_back({id, std::move(callback)});
    std::weak_ptr<ObserverState> weak = observers_;
    return presentation::Subscription([weak, id]() {
        if (const auto state = weak.lock())
            std::erase_if(state->entries,
                          [id](const ObserverState::Entry &entry) { return entry.id == id; });
    });
}

void EditorPropertyModel::refresh() {
    for (const presentation::PropertyDescriptor &property : presentationSchema_.properties) {
        const std::optional<presentation::Value> current = read(property.path);
        if (!current) continue;
        const auto found = cachedValues_.find(property.path);
        if (found == cachedValues_.end() || found->second != *current) {
            cachedValues_[property.path] = *current;
            emit(property.path, *current);
        }
    }
}

void EditorPropertyModel::emit(const std::string &path, const presentation::Value &value) {
    cachedValues_[path] = value;
    const presentation::PropertyChange change{path, value, ++revision_};
    const auto snapshot = observers_->entries;
    for (const ObserverState::Entry &entry : snapshot)
        if (entry.callback) entry.callback(change);
}

}  // namespace eve::editor
