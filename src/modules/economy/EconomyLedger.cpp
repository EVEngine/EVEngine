#include "economy/EconomyLedger.h"

#include "economy/ResourceType.h"

#include <algorithm>

namespace eve::economy {

int EconomyLedger::credit(const std::string& type, int amount) {
    if (amount <= 0) return 0;
    const auto* def = ResourceTypeRegistry::find(type);
    const int   cap = def ? def->stockMax : 0;

    int& current  = current_[type];
    int  accepted = amount;
    if (cap > 0 && current + amount > cap) accepted = std::max(0, cap - current);
    current += accepted;
    income_[type] += accepted;
    if (accepted < amount) wasted_[type] += amount - accepted;
    return accepted;
}

bool EconomyLedger::debit(const std::string& type, int amount) {
    if (amount <= 0) return true;
    auto       it      = current_.find(type);
    const int  current = it == current_.end() ? 0 : it->second;
    if (current < amount) return false;
    it->second = current - amount;
    expense_[type] += amount;
    return true;
}

bool EconomyLedger::canAfford(const std::string& type, int amount) const {
    if (amount <= 0) return true;
    auto it = current_.find(type);
    return it != current_.end() && it->second >= amount;
}

int EconomyLedger::get(const std::string& type) const {
    auto it = current_.find(type);
    return it == current_.end() ? 0 : it->second;
}

int EconomyLedger::getCap(const std::string& type) const {
    const auto* def = ResourceTypeRegistry::find(type);
    return def ? def->stockMax : 0;
}

int EconomyLedger::getWasted(const std::string& type) const {
    auto it = wasted_.find(type);
    return it == wasted_.end() ? 0 : it->second;
}

int EconomyLedger::getIncome(const std::string& type) const {
    auto it = income_.find(type);
    return it == income_.end() ? 0 : it->second;
}

int EconomyLedger::getExpense(const std::string& type) const {
    auto it = expense_.find(type);
    return it == expense_.end() ? 0 : it->second;
}

void EconomyLedger::swap(EconomyLedger& other) noexcept {
    using std::swap;
    swap(current_, other.current_);
    swap(wasted_, other.wasted_);
    swap(income_, other.income_);
    swap(expense_, other.expense_);
}

EconomyLedger::Snapshot EconomyLedger::snapshot() const {
    return {{current_.begin(), current_.end()}, {wasted_.begin(), wasted_.end()},
            {income_.begin(), income_.end()}, {expense_.begin(), expense_.end()}};
}

void EconomyLedger::restore(const Snapshot& snapshot) {
    current_ = std::unordered_map<std::string, int>(snapshot.current.begin(), snapshot.current.end());
    wasted_ = std::unordered_map<std::string, int>(snapshot.wasted.begin(), snapshot.wasted.end());
    income_ = std::unordered_map<std::string, int>(snapshot.income.begin(), snapshot.income.end());
    expense_ = std::unordered_map<std::string, int>(snapshot.expense.begin(), snapshot.expense.end());
}

}  // namespace eve::economy
