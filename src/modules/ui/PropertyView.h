#pragma once

#include "property_access/PropertyAccess.h"
#include "ui/Component.h"

#include <string>

namespace eve::ui {

/** @brief Policy controlling schema-to-widget generation for a UI shell. */
struct PropertyViewOptions {
    std::string idPrefix = "property/";
    std::string title;
    bool showAdvanced = false;
    bool showEditorOnly = false;
    bool showReadOnly = true;
    bool groupCategories = true;
};

/** @brief Return the stable widget id generated for a property path. */
std::string propertyWidgetId(const PropertyViewOptions &options, const std::string &path);

/** @brief Generate one field from a model property, or a diagnostic text row. */
WidgetDesc buildPropertyField(property_access::IPropertyAccess &model, const std::string &path,
                              const PropertyViewOptions &options = {});

/**
 * @brief Generate a declarative, two-way property view from any model schema.
 *
 * The returned tree contains no backend-specific state. Widget callbacks call
 * IPropertyAccess::write(), so gameplay models and command-backed editor models
 * use the same UI generation path.
 */
WidgetDesc buildPropertyView(property_access::IPropertyAccess &model, const PropertyViewOptions &options = {});

/** @brief Pull current model values into an existing compatible UI tree. */
void syncPropertyView(UIHost &host, const property_access::IPropertyAccess &model,
                      const PropertyViewOptions &options = {});

/** @brief Component wrapper that becomes dirty when its bound model changes. */
class PropertyComponent final : public Component {
public:
    explicit PropertyComponent(property_access::IPropertyAccess *model = nullptr, PropertyViewOptions options = {});
    ~PropertyComponent() override = default;

    /** @brief Replace the model and reconnect change observation. */
    void bind(property_access::IPropertyAccess *model);
    /**
     * @brief Return the currently bound property model, or null when unbound.
     * @return Borrowed nullable model supplied by the caller.
     * @ownership PropertyComponent does not own the model; the caller must keep it alive.
     * @lifetime Valid until bind() replaces it or the model is destroyed; do not retain across frames without external
     * lifetime control.
     * @thread Call on the UI thread.
     * @reentrancy The accessor invokes no callbacks and is invalid across bind/rebuild mutation.
     */
    property_access::IPropertyAccess *model() const { return model_; }
    /** @brief Replace generation policy and mark the component dirty. */
    void setOptions(PropertyViewOptions options);
    /** @brief Build the current schema-driven tree. */
    WidgetDesc build() override;

private:
    void observe();

    property_access::IPropertyAccess *model_ = nullptr;
    PropertyViewOptions options_;
    property_access::Subscription     subscription_;
};

}  // namespace eve::ui
