#include "ui/PropertyView.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <utility>
#include <vector>

namespace eve::ui {
namespace {

using eve::Value;
using property_access::PropertyDescriptor;
using property_access::PropertyFlag;
using property_access::PropertyKind;

/** UI callbacks cannot surface Result diagnostics yet; rejection leaves widgets unchanged. */
void applyWrite(property_access::IPropertyAccess &model, const std::string &path, Value value) {
    model.write(path, std::move(value))
        .ignore("PropertyView UI writes are best-effort until the shell can present Result diagnostics");
}

std::string sanitize(std::string value) {
    for (char &character : value)
        if (character == ' ' || character == '.' || character == '[' || character == ']' ||
            character == '/')
            character = '_';
    return value;
}

std::string displayName(const PropertyDescriptor &property) {
    if (!property.displayName.empty()) return property.displayName;
    const std::size_t separator = property.path.find_last_of("./");
    return separator == std::string::npos ? property.path : property.path.substr(separator + 1);
}

std::string valueText(const Value &value) {
    if (const auto *text = value.getIf<std::string>()) return *text;
    if (const auto *boolean = value.getIf<bool>()) return *boolean ? "true" : "false";
    if (const auto *integer = value.getIf<std::int64_t>()) return std::to_string(*integer);
    if (const auto *number = value.getIf<double>()) return std::to_string(*number);
    if (const auto *array = value.getIf<Value::Array>())
        return std::to_string(array->size()) + " items";
    if (const auto *object = value.getIf<Value::Object>())
        return std::to_string(object->size()) + " fields";
    return "null";
}

double numericValue(const Value &value) {
    if (const auto *number = value.getIf<double>()) return *number;
    if (const auto *integer = value.getIf<std::int64_t>()) return static_cast<double>(*integer);
    return 0.0;
}

bool parseNumber(const std::string &text, bool integer, Value &out) {
    if (integer) {
        std::int64_t  parsed    = 0;
        const auto    converted = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (converted.ec != std::errc() || converted.ptr != text.data() + text.size()) return false;
        out = Value(parsed);
        return true;
    }
    char *end = nullptr;
    errno     = 0;
    const double parsed = std::strtod(text.c_str(), &end);
    if (text.empty() || errno == ERANGE || end != text.c_str() + text.size()) return false;
    out = Value(parsed);
    return true;
}

Value::Array componentArray(const Value &value, std::size_t count) {
    Value::Array result(count, Value(0.0));
    if (const auto *items = value.getIf<Value::Array>()) {
        for (std::size_t index = 0; index < count && index < items->size(); ++index)
            result[index] = Value(numericValue((*items)[index]));
    }
    return result;
}

AccessibilityRole accessibilityRole(PropertyKind kind, bool readOnly) {
    if (readOnly) return AccessibilityRole::Text;
    switch (kind) {
        case PropertyKind::Bool: return AccessibilityRole::Checkbox;
        case PropertyKind::Integer:
        case PropertyKind::Number: return AccessibilityRole::Slider;
        case PropertyKind::Action: return AccessibilityRole::Button;
        case PropertyKind::Enum: return AccessibilityRole::List;
        case PropertyKind::Color: return AccessibilityRole::Slider;
        case PropertyKind::String:
        case PropertyKind::AssetRef:
        case PropertyKind::ObjectRef:
        case PropertyKind::Auto: return AccessibilityRole::TextInput;
        default: return AccessibilityRole::Region;
    }
}

WidgetDesc scalarElementEditor(const std::string &elementId, const std::string &elementLabel,
                               const Value &element, std::function<void(Value)> writeElement) {
    if (const auto *boolean = element.getIf<bool>()) {
        return checkbox(elementLabel, *boolean, elementId,
                        [writeElement = std::move(writeElement)](bool next) { writeElement(Value(next)); });
    }
    if (element.getIf<std::int64_t>() || element.getIf<double>()) {
        return inputText(elementLabel, valueText(element), elementId,
                         [writeElement = std::move(writeElement),
                          integer = element.getIf<std::int64_t>() != nullptr](const std::string &next) {
                             Value parsed;
                             if (parseNumber(next, integer, parsed)) writeElement(std::move(parsed));
                         });
    }
    if (element.getIf<std::string>() || element.isNull()) {
        return inputText(elementLabel, valueText(element), elementId,
                         [writeElement = std::move(writeElement)](const std::string &next) {
                             writeElement(Value(next));
                         });
    }
    return text(elementLabel + " = " + valueText(element), elementId);
}

WidgetDesc makeCompositeEditor(property_access::IPropertyAccess &model, const PropertyViewOptions &options,
                               const PropertyDescriptor &property, const Value &value,
                               const std::vector<std::string> &labels) {
    const std::string id     = propertyWidgetId(options, property.path);
    Value::Array      items  = componentArray(value, labels.size());
    std::vector<WidgetDesc> children;
    children.push_back(text(displayName(property), id + "_label"));
    for (std::size_t index = 0; index < labels.size(); ++index) {
        const std::string componentId = id + "_" + labels[index];
        const std::string path        = property.path;
        const std::size_t component   = index;
        const bool        useSlider   = property.numeric.minimum && property.numeric.maximum;
        if (useSlider) {
            children.push_back(slider(
                labels[index], static_cast<float>(numericValue(items[index])),
                static_cast<float>(*property.numeric.minimum), static_cast<float>(*property.numeric.maximum),
                componentId, [&model, path, component, count = labels.size()](float next) {
                    const std::optional<Value> current = model.read(path);
                    Value::Array              updated =
                        componentArray(current ? *current : Value(Value::Array{}), count);
                    updated[component] = Value(static_cast<double>(next));
                    applyWrite(model, path, Value(std::move(updated)));
                }));
        } else {
            children.push_back(inputText(
                labels[index], valueText(items[index]), componentId,
                [&model, path, component, count = labels.size()](const std::string &next) {
                    Value parsed;
                    if (!parseNumber(next, false, parsed)) return;
                    const std::optional<Value> current = model.read(path);
                    Value::Array              updated =
                        componentArray(current ? *current : Value(Value::Array{}), count);
                    updated[component] = std::move(parsed);
                    applyWrite(model, path, Value(std::move(updated)));
                }));
        }
    }
    return row(std::move(children), id);
}

WidgetDesc makeArrayEditor(property_access::IPropertyAccess &model, const PropertyViewOptions &options,
                           const PropertyDescriptor &property, const Value &value) {
    const std::string id = propertyWidgetId(options, property.path);
    Value::Array      items;
    if (const auto *array = value.getIf<Value::Array>()) items = *array;
    std::vector<WidgetDesc> rows;
    for (std::size_t index = 0; index < items.size(); ++index) {
        const std::string elementId    = id + "_" + std::to_string(index);
        const std::string elementLabel = displayName(property) + "[" + std::to_string(index) + "]";
        const std::string path         = property.path;
        const std::size_t elementIndex = index;
        WidgetDesc        cell         = scalarElementEditor(
            elementId, elementLabel, items[index],
            [&model, path, elementIndex](Value next) {
                const std::optional<Value> current = model.read(path);
                Value::Array              updated;
                if (const auto *array = current ? current->getIf<Value::Array>() : nullptr)
                    updated = *array;
                if (elementIndex >= updated.size()) updated.resize(elementIndex + 1);
                updated[elementIndex] = std::move(next);
                applyWrite(model, path, Value(std::move(updated)));
            });
        const auto onStructure = options.onStructureChange;
        rows.push_back(row(
            {std::move(cell),
             button("x##" + elementId + "_del", elementId + "_del",
                    [&model, path, elementIndex, onStructure]() {
                        const std::optional<Value> current = model.read(path);
                        Value::Array              updated;
                        if (const auto *array = current ? current->getIf<Value::Array>() : nullptr)
                            updated = *array;
                        if (elementIndex < updated.size())
                            updated.erase(updated.begin() + static_cast<std::ptrdiff_t>(elementIndex));
                        applyWrite(model, path, Value(std::move(updated)));
                        if (onStructure) onStructure();
                    })},
            elementId + "_row"));
    }
    const std::string path = property.path;
    const auto        onStructure = options.onStructureChange;
    rows.push_back(row(
        {button("+##" + id + "_add", id + "_add",
                [&model, path, onStructure]() {
                    const std::optional<Value> current = model.read(path);
                    Value::Array              updated;
                    if (const auto *array = current ? current->getIf<Value::Array>() : nullptr)
                        updated = *array;
                    updated.emplace_back(std::string{});
                    applyWrite(model, path, Value(std::move(updated)));
                    if (onStructure) onStructure();
                })},
        id + "_addrow"));
    return collapsingHeader(displayName(property) + " (array[" + std::to_string(items.size()) + "])##" + id,
                            std::move(rows), id, false);
}

WidgetDesc makeMapEditor(property_access::IPropertyAccess &model, const PropertyViewOptions &options,
                         const PropertyDescriptor &property, const Value &value, bool allowMutate) {
    const std::string id = propertyWidgetId(options, property.path);
    Value::Object     fields;
    if (const auto *object = value.getIf<Value::Object>()) fields = *object;
    std::vector<WidgetDesc> rows;
    for (const auto &[key, entry] : fields) {
        const std::string elementId = id + "_" + sanitize(key);
        const std::string path      = property.path;
        const std::string fieldKey  = key;
        WidgetDesc        cell      = scalarElementEditor(
            elementId, key, entry, [&model, path, fieldKey](Value next) {
                const std::optional<Value> current = model.read(path);
                Value::Object             updated;
                if (const auto *object = current ? current->getIf<Value::Object>() : nullptr)
                    updated = *object;
                updated[fieldKey] = std::move(next);
                applyWrite(model, path, Value(std::move(updated)));
            });
        if (allowMutate) {
            const auto onStructure = options.onStructureChange;
            rows.push_back(row(
                {std::move(cell),
                 button("x##" + elementId + "_del", elementId + "_del",
                        [&model, path, fieldKey, onStructure]() {
                            const std::optional<Value> current = model.read(path);
                            Value::Object             updated;
                            if (const auto *object = current ? current->getIf<Value::Object>() : nullptr)
                                updated = *object;
                            updated.erase(fieldKey);
                            applyWrite(model, path, Value(std::move(updated)));
                            if (onStructure) onStructure();
                        })},
                elementId + "_row"));
        } else {
            rows.push_back(std::move(cell));
        }
    }
    if (allowMutate) {
        const std::string path = property.path;
        const auto        onStructure = options.onStructureChange;
        rows.push_back(row(
            {button("+##" + id + "_add", id + "_add",
                    [&model, path, onStructure]() {
                        const std::optional<Value> current = model.read(path);
                        Value::Object             updated;
                        if (const auto *object = current ? current->getIf<Value::Object>() : nullptr)
                            updated = *object;
                        std::string key    = "key" + std::to_string(updated.size());
                        std::size_t suffix = 0;
                        while (updated.contains(key))
                            key = "key" + std::to_string(updated.size() + (++suffix));
                        updated.emplace(key, Value(std::string{}));
                        applyWrite(model, path, Value(std::move(updated)));
                        if (onStructure) onStructure();
                    })},
            id + "_addrow"));
    }
    const std::string kindLabel = property.kind == PropertyKind::Struct ? "struct" : "map";
    return collapsingHeader(displayName(property) + " (" + kindLabel + ")##" + id, std::move(rows), id, false);
}

WidgetDesc makePropertyField(property_access::IPropertyAccess &model, const PropertyViewOptions &options,
                             const PropertyDescriptor &property, const Value &value) {
    const std::string id = propertyWidgetId(options, property.path);
    const std::string label = displayName(property);
    const bool        readOnly =
        property_access::hasFlag(property.flags, PropertyFlag::ReadOnly) || property.kind == PropertyKind::ReadOnlyText;
    WidgetDesc result;

    if (readOnly) {
        if (property.kind == PropertyKind::Array || property.kind == PropertyKind::Map ||
            property.kind == PropertyKind::Struct)
            result = collapsingHeader(label + ": " + valueText(value) + "##" + id,
                                      {text(valueText(value), id + "_summary")}, id, false);
        else
            result = text(label + ": " + valueText(value), id);
    } else {
        switch (property.kind) {
            case PropertyKind::Bool: {
                const bool current = value.getIf<bool>() ? *value.getIf<bool>() : false;
                const std::string path = property.path;
                result = checkbox(label, current, id, [&model, path](bool next) {
                    applyWrite(model, path, Value(next));
                });
                break;
            }
            case PropertyKind::Integer:
            case PropertyKind::Number: {
                const bool integer = property.kind == PropertyKind::Integer;
                const std::string path = property.path;
                if (property.numeric.minimum && property.numeric.maximum) {
                    result = slider(label, static_cast<float>(numericValue(value)),
                                    static_cast<float>(*property.numeric.minimum),
                                    static_cast<float>(*property.numeric.maximum), id,
                                    [&model, path, integer](float next) {
                                        if (integer)
                                            applyWrite(model, path, Value(static_cast<std::int64_t>(next)));
                                        else
                                            applyWrite(model, path, Value(static_cast<double>(next)));
                                    });
                } else {
                    result = inputText(label, valueText(value), id,
                                       [&model, path, integer](const std::string &next) {
                                           Value parsed;
                                           if (parseNumber(next, integer, parsed))
                                               applyWrite(model, path, std::move(parsed));
                                       });
                }
                break;
            }
            case PropertyKind::Enum: {
                const std::string selected = valueText(value);
                int selectedIndex = 0;
                for (std::size_t index = 0; index < property.choices.size(); ++index)
                    if (property.choices[index] == selected) selectedIndex = static_cast<int>(index);
                const std::string path = property.path;
                const std::vector<std::string> choices = property.choices;
                result = combo(label, choices, selectedIndex, id,
                               [&model, path, choices](float next) {
                                   const int index = static_cast<int>(next);
                                   if (index >= 0 && static_cast<std::size_t>(index) < choices.size())
                                       applyWrite(model, path, Value(choices[static_cast<std::size_t>(index)]));
                               });
                break;
            }
            case PropertyKind::Action: {
                const std::string path = property.path;
                result = button(label, id, [&model, path]() { applyWrite(model, path, Value(true)); });
                break;
            }
            case PropertyKind::String:
            case PropertyKind::AssetRef:
            case PropertyKind::ObjectRef:
            case PropertyKind::Auto: {
                const std::string path = property.path;
                result = inputText(label, valueText(value), id,
                                   [&model, path](const std::string &next) {
                                       applyWrite(model, path, Value(next));
                                   });
                break;
            }
            case PropertyKind::Color:
                result = makeCompositeEditor(model, options, property, value, {"r", "g", "b", "a"});
                break;
            case PropertyKind::Vec2:
                result = makeCompositeEditor(model, options, property, value, {"x", "y"});
                break;
            case PropertyKind::Vec3:
                result = makeCompositeEditor(model, options, property, value, {"x", "y", "z"});
                break;
            case PropertyKind::Vec4:
                result = makeCompositeEditor(model, options, property, value, {"x", "y", "z", "w"});
                break;
            case PropertyKind::Array:
                result = makeArrayEditor(model, options, property, value);
                break;
            case PropertyKind::Map:
                result = makeMapEditor(model, options, property, value, true);
                break;
            case PropertyKind::Struct:
                result = makeMapEditor(model, options, property, value, true);
                break;
            case PropertyKind::ReadOnlyText:
                result = text(label + ": " + valueText(value), id);
                break;
        }
    }
    result.tooltip = property.description;
    result.accessibilityRole = accessibilityRole(property.kind, readOnly);
    result.accessibilityName = label;
    result.accessibilityDescription = property.description;
    if (readOnly) {
        result.focusMode = FocusMode::None;
        result.mouseFilter = MouseFilter::Ignore;
    }
    return result;
}

bool visible(const PropertyDescriptor &property, const PropertyViewOptions &options) {
    if (!options.showAdvanced && property_access::hasFlag(property.flags, PropertyFlag::Advanced)) return false;
    if (!options.showEditorOnly && property_access::hasFlag(property.flags, PropertyFlag::EditorOnly)) return false;
    if (!options.showReadOnly && (property_access::hasFlag(property.flags, PropertyFlag::ReadOnly) ||
                                  property.kind == PropertyKind::ReadOnlyText))
        return false;
    return true;
}

void syncComposite(UIHost &host, const std::string &id, const Value &value,
                   const std::vector<std::string> &labels, const PropertyDescriptor &property) {
    Value::Array items = componentArray(value, labels.size());
    for (std::size_t index = 0; index < labels.size(); ++index) {
        const std::string componentId = id + "_" + labels[index];
        if (property.numeric.minimum && property.numeric.maximum)
            host.setValueById(componentId, static_cast<float>(numericValue(items[index])));
        else
            host.setValueTextById(componentId, valueText(items[index]));
    }
}

}  // namespace

std::string propertyWidgetId(const PropertyViewOptions &options, const std::string &path) {
    return options.idPrefix + sanitize(path);
}

WidgetDesc buildPropertyField(property_access::IPropertyAccess &model, const std::string &path,
                              const PropertyViewOptions &options) {
    auto                       property = model.schema().find(path);
    const std::optional<Value> value = model.read(path);
    if (!property || !value)
        return text(path + ": unavailable", propertyWidgetId(options, path))
            .withFocusMode(FocusMode::None)
            .withMouseFilter(MouseFilter::Ignore)
            .withAccessibility(AccessibilityRole::Text, path, "Property is unavailable");
    return makePropertyField(model, options, property->get(), *value);
}

WidgetDesc buildPropertyView(property_access::IPropertyAccess &model, const PropertyViewOptions &options) {
    std::vector<WidgetDesc> children;
    if (!options.title.empty()) children.push_back(sectionHeader(options.title, options.idPrefix + "title"));

    std::string category;
    for (const PropertyDescriptor &property : model.schema().properties) {
        if (!visible(property, options)) continue;
        const std::optional<Value> value = model.read(property.path);
        if (!value) continue;
        if (options.groupCategories && !property.category.empty() && property.category != category) {
            category = property.category;
            children.push_back(sectionHeader(category, options.idPrefix + "category/" + sanitize(category)));
        }
        children.push_back(makePropertyField(model, options, property, *value));
    }
    return column(std::move(children), options.idPrefix + "root");
}

void syncPropertyView(UIHost &host, const property_access::IPropertyAccess &model, const PropertyViewOptions &options) {
    for (const PropertyDescriptor &property : model.schema().properties) {
        const std::optional<Value> value = model.read(property.path);
        if (!value) continue;
        const std::string id = propertyWidgetId(options, property.path);
        const bool        readOnly = property_access::hasFlag(property.flags, PropertyFlag::ReadOnly) ||
                              property.kind == PropertyKind::ReadOnlyText;
        if (readOnly) {
            host.setTextById(id, displayName(property) + ": " + valueText(*value));
            continue;
        }
        switch (property.kind) {
            case PropertyKind::Bool:
                if (const auto *boolean = value->getIf<bool>()) host.setCheckedById(id, *boolean);
                break;
            case PropertyKind::Enum: {
                const std::string selected = valueText(*value);
                const auto found = std::find(property.choices.begin(), property.choices.end(), selected);
                host.setValueById(id, found == property.choices.end()
                                          ? 0.f
                                          : static_cast<float>(found - property.choices.begin()));
                break;
            }
            case PropertyKind::Integer:
            case PropertyKind::Number:
                if (property.numeric.minimum && property.numeric.maximum)
                    host.setValueById(id, static_cast<float>(numericValue(*value)));
                else
                    host.setValueTextById(id, valueText(*value));
                break;
            case PropertyKind::Color:
                syncComposite(host, id, *value, {"r", "g", "b", "a"}, property);
                break;
            case PropertyKind::Vec2:
                syncComposite(host, id, *value, {"x", "y"}, property);
                break;
            case PropertyKind::Vec3:
                syncComposite(host, id, *value, {"x", "y", "z"}, property);
                break;
            case PropertyKind::Vec4:
                syncComposite(host, id, *value, {"x", "y", "z", "w"}, property);
                break;
            case PropertyKind::Array:
                if (const auto *items = value->getIf<Value::Array>()) {
                    for (std::size_t index = 0; index < items->size(); ++index) {
                        const std::string elementId = id + "_" + std::to_string(index);
                        if (const auto *boolean = (*items)[index].getIf<bool>())
                            host.setCheckedById(elementId, *boolean);
                        else
                            host.setValueTextById(elementId, valueText((*items)[index]));
                    }
                }
                break;
            case PropertyKind::Map:
            case PropertyKind::Struct:
                if (const auto *fields = value->getIf<Value::Object>()) {
                    for (const auto &[key, entry] : *fields) {
                        const std::string elementId = id + "_" + sanitize(key);
                        if (const auto *boolean = entry.getIf<bool>())
                            host.setCheckedById(elementId, *boolean);
                        else
                            host.setValueTextById(elementId, valueText(entry));
                    }
                }
                break;
            case PropertyKind::String:
            case PropertyKind::AssetRef:
            case PropertyKind::ObjectRef:
            case PropertyKind::Auto:
                if (const auto *textValue = value->getIf<std::string>())
                    host.setValueTextById(id, *textValue);
                break;
            case PropertyKind::Action:
            case PropertyKind::ReadOnlyText:
                break;
        }
    }
}

PropertyComponent::PropertyComponent(property_access::IPropertyAccess *model, PropertyViewOptions options)
    : model_(model), options_(std::move(options)) {
    observe();
}

void PropertyComponent::bind(property_access::IPropertyAccess *model) {
    subscription_.dispose();
    model_ = model;
    observe();
    markDirty();
}

void PropertyComponent::setOptions(PropertyViewOptions options) {
    options_ = std::move(options);
    markDirty();
}

WidgetDesc PropertyComponent::build() {
    if (!model_) return column({}, options_.idPrefix + "root");
    return buildPropertyView(*model_, options_);
}

void PropertyComponent::observe() {
    if (!model_) return;
    subscription_ = model_->subscribe([this](const property_access::PropertyChange &) { markDirty(); });
}

}  // namespace eve::ui
