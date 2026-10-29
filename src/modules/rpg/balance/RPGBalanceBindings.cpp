#include "common/SquirrelBinding.h"
#include "rpg/balance/RPGBalance.h"

namespace eve::rpg::balance {
void RPGBalanceSuite::expose(ssq::Table& table) {
    auto cls = table.addClass<RPGBalanceSuite>(
        "RPGBalanceSuite", std::function<RPGBalanceSuite*()>([] { return new RPGBalanceSuite(); }), true);
    expose(cls);
}
void RPGBalanceSuite::expose(ssq::Class& cls) {
    const auto vm = cls.getHandle();
    cls.addFunc("configure", [vm](RPGBalanceSuite* self, ssq::Table input) {
        auto value = script::valueFromSquirrel(input);
        if (!value) return script::projectResult(vm, Result<void>::failure(value.status()));
        return script::projectResult(vm, self->configure(value.value()));
    });
    cls.addFunc("targets", [vm](RPGBalanceSuite* self) {
        return script::projectResult(vm, self->targets(), [](Value value) { return value; });
    });
    cls.addFunc("measure", [vm](RPGBalanceSuite* self, ssq::Table parameters, ssq::Array seeds, float dt, int rounds) {
        auto result = [&]() -> Result<Value> {
            auto p = script::valueFromSquirrel(parameters);
            if (!p) return Result<Value>::failure(p.status());
            auto s = script::valueFromSquirrel(seeds);
            if (!s) return Result<Value>::failure(s.status());
            return self->measure(p.value(), s.value(), dt, rounds);
        }();
        if (result) {
            const auto top     = sq_gettop(vm);
            auto       checked = script::pushValue(vm, result.value());
            sq_settop(vm, top);
            if (!checked)
                return script::projectResult(vm, Result<Value>::failure(checked.status()), [](Value v) { return v; });
        }
        return script::projectResult(vm, std::move(result), [](Value value) { return value; });
    });
}
}  // namespace eve::rpg::balance
