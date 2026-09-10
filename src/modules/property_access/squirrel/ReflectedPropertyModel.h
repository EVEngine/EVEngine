#pragma once

#include "common/Runtime.h"
#include "property_access/PropertyAccess.h"

#include <map>
#include <memory>

namespace eve::property_access {

/**
 * @brief Adapts one live Squirrel instance to the renderer-independent property model.
 *
 * Reflection metadata defines the schema while Runtime owns all reads and writes.
 * Scalar properties are editable; arrays, tables and nested instances are exposed
 * as read-only structured values for generic views and automation.
 *
 * This adapter is compiled with `property_access` but is not part of the L0
 * contract: `PropertyAccess.h` stays free of the script runtime. Include this
 * header only from script-aware hosts such as the Inspector.
 */
class EVENGINE_API ReflectedPropertyModel final : public IPropertyAccess {
public:
    ReflectedPropertyModel(Runtime &runtime, ssq::Object instance);
    ~ReflectedPropertyModel() override;
    ReflectedPropertyModel(const ReflectedPropertyModel &) = delete;
    ReflectedPropertyModel &operator=(const ReflectedPropertyModel &) = delete;

    const PropertySchema &schema() const override { return schema_; }
    std::optional<eve::Value> read(const std::string &path) const override;
    WriteResult               write(const std::string &path, const eve::Value &value) override;
    std::uint64_t             revision() const override { return revision_; }
    Subscription              subscribe(ChangeCallback callback) override;

    /** @brief Rebuild reflection metadata after a script reload. */
    void rebuildSchema();
    /** @brief Pull live values and notify observers for changed properties. */
    void refresh();

private:
    struct ObserverState;
    eve::Value convertValue(const std::string &path, const ReflectedValue &value) const;
    void       emit(const std::string &path, const eve::Value &value);

    Runtime                      *runtime_ = nullptr;
    ssq::Object                   instance_;
    PropertySchema                schema_;
    std::map<std::string, eve::Value> cachedValues_;
    std::uint64_t                 revision_ = 0;
    std::shared_ptr<ObserverState> observers_;
};

}  // namespace eve::property_access
