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

using property_access::PropertyDescriptor;
using property_access::PropertyFlag;
using property_access::PropertyKind;
using eve::Value;

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

AccessibilityRole accessibilityRole(PropertyKind kind, bool readOnly) {
    if (readOnly) return AccessibilityRole::Text;
    switch (kind) {
        case PropertyKind::Bool: return AccessibilityRole::Checkbox;
        case PropertyKind::Integer:
        case PropertyKind::Number: return AccessibilityRole::Slider;
        case PropertyKind::Action: return AccessibilityRole::Button;
        case PropertyKind::Enum: return AccessibilityRole::List;
        case PropertyKind::String:
        case PropertyKind::AssetRef:
        case PropertyKind::ObjectRef:
        case PropertyKind::Auto: return AccessibilityRole::TextInput;
        default: return AccessibilityRole::Text;
    }
}

WidgetDesc makePropertyField(property_access::IPropertyAccess &model,
                             const PropertyViewOptions &options,
                             const PropertyDescriptor &property, const Value &value) {
    const std::string id = propertyWidgetId(options, property.path);
    const std::string label = displayName(property);
    const bool readOnly = property_access::hasFlag(property.flags, PropertyFlag::ReadOnly) ||
                          property.kind == PropertyKind::ReadOnlyText;
    WidgetDesc result;

    if (readOnly) {
        result = text(label + ": " + valueText(value), id);
    } else {
        switch (property.kind) {
            case PropertyKind::Bool: {
                const bool current = value.getIf<bool>() ? *value.getIf<bool>() : false;
                const std::string path = property.path;
                result = checkbox(label, current, id, [&model, path](bool next) {
                    model.write(path, Value(next));
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
                                            model.write(path, Value(static_cast<std::int64_t>(next)));
                                        else
                                            model.write(path, Value(static_cast<double>(next)));
                                    });
                } else {
                    result = inputText(label, valueText(value), id,
                                       [&model, path, integer](const std::string &next) {
                                           if (integer) {
                                               std::int64_t parsed = 0;
                                               const auto converted = std::from_chars(
                                                   next.data(), next.data() + next.size(), parsed);
                                               if (converted.ec == std::errc() &&
                                                   converted.ptr == next.data() + next.size())
                                                   model.write(path, Value(parsed));
                                           } else {
                                               char *end = nullptr;
                                               errno = 0;
                                               const double parsed = std::strtod(next.c_str(), &end);
                                               if (!next.empty() && errno != ERANGE &&
                                                   end == next.c_str() + next.size())
                                                   model.write(path, Value(parsed));
                                           }
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
                                       model.write(path, Value(choices[static_cast<std::size_t>(index)]));
                               });
                break;
            }
            case PropertyKind::Action: {
                const std::string path = property.path;
                result = button(label, id, [&model, path]() { model.write(path, Value(true)); });
                break;
            }
            case PropertyKind::String:
            case PropertyKind::AssetRef:
            case PropertyKind::ObjectRef:
            case PropertyKind::Auto: {
                const std::string path = property.path;
                result = inputText(label, valueText(value), id,
                                   [&model, path](const std::string &next) {
                                       model.write(path, Value(next));
                                   });
                break;
            }
            case PropertyKind::Color:
            case PropertyKind::Vec2:
            case PropertyKind::Vec3:
            case PropertyKind::Vec4:
            case PropertyKind::Struct:
            case PropertyKind::Array:
            case PropertyKind::Map:
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
    if (!options.showAdvanced && property_access::hasFlag(property.flags, PropertyFlag::Advanced))
        return false;
    if (!options.showEditorOnly &&
        property_access::hasFlag(property.flags, PropertyFlag::EditorOnly))
        return false;
    if (!options.showReadOnly &&
        (property_access::hasFlag(property.flags, PropertyFlag::ReadOnly) ||
         property.kind == PropertyKind::ReadOnlyText))
        return false;
    return true;
}

}  // namespace

std::string propertyWidgetId(const PropertyViewOptions &options, const std::string &path) {
    return options.idPrefix + sanitize(path);
}

WidgetDesc buildPropertyField(property_access::IPropertyAccess &model, const std::string &path,
                              const PropertyViewOptions &options) {
    auto property = model.schema().find(path);
    const std::optional<Value> value = model.read(path);
    if (!property || !value)
        return text(path + ": unavailable", propertyWidgetId(options, path))
            .withFocusMode(FocusMode::None)
            .withMouseFilter(MouseFilter::Ignore)
            .withAccessibility(AccessibilityRole::Text, path, "Property is unavailable");
    return makePropertyField(model, options, property->get(), *value);
}

WidgetDesc buildPropertyView(property_access::IPropertyAccess &model,
                             const PropertyViewOptions &options) {
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

void syncPropertyView(UIHost &host, const property_access::IPropertyAccess &model,
                      const PropertyViewOptions &options) {
    for (const PropertyDescriptor &property : model.schema().properties) {
        const std::optional<Value> value = model.read(property.path);
        if (!value) continue;
        const std::string id = propertyWidgetId(options, property.path);
        const bool readOnly = property_access::hasFlag(property.flags, PropertyFlag::ReadOnly) ||
                              property.kind == PropertyKind::ReadOnlyText;
        if (readOnly) {
            host.setTextById(id, displayName(property) + ": " + valueText(*value));
        } else if (property.kind == PropertyKind::Bool) {
            if (const auto *boolean = value->getIf<bool>()) host.setCheckedById(id, *boolean);
        } else if (property.kind == PropertyKind::Enum) {
            const std::string selected = valueText(*value);
            const auto found = std::find(property.choices.begin(), property.choices.end(), selected);
            host.setValueById(id, found == property.choices.end()
                                      ? 0.f
                                      : static_cast<float>(found - property.choices.begin()));
        } else if (property.kind == PropertyKind::Integer ||
                   property.kind == PropertyKind::Number) {
            if (property.numeric.minimum && property.numeric.maximum)
                host.setValueById(id, static_cast<float>(numericValue(*value)));
            else
                host.setValueTextById(id, valueText(*value));
        } else if (const auto *textValue = value->getIf<std::string>()) {
            host.setValueTextById(id, *textValue);
        }
    }
}

PropertyComponent::PropertyComponent(property_access::IPropertyAccess *model,
                                     PropertyViewOptions options)
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
