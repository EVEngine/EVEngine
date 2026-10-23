#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "rts/RTSScriptInternal.h"
#include "rts/RTSScriptRuntimeInternal.h"

namespace eve::rts {
Result<Value> RTS::inspectScriptEconomy(Faction& faction) {
    auto account = scriptAccount(faction);
    if (!account) return Result<Value>::failure(account.status());
    const auto   snapshot = scriptRuntime_->economies.at(faction.identity()->subject.format())->ledger.snapshot();
    Value::Array resources;
    const auto   count = [](const auto& counters, const auto& name) {
        const auto found = counters.find(name);
        return found == counters.end() ? 0 : found->second;
    };
    for (const auto& [name, balance] : snapshot.current)
        resources.emplace_back(Value::Object{{"resource", name},
                                             {"balance", balance},
                                             {"income", count(snapshot.income, name)},
                                             {"expense", count(snapshot.expense, name)},
                                             {"wasted", count(snapshot.wasted, name)}});
    return Result<Value>::success(Value(
        Value::Object{{"faction", faction.identity()->subject.format()}, {"resources", Value(std::move(resources))}}));
}
namespace script_internal {
void exposeEconomyBindings(ssq::Class& cls) {
    const auto vm = cls.getHandle();
    cls.addFunc("inspectScriptEconomy", [vm](RTS* self, const std::string& factionText) -> ssq::Table {
        auto subject = parseScriptSubject(factionText, "faction");
        if (!subject) return script::projectStatusResult(vm, subject.status());
        auto* faction = self->findFaction(subject.value());
        if (!faction)
            return script::projectStatusResult(
                vm, Status::failure(Diagnostic::error(DiagnosticCode::NotFound, "faction is absent", "faction")));
        return script::projectResult(vm, self->inspectScriptEconomy(*faction), [](Value value) { return value; });
    });
}
}  // namespace script_internal
}  // namespace eve::rts
