#pragma once

#include "common/Result.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace eve::cap {

/** @brief Owning identity of one publication; generations are local to its registry. */
struct OwnedProviderHandle {
    std::string   id;
    std::uint64_t generation = 0;
    /** @brief Compare publication identities. */
    friend bool operator==(const OwnedProviderHandle&, const OwnedProviderHandle&) = default;
};

/**
 * @brief Instance-owned publication mechanism shared by capability and domain registries.
 * @ownership The registry and acquired records share provider ownership. Metadata is an owning value.
 * @lifetime Withdrawal prevents new acquisitions; existing records retain the old generation.
 * Registry destruction releases its publications; previously acquired records remain alive.
 * @thread Publication, acquisition, withdrawal and snapshots are synchronized. Provider callbacks
 * are never invoked under the registry mutex; their affinity remains the domain owner's contract.
 * @reentrancy Records may invoke callbacks which mutate this registry after acquisition returns.
 * @cost Publication copies an id and allocates a slot; snapshot copies all live records.
 * Bind or acquire outside per-frame inner loops and reuse the resulting owning record.
 */
template <class I, class Metadata = std::monostate>
class ProviderRegistry {
public:
    /** @brief Construct an empty registry with its own generation namespace. */
    ProviderRegistry()                                   = default;
    ProviderRegistry(const ProviderRegistry&)            = delete;
    ProviderRegistry& operator=(const ProviderRegistry&) = delete;
    /** @brief Owning lease and immutable-at-publication metadata for a generation. */
    struct Record {
        OwnedProviderHandle handle;
        std::shared_ptr<I>  provider;
        Metadata            metadata;
    };

    /** @brief Unpublish before releasing implementations; destructor reentry sees an empty, closing registry. */
    ~ProviderRegistry() {
        decltype(entries_) retired;
        {
            std::scoped_lock lock(mutex_);
            closing_ = true;
            retired.swap(entries_);
        }
    }

    /** @brief Publish a unique id. @return InvalidArgument or Conflict without changing live state. */
    [[nodiscard]] Result<OwnedProviderHandle> publish(std::string id, std::shared_ptr<I> provider,
                                                      Metadata metadata = {}) {
        if (id.empty() || !provider)
            return Result<OwnedProviderHandle>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Provider requires id and implementation", "id"));
        std::scoped_lock lock(mutex_);
        if (closing_)
            return Result<OwnedProviderHandle>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Provider registry is shutting down", "id"));
        if (entries_.contains(id))
            return Result<OwnedProviderHandle>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Provider id is already published", "id"));
        const auto previous = generations_[id];
        if (previous == std::numeric_limits<std::uint64_t>::max())
            return Result<OwnedProviderHandle>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Provider generation space is exhausted", "id"));
        const OwnedProviderHandle handle{id, previous + 1};
        entries_.emplace(id, Record{handle, std::move(provider), std::move(metadata)});
        generations_[id] = handle.generation;
        return Result<OwnedProviderHandle>::success(handle, Status::success(StatusCode::Applied));
    }

    /** @brief Acquire an exact generation. @return Unsupported when never seen, Conflict when stale. */
    [[nodiscard]] Result<Record> acquire(const OwnedProviderHandle& handle) const {
        std::scoped_lock lock(mutex_);
        const auto       found = entries_.find(handle.id);
        if (found == entries_.end() || found->second.handle != handle)
            return Result<Record>::failure(Diagnostic::error(
                generations_.contains(handle.id) ? DiagnosticCode::Conflict : DiagnosticCode::Unsupported,
                "Provider generation is absent or stale", "handle"));
        return Result<Record>::success(found->second, Status::success(StatusCode::Applied));
    }

    /** @brief Acquire the current id. @return Unsupported when absent, without creating a slot. */
    [[nodiscard]] Result<Record> acquire(std::string_view id) const {
        std::scoped_lock lock(mutex_);
        const auto       found = entries_.find(std::string(id));
        if (found == entries_.end())
            return Result<Record>::failure(
                Diagnostic::error(DiagnosticCode::Unsupported, "Provider is not available", "id"));
        return Result<Record>::success(found->second, Status::success(StatusCode::Applied));
    }

    /** @brief Withdraw an exact generation; retained leases stay valid. @return Removed owning record or Conflict. */
    [[nodiscard]] Result<Record> withdraw(const OwnedProviderHandle& handle) {
        Record removed;
        {
            std::scoped_lock lock(mutex_);
            const auto       found = entries_.find(handle.id);
            if (found == entries_.end() || found->second.handle != handle)
                return Result<Record>::failure(
                    Diagnostic::error(DiagnosticCode::Conflict, "Provider generation is stale", "handle"));
            removed = std::move(found->second);
            entries_.erase(found);
        }
        return Result<Record>::success(std::move(removed), Status::success(StatusCode::Applied));
    }

    /**
     * @brief Copy owning dispatch records in id order; later mutations affect the next snapshot.
     * @cost Copies all live providers and metadata and sorts by id (O(n log n), O(n) memory);
     * amortize once over a complete dispatch, rather than once per provider callback.
     */
    [[nodiscard]] std::vector<Record> snapshot() const {
        std::vector<Record> result;
        {
            std::scoped_lock lock(mutex_);
            result.reserve(entries_.size());
            for (const auto& [id, entry] : entries_) result.push_back(entry);
        }
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.handle.id < b.handle.id; });
        return result;
    }

private:
    bool                                           closing_ = false;
    mutable std::mutex                             mutex_;
    std::unordered_map<std::string, Record>        entries_;
    std::unordered_map<std::string, std::uint64_t> generations_;
};

}  // namespace eve::cap
