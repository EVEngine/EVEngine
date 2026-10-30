#include "agent/BalanceCodec.h"
#include "common/SquirrelBinding.h"

#include <cmath>

namespace eve::agent::balance {
namespace {
struct StackScope {
    HSQUIRRELVM vm;
    SQInteger   top;
    explicit StackScope(HSQUIRRELVM v) : vm(v), top(sq_gettop(v)) {}
    ~StackScope() { sq_settop(vm, top); }
};

class ScriptEvaluator final : public IEvaluator {
public:
    ScriptEvaluator(const ssq::Table& table, const Problem& problem) : table_(table), problem_(problem) {}
    Result<std::vector<double>> evaluate(std::span<const double> parameters, std::uint64_t seed, double dt,
                                         std::uint32_t steps) override {
        const auto vm = table_.getHandle();
        StackScope stack(vm);
        sq_pushobject(vm, table_.getRaw());
        sq_pushstring(vm, "evaluate", -1);
        if (SQ_FAILED(sq_get(vm, -2))) return fail("Missing evaluate(parameters, seed, dt, steps) method");
        HSQOBJECT function;
        sq_getstackobj(vm, -1, &function);
        Value::Object values;
        for (std::size_t i = 0; i < parameters.size(); ++i)
            values.emplace(problem_.parameters[i].name, Value(parameters[i]));
        auto pushed = script::pushValue(vm, Value(std::move(values)));
        if (!pushed) return Result<std::vector<double>>::failure(pushed.status());
        const auto seedText = std::to_string(seed);
        HSQOBJECT  arguments;
        sq_getstackobj(vm, -1, &arguments);
        if (SQ_FAILED(
                script::callFunctionResult(vm, function, table_, arguments, seedText, SQFloat(dt), SQInteger(steps)))) {
            sq_getlasterror(vm);
            const SQChar* error     = nullptr;
            const auto    converted = sq_getstring(vm, -1, &error);
            return fail(SQ_SUCCEEDED(converted) && error ? error : "Balance evaluator callback failed");
        }
        auto result = script::valueFromSquirrel(vm, -1);
        if (!result) return Result<std::vector<double>>::failure(result.status());
        const auto* metrics = result.value().getIf<Value::Object>();
        if (!metrics || metrics->size() != problem_.targets.size())
            return fail("Return exactly the named target metrics");
        std::vector<double> numbers;
        for (const auto& target : problem_.targets) {
            auto it = metrics->find(target.name);
            if (it == metrics->end()) return fail("Missing metric: " + target.name);
            if (const auto* d = it->second.getIf<double>())
                numbers.push_back(*d);
            else if (const auto* n = it->second.getIf<std::int64_t>())
                numbers.push_back(double(*n));
            else
                return fail("Metric is not numeric: " + target.name);
        }
        return Result<std::vector<double>>::success(std::move(numbers));
    }

private:
    Result<std::vector<double>> fail(std::string message) {
        return Result<std::vector<double>>::failure(
            Diagnostic::error(DiagnosticCode::CallbackFailure, std::move(message), {}, {}, "agent.balance.script"));
    }
    ssq::Table     table_;
    const Problem& problem_;
};
}  // namespace

void BalanceSearch::expose(ssq::Table& table) {
    auto cls = table.addClass<BalanceSearch>("BalanceSearch",
                                             std::function<BalanceSearch*()>([] { return new BalanceSearch(); }), true);
    expose(cls);
}

void BalanceSearch::expose(ssq::Class& cls) {
    const auto vm = cls.getHandle();
    cls.addFunc("configure", [vm](BalanceSearch* self, ssq::Table input) {
        auto value = script::valueFromSquirrel(input);
        if (!value) return script::projectResult(vm, Result<void>::failure(value.status()));
        auto problem = decodeProblem(value.value());
        if (!problem) return script::projectResult(vm, Result<void>::failure(problem.status()));
        return script::projectResult(vm, self->configure(problem.value()));
    });
    cls.addFunc("run", [vm](BalanceSearch* self, ssq::Table evaluator) {
        Result<Value> result = [&]() -> Result<Value> {
            if (!self->problem_)
                return Result<Value>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "Configure the balance search before run"));
            ScriptEvaluator adapter(evaluator, *self->problem_);
            auto            report = self->run(adapter);
            if (!report) return Result<Value>::failure(report.status());
            return Result<Value>::success(encodeBalanceReport(report.value()));
        }();
        if (result) {
            StackScope stack(vm);
            auto       checked = script::pushValue(vm, result.value());
            if (!checked)
                return script::projectResult(vm, Result<Value>::failure(checked.status()), [](Value v) { return v; });
        }
        return script::projectResult(vm, std::move(result), [](Value v) { return v; });
    });
}
}  // namespace eve::agent::balance
