#include "agent/AgentModule.h"
#include "agent/Codec.h"
#include "common/SquirrelBinding.h"

#include <algorithm>
#include <exception>

namespace eve::agent {
namespace {
template <class T>
Result<T> bindingError(DiagnosticCode code, std::string text) {
    return Result<T>::failure(Diagnostic::error(code, std::move(text), {}, {}, "agent.script"));
}
struct StackScope {
    HSQUIRRELVM vm;
    SQInteger   top;
    explicit StackScope(HSQUIRRELVM v) : vm(v), top(sq_gettop(v)) {}
    ~StackScope() { sq_settop(vm, top); }
};
struct ActiveScope {
    bool& active;
    explicit ActiveScope(bool& value) : active(value) { active = true; }
    ~ActiveScope() { active = false; }
};

// Script references live only for this synchronous adapter call. Every callback
// restores the VM stack, including missing methods and thrown script errors.
class ScriptEnvironment final : public IEnvironment {
public:
    explicit ScriptEnvironment(const ssq::Table& environment) : environment_(environment) {}
    Result<Observation> reset(std::uint64_t seed) override { return invoke("reset", seed, 0, 0); }
    Result<Observation> step(std::uint32_t action, double dt) override { return invoke("step", 0, action, dt); }

private:
    Result<Observation> invoke(const char* method, std::uint64_t seed, std::uint32_t action, double dt) {
        auto       vm = environment_.getHandle();
        StackScope stack(vm);
        sq_pushobject(vm, environment_.getRaw());
        sq_pushstring(vm, method, -1);
        if (SQ_FAILED(sq_get(vm, -2)))
            return bindingError<Observation>(DiagnosticCode::NotFound, "Missing environment method");
        sq_pushobject(vm, environment_.getRaw());
        const bool resetting = std::string_view(method) == "reset";
        if (resetting) {
            const auto text = std::to_string(seed);
            sq_pushstring(vm, text.c_str(), SQInteger(text.size()));
        } else {
            sq_pushinteger(vm, SQInteger(action));
            sq_pushfloat(vm, SQFloat(dt));
        }
        if (SQ_FAILED(sq_call(vm, resetting ? 2 : 3, SQTrue, SQFalse))) {
            sq_getlasterror(vm);
            const SQChar* text      = nullptr;
            const auto    converted = sq_getstring(vm, -1, &text);
            return bindingError<Observation>(DiagnosticCode::CallbackFailure,
                                             SQ_SUCCEEDED(converted) && text ? text : "Environment callback failed");
        }
        auto value = script::valueFromSquirrel(vm, -1);
        if (!value) return Result<Observation>::failure(value.status());
        return decodeObservation(value.value());
    }
    ssq::Table environment_;
};

ssq::Table project(HSQUIRRELVM vm, Result<Value> result) {
    // The common projector has a bounded payload. Check first so oversized
    // reports return a failure instead of an ok result with a null payload.
    if (result) {
        StackScope stack(vm);
        auto       checked = script::pushValue(vm, result.value());
        if (!checked)
            return script::projectResult(vm, Result<Value>::failure(checked.status()), [](Value v) { return v; });
    }
    return script::projectResult(vm, std::move(result), [](Value v) { return v; });
}
Result<Value> inferScript(const ssq::Object& policy, const ssq::Object& observation, Backend backend, bool actionOnly) {
    auto pv = script::valueFromSquirrel(policy);
    if (!pv) return Result<Value>::failure(pv.status());
    auto ov = script::valueFromSquirrel(observation);
    if (!ov) return Result<Value>::failure(ov.status());
    auto p = decodePolicy(pv.value());
    if (!p) return Result<Value>::failure(p.status());
    auto o = decodeObservation(ov.value());
    if (!o) return Result<Value>::failure(o.status());
    auto result = infer(p.value(), o.value(), backend);
    if (!result) return Result<Value>::failure(result.status());
    if (actionOnly) {
        const auto& probabilities = result.value();
        const auto  action = *std::max_element(o.value().legalActions.begin(), o.value().legalActions.end(),
                                               [&](auto a, auto b) { return probabilities[a] < probabilities[b]; });
        return Result<Value>::success(Value(std::int64_t(action)));
    }
    Value::Array values;
    for (auto n : result.value()) values.emplace_back(n);
    return Result<Value>::success(Value(std::move(values)));
}
}  // namespace

void Agent::expose(ssq::Class& cls) {
    const auto vm = cls.getHandle();
    cls.addFunc("getName", &Agent::getName);
    cls.addFunc("run", [vm](Agent* self, ssq::Table config, ssq::Table environment) {
        if (self->active_)
            return project(vm, bindingError<Value>(DiagnosticCode::Conflict, "Agent run/replay reentry"));
        ActiveScope active(self->active_);
        auto        value = script::valueFromSquirrel(config);
        if (!value) return project(vm, Result<Value>::failure(value.status()));
        auto parsed = decodeConfig(value.value());
        if (!parsed) return project(vm, Result<Value>::failure(parsed.status()));
        ScriptEnvironment adapter(environment);
        auto              result = run(parsed.value(), adapter);
        if (!result) return project(vm, Result<Value>::failure(result.status()));
        return project(vm, Result<Value>::success(encodeReport(result.value())));
    });
    cls.addFunc("infer", [vm](Agent*, ssq::Table policy, ssq::Table observation) {
        return project(vm, inferScript(policy, observation, Backend::Cpu, false));
    });
    cls.addFunc("inferTensor", [vm](Agent*, ssq::Table policy, ssq::Table observation) {
        return project(vm, inferScript(policy, observation, Backend::Tensor, false));
    });
    cls.addFunc("inferGpu", [vm](Agent*, ssq::Table policy, ssq::Table observation) {
        return project(vm, inferScript(policy, observation, Backend::Gpu, false));
    });
    cls.addFunc("act", [vm](Agent*, ssq::Table policy, ssq::Table observation) {
        return project(vm, inferScript(policy, observation, Backend::Cpu, true));
    });
    cls.addFunc("actTensor", [vm](Agent*, ssq::Table policy, ssq::Table observation) {
        return project(vm, inferScript(policy, observation, Backend::Tensor, true));
    });
    cls.addFunc("actGpu", [vm](Agent*, ssq::Table policy, ssq::Table observation) {
        return project(vm, inferScript(policy, observation, Backend::Gpu, true));
    });
    cls.addFunc("replay", [vm](Agent* self, ssq::Table trace, ssq::Table environment, float tolerance) {
        if (self->active_)
            return script::projectResult(vm, bindingError<void>(DiagnosticCode::Conflict, "Agent run/replay reentry"));
        ActiveScope active(self->active_);
        auto        value = script::valueFromSquirrel(trace);
        if (!value) return script::projectResult(vm, Result<void>::failure(value.status()));
        auto parsed = decodeTrace(value.value());
        if (!parsed) return script::projectResult(vm, Result<void>::failure(parsed.status()));
        ScriptEnvironment adapter(environment);
        return script::projectResult(vm, replay(parsed.value(), adapter, tolerance));
    });
    cls.addFunc("getBackends", [vm](Agent*) {
        Value::Array names{Value("cpu-mlp")};
        auto         tensor = backendName(Backend::Tensor);
        if (tensor) names.emplace_back(tensor.value());
        auto gpu = backendName(Backend::Gpu);
        if (gpu) names.emplace_back(gpu.value());
        return project(vm, Result<Value>::success(Value(std::move(names))));
    });
}
}  // namespace eve::agent
