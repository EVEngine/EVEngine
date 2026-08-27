#include "common/ResourceAccount.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <limits>
#include <utility>

namespace eve::resource {
namespace {

std::atomic<std::uint64_t>& nextAccountNonce() {
    static std::atomic<std::uint64_t> next{1};
    return next;
}

eve::Diagnostic invalidArgument(std::string message, std::string path = {}) {
    return eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move(message), std::move(path));
}

eve::Diagnostic invariantFailure(std::string message, std::string path = {}) {
    return eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, std::move(message), std::move(path));
}

}  // namespace

eve::Result<AccountNonce> allocateAccountNonce() {
    auto&         counter = nextAccountNonce();
    std::uint64_t value   = counter.load(std::memory_order_relaxed);
    for (;;) {
        if (value == 0 || value == std::numeric_limits<std::uint64_t>::max())
            return eve::Result<AccountNonce>::failure(invariantFailure("resource account nonce exhausted", "account"));
        if (counter.compare_exchange_weak(value, value + 1, std::memory_order_relaxed, std::memory_order_relaxed))
            return eve::Result<AccountNonce>::success(AccountNonce(value));
    }
}

eve::Result<ResourceId> ResourceId::parse(std::string_view text) {
    if (text.empty())
        return eve::Result<ResourceId>::failure(invalidArgument("resource id must not be empty", "resource"));
    if (text.size() > 128)
        return eve::Result<ResourceId>::failure(invalidArgument("resource id is too long", "resource"));
    for (const unsigned char character : text) {
        if (std::iscntrl(character) || std::isspace(character))
            return eve::Result<ResourceId>::failure(
                invalidArgument("resource id must not contain whitespace or control characters", "resource"));
    }
    return eve::Result<ResourceId>::success(ResourceId(std::string(text)));
}

eve::Result<Amount> Amount::from(std::int64_t value) {
    if (value < 0)
        return eve::Result<Amount>::failure(invalidArgument("resource amount must not be negative", "amount"));
    return eve::Result<Amount>::success(Amount(value));
}

eve::Result<Amount> Amount::checkedAdd(Amount other) const {
    if (other.value_ > std::numeric_limits<std::int64_t>::max() - value_)
        return eve::Result<Amount>::failure(invariantFailure("resource amount addition overflowed", "amount"));
    return eve::Result<Amount>::success(Amount(value_ + other.value_));
}

eve::Result<ResourceCost> ResourceCost::create(std::string_view resourceId, std::int64_t amountValue) {
    auto resource = ResourceId::parse(resourceId);
    if (!resource) return eve::Result<ResourceCost>::failure(resource.status());
    auto amount = Amount::from(amountValue);
    if (!amount) return eve::Result<ResourceCost>::failure(amount.status());
    if (amount.value().isZero())
        return eve::Result<ResourceCost>::failure(invalidArgument("resource cost amount must be positive", "amount"));
    return eve::Result<ResourceCost>::success(
        ResourceCost{std::move(resource).takeValue(), std::move(amount).takeValue()});
}

eve::Result<CostSpec> CostSpec::create(std::vector<ResourceCost> items) {
    if (items.empty())
        return eve::Result<CostSpec>::failure(invalidArgument("resource cost must contain at least one item", "cost"));
    for (const auto& item : items) {
        if (!item.resource.isValid())
            return eve::Result<CostSpec>::failure(
                invalidArgument("resource cost contains an invalid resource id", "cost.resource"));
        if (item.amount.value() <= 0)
            return eve::Result<CostSpec>::failure(
                invalidArgument("resource cost amounts must be positive", "cost.amount"));
    }

    std::sort(items.begin(), items.end(),
              [](const auto& left, const auto& right) { return left.resource.value() < right.resource.value(); });

    std::vector<ResourceCost> canonical;
    canonical.reserve(items.size());
    for (auto& item : items) {
        if (canonical.empty() || canonical.back().resource != item.resource) {
            canonical.push_back(std::move(item));
            continue;
        }
        auto combined = canonical.back().amount.checkedAdd(item.amount);
        if (!combined) return eve::Result<CostSpec>::failure(combined.status());
        canonical.back().amount = std::move(combined).takeValue();
    }
    return eve::Result<CostSpec>::success(CostSpec(std::move(canonical)));
}

eve::Result<CostSpec> CostSpec::from(std::initializer_list<CostInput> items) {
    if (items.size() == 0)
        return eve::Result<CostSpec>::failure(invalidArgument("resource cost must contain at least one item", "cost"));

    std::vector<ResourceCost> parsed;
    parsed.reserve(items.size());
    for (const auto& input : items) {
        auto item = ResourceCost::create(input.resource, input.amount);
        if (!item) return eve::Result<CostSpec>::failure(item.status());
        parsed.push_back(std::move(item).takeValue());
    }
    return create(std::move(parsed));
}

eve::Result<CostSpec> CostSpec::single(std::string_view resource, std::int64_t amount) {
    return from({CostInput{resource, amount}});
}

}  // namespace eve::resource
