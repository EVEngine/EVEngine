#pragma once

#include "property_access/PropertyAccess.h"

#include <algorithm>
#include <map>
#include <memory>
#include <utility>
#include <vector>

/** Test-only in-memory authority for the IPropertyAccess contract. */
class TestPropertyAccess final : public eve::property_access::IPropertyAccess {
public:
    explicit TestPropertyAccess(eve::property_access::PropertySchema schema = {})
        : schema_(std::move(schema)), observers_(std::make_shared<ObserverState>()) {
        for (const auto& property : schema_.properties)
            values_.emplace(property.path, property.defaultValue);
    }

    const eve::property_access::PropertySchema& schema() const override { return schema_; }
    std::optional<eve::Value> read(const std::string& path) const override {
        const auto found = values_.find(path);
        return found == values_.end() ? std::nullopt : std::optional<eve::Value>(found->second);
    }
    eve::property_access::WriteResult write(const std::string& path,
                                             const eve::Value& value) override {
        auto property = schema_.find(path);
        if (!property)
            return eve::property_access::WriteResult::reject(
                "property_access.property.missing", "Property is not in the schema");
        auto result = eve::property_access::validatePropertyValue(property->get(), value);
        if (!result.accepted) return result;
        const auto found = values_.find(path);
        if (found != values_.end() && found->second == value) return result;
        values_[path] = value;
        const eve::property_access::PropertyChange change{path, value, ++revision_};
        const auto snapshot = observers_->entries;
        for (const auto& entry : snapshot)
            if (entry.callback) entry.callback(change);
        return result;
    }
    std::uint64_t revision() const override { return revision_; }
    eve::Subscription subscribe(ChangeCallback callback) override {
        const std::uint64_t id = observers_->nextId++;
        observers_->entries.push_back({id, std::move(callback)});
        std::weak_ptr<ObserverState> weak = observers_;
        return eve::Subscription([weak, id]() {
            if (const auto state = weak.lock())
                std::erase_if(state->entries,
                              [id](const Entry& entry) { return entry.id == id; });
        });
    }

private:
    struct Entry { std::uint64_t id; ChangeCallback callback; };
    struct ObserverState { std::uint64_t nextId = 1; std::vector<Entry> entries; };
    eve::property_access::PropertySchema schema_;
    std::map<std::string, eve::Value> values_;
    std::uint64_t revision_ = 0;
    std::shared_ptr<ObserverState> observers_;
};
