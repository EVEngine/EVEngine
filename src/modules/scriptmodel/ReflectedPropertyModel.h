#pragma once

#include "common/Runtime.h"
#include "property_access/PropertyAccess.h"

#include <memory>

namespace eve::scriptmodel {

/**
 * @brief Adapts one live Squirrel instance to the renderer-independent property model.
 *
 * Reflection metadata defines the schema while Runtime owns all reads and writes.
 * Scalar properties are editable; arrays, tables and nested instances are exposed
 * as read-only structured values for generic views and automation.
 */
class EVENGINE_API ReflectedPropertyModel final : public property_access::IPropertyAccess {
public:
    ReflectedPropertyModel(Runtime &runtime, ssq::Object instance);
    ~ReflectedPropertyModel() override;
    ReflectedPropertyModel(const ReflectedPropertyModel &) = delete;
    ReflectedPropertyModel &operator=(const ReflectedPropertyModel &) = delete;

    const property_access::PropertySchema &schema() const override { return schema_; }
    std::optional<eve::Value> read(const std::string &path) const override;
    property_access::WriteResult write(const std::string &path,
                                    const eve::Value &value) override;
    std::uint64_t revision() const override { return revision_; }
    property_access::Subscription subscribe(ChangeCallback callback) override;

    /** @brief Rebuild reflection metadata after a script reload. */
    void rebuildSchema();
    /** @brief Pull live values and notify observers for changed properties. */
    void refresh();

private:
    struct ObserverState;
    eve::Value convertValue(const std::string &path,
                                     const ReflectedValue &value) const;
    void emit(const std::string &path, const eve::Value &value);

    Runtime *runtime_ = nullptr;
    ssq::Object instance_;
    property_access::PropertySchema schema_;
    std::map<std::string, eve::Value> cachedValues_;
    std::uint64_t revision_ = 0;
    std::shared_ptr<ObserverState> observers_;
};

}  // namespace eve::scriptmodel
