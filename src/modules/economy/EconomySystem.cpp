#include "economy/EconomySystem.h"

#include "common/Assert.h"

#include <utility>

namespace eve::economy {

std::unordered_map<int, EconomyLedger>& EconomySystem::ledgers() {
    static std::unordered_map<int, EconomyLedger> s_ledgers;
    return s_ledgers;
}

std::vector<EconomyEvent>& EconomySystem::events() {
    static std::vector<EconomyEvent> s_events;
    return s_events;
}

std::unordered_map<std::string, EconomyHook>& EconomySystem::hooks() {
    static std::unordered_map<std::string, EconomyHook> s_hooks;
    return s_hooks;
}

EconomyLedger& EconomySystem::ledger(int player) { return ledgers()[player]; }

void EconomySystem::clear() {
    ledgers().clear();
    events().clear();
    hooks().clear();
}

int EconomySystem::credit(int player, const std::string& type, int amount) {
    const int accepted = ledger(player).credit(type, amount);
    emit(EconomyEvent{player, type, "credit", accepted});
    const int wasted = amount - accepted;
    if (wasted > 0) emit(EconomyEvent{player, type, "waste", wasted});
    return accepted;
}

bool EconomySystem::debit(int player, const std::string& type, int amount) {
    if (!ledger(player).debit(type, amount)) return false;
    emit(EconomyEvent{player, type, "debit", amount});
    return true;
}

int EconomySystem::get(int player, const std::string& type) { return ledger(player).get(type); }

int EconomySystem::getCap(int player, const std::string& type) { return ledger(player).getCap(type); }

int EconomySystem::getWasted(int player, const std::string& type) { return ledger(player).getWasted(type); }

int EconomySystem::getIncome(int player, const std::string& type) { return ledger(player).getIncome(type); }

int EconomySystem::getExpense(int player, const std::string& type) { return ledger(player).getExpense(type); }

void EconomySystem::registerHook(const std::string& name, EconomyHook fn) {
    EV_PARAM_CHECK(!name.empty(), "hook name must not be empty");
    hooks()[name] = std::move(fn);
}

void EconomySystem::unregisterHook(const std::string& name) { hooks().erase(name); }

void EconomySystem::clearEvents() { events().clear(); }

int EconomySystem::eventCount() { return static_cast<int>(events().size()); }

const EconomyEvent& EconomySystem::eventAt(int index) {
    const bool valid = index >= 0 && size_t(index) < events().size();
    EV_PARAM_CHECK(valid, "event index out of range");
    return events()[static_cast<size_t>(index)];
}

void EconomySystem::emit(const EconomyEvent& ev) {
    events().push_back(ev);
    for (auto& [name, fn] : hooks()) {
        (void)name;
        fn(ev);
    }
}

}  // namespace eve::economy
