#include "transaction/AtomicBatch.h"
#include <map>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "transaction/AtomicResourcePayment.h"

namespace eve::transaction {
namespace {
int ownerSlot;
}
struct AtomicBatch::Impl {
    std::vector<std::unique_ptr<ITransactionParticipant>> effects;
    std::vector<std::function<Result<void>()>>            guards;
    std::optional<resource::CostSpec>                     cost;
    std::shared_ptr<resource::IResourceAccount>           accountOwner;
    resource::IResourceAccount*                           account = nullptr;
    std::optional<Status>                                 failure;
    std::map<std::string, resource::ResourceCost>         floors;
    bool                                                  consumed  = false;
    bool                                                  executing = false;
    std::function<std::optional<Value>(std::string_view)> query;
};
AtomicBatch::AtomicBatch() : impl_(std::make_unique<Impl>()) {}
AtomicBatch::~AtomicBatch() = default;
Result<void> AtomicBatch::reject(Status status) {
    if (!impl_->failure) impl_->failure = std::move(status);
    return Result<void>::failure(*impl_->failure);
}
Result<void> AtomicBatch::bindAccount(resource::IResourceAccount&                           account,
                                      std::function<std::optional<Value>(std::string_view)> query) {
    if (impl_->consumed || impl_->executing || (impl_->account && impl_->account != &account))
        return reject(Status::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "batch is consumed or has a different payer", "batch.account")));
    impl_->account = &account;
    if (query) impl_->query = std::move(query);
    if (impl_->failure) return Result<void>::failure(*impl_->failure);
    return Result<void>::success();
}
Result<void> AtomicBatch::bindAccount(std::shared_ptr<resource::IResourceAccount>           account,
                                      std::function<std::optional<Value>(std::string_view)> query) {
    if (!account)
        return reject(
            Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "account is null", "account")));
    auto bound = bindAccount(*account, std::move(query));
    if (!bound) return bound;
    impl_->accountOwner = std::move(account);
    return Result<void>::success();
}
Result<resource::CostSpec> AtomicBatch::parseCost(const std::string& json) {
    auto value = Value::fromJson(json);
    if (!value) return Result<resource::CostSpec>::failure(value.status());
    const auto* object = value.value().getIf<Value::Object>();
    if (!object)
        return Result<resource::CostSpec>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "cost must be an object", "cost"));
    std::vector<resource::ResourceCost> items;
    for (const auto& [name, amount] : *object) {
        const auto* integer = amount.getIf<std::int64_t>();
        if (!integer)
            return Result<resource::CostSpec>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "cost must be integral", "cost"));
        auto item = resource::ResourceCost::create(name, *integer);
        if (!item) return Result<resource::CostSpec>::failure(item.status());
        items.push_back(std::move(item).takeValue());
    }
    return resource::CostSpec::create(std::move(items));
}
Result<void> AtomicBatch::guard(std::function<Result<void>()> validate) {
    if (impl_->consumed || impl_->executing || !validate)
        return reject(Status::failure(Diagnostic::error(DiagnosticCode::Conflict, "cannot add batch guard", "batch")));
    if (impl_->failure) return Result<void>::failure(*impl_->failure);
    impl_->guards.push_back(std::move(validate));
    return Result<void>::success();
}
Result<void> AtomicBatch::protect(resource::ResourceCost floor) {
    if (impl_->consumed || impl_->executing)
        return reject(Status::failure(Diagnostic::error(DiagnosticCode::Conflict, "batch is consumed", "batch")));
    if (impl_->failure) return Result<void>::failure(*impl_->failure);
    if (!floor.resource.isValid() || floor.amount.isZero())
        return reject(
            Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid resource floor", "batch")));
    const auto name  = std::string(floor.resource.value());
    const auto found = impl_->floors.find(name);
    if (found == impl_->floors.end() || found->second.amount.value() < floor.amount.value())
        impl_->floors.insert_or_assign(name, std::move(floor));
    return Result<void>::success();
}
Result<void> AtomicBatch::add(std::vector<std::unique_ptr<ITransactionParticipant>> effects,
                              std::optional<resource::CostSpec>                     cost) {
    if (impl_->consumed || impl_->executing)
        return reject(Status::failure(Diagnostic::error(DiagnosticCode::Conflict, "batch is consumed", "batch")));
    if (impl_->failure) return Result<void>::failure(*impl_->failure);
    for (const auto& effect : effects)
        if (!effect)
            return reject(Status::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "batch effect is null", "batch.effects")));
    if (cost) {
        auto items = cost->items();
        if (impl_->cost) items.insert(items.end(), impl_->cost->items().begin(), impl_->cost->items().end());
        auto merged = resource::CostSpec::create(std::move(items));
        if (!merged) return reject(merged.status());
        impl_->cost = std::move(merged).takeValue();
    }
    for (auto& effect : effects) impl_->effects.push_back(std::move(effect));
    return Result<void>::success();
}
std::optional<Value> AtomicBatch::resource(std::string_view name) const {
    if (!impl_->account || impl_->consumed) return std::nullopt;
    for (const auto& guard : impl_->guards) {
        auto valid = guard();
        if (!valid) return std::nullopt;
    }
    return impl_->query ? impl_->query(name) : std::nullopt;
}
Result<void> AtomicBatch::retainScriptOwner(void* handle) {
    if (impl_->consumed || impl_->executing)
        return reject(Status::failure(Diagnostic::error(DiagnosticCode::Conflict, "batch is consumed", "batch")));
    auto       vm  = static_cast<HSQUIRRELVM>(handle);
    const auto top = sq_gettop(vm);
    sq_push(vm, 2);
    sq_pushuserpointer(vm, &ownerSlot);
    if (SQ_FAILED(sq_get(vm, -2))) {
        sq_settop(vm, top);
        return reject(
            Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "batch owner slot absent", "batch")));
    }
    if (sq_gettype(vm, -1) == OT_NULL) {
        sq_pop(vm, 1);
        sq_pushuserpointer(vm, &ownerSlot);
        sq_newarray(vm, 0);
        sq_set(vm, -3);
        sq_pushuserpointer(vm, &ownerSlot);
        sq_get(vm, -2);
    }
    sq_push(vm, 1);
    if (SQ_FAILED(sq_arrayappend(vm, -2))) {
        sq_settop(vm, top);
        return reject(
            Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "cannot retain domain owner", "batch")));
    }
    sq_settop(vm, top);
    return Result<void>::success();
}
Result<TransactionReceipt> AtomicBatch::commit() {
    if (impl_->consumed || impl_->executing)
        return Result<TransactionReceipt>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "batch is consumed", "batch"));
    // Keep condition queries available through prepare, then release all borrowed roots.
    impl_->executing = true;
    auto result      = [&]() -> Result<TransactionReceipt> {
        if (impl_->failure) return Result<TransactionReceipt>::failure(*impl_->failure);
        for (const auto& guard : impl_->guards) {
            auto valid = guard();
            if (!valid) return Result<TransactionReceipt>::failure(valid.status());
        }
        if (impl_->effects.empty() && !impl_->cost)
            return Result<TransactionReceipt>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "batch has no effects", "batch"));
        if (!impl_->floors.empty()) {
            if (!impl_->account)
                return Result<TransactionReceipt>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "protected batch has no payer", "batch"));
            auto items = impl_->cost ? impl_->cost->items() : std::vector<resource::ResourceCost>{};
            for (const auto& [name, floor] : impl_->floors) items.push_back(floor);
            auto cost = resource::CostSpec::create(std::move(items));
            if (!cost) return Result<TransactionReceipt>::failure(cost.status());
            auto affordable = impl_->account->canAfford(cost.value());
            if (!affordable) return Result<TransactionReceipt>::failure(affordable.status());
            if (!affordable.value().isAffordable())
                return Result<TransactionReceipt>::failure(
                    Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                           "merged payment would consume protected resources", "batch"));
        }
        std::vector<ITransactionParticipant*> effects;
        for (auto& effect : impl_->effects) effects.push_back(effect.get());
        TransactionContext context("script.atomic");
        if (impl_->cost) {
            if (!impl_->account)
                return Result<TransactionReceipt>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "paid batch has no payer", "batch.account"));
            return AtomicResourcePayment::execute(context, *impl_->account, *impl_->cost, effects);
        }
        return AtomicResourcePayment::execute(context, effects);
    }();
    impl_->consumed  = true;
    impl_->executing = false;
    impl_->query     = {};
    impl_->effects.clear();
    impl_->guards.clear();
    impl_->account = nullptr;
    impl_->accountOwner.reset();
    return result;
}
namespace script_internal {
void exposeAtomicBatch(ssq::Table& table, ssq::Class& module) {
    auto       cls = table.addClass("AtomicBatch", ssq::Class::Ctor<AtomicBatch()>(), true);
    const auto vm  = table.getHandle();
    sq_pushobject(vm, cls.getRaw());
    sq_pushuserpointer(vm, &ownerSlot);
    sq_pushnull(vm);
    sq_newslot(vm, -3, SQFalse);
    sq_pop(vm, 1);
    cls.addFunc("commit", [vm](AtomicBatch* batch) {
        auto       result = batch->commit();
        const auto top    = sq_gettop(vm);
        sq_push(vm, 1);
        sq_pushuserpointer(vm, &ownerSlot);
        if (SQ_SUCCEEDED(sq_get(vm, -2)) && sq_gettype(vm, -1) == OT_ARRAY) sq_arrayresize(vm, -1, 0);
        sq_settop(vm, top);
        return script::projectResult(vm, std::move(result), [](TransactionReceipt receipt) {
            return Value(Value::Object{{"transactionId", receipt.transactionId},
                                       {"participants", static_cast<std::int64_t>(receipt.participantCount)}});
        });
    });
    module.addFunc("newAtomicBatch", [vm](Transaction*) {
        const auto top = sq_gettop(vm);
        sq_pushroottable(vm);
        sq_pushstring(vm, "eve", -1);
        if (SQ_FAILED(sq_get(vm, -2))) {
            sq_settop(vm, top);
            throw ssq::RuntimeException("eve table is absent");
        }
        sq_pushstring(vm, "AtomicBatch", -1);
        if (SQ_FAILED(sq_get(vm, -2))) {
            sq_settop(vm, top);
            throw ssq::RuntimeException("AtomicBatch class is absent");
        }
        sq_pushroottable(vm);
        if (SQ_FAILED(sq_call(vm, 1, SQTrue, SQTrue))) {
            sq_settop(vm, top);
            throw ssq::RuntimeException("cannot create atomic batch");
        }
        ssq::Instance instance(vm);
        sq_getstackobj(vm, -1, &instance.getRaw());
        sq_addref(vm, &instance.getRaw());
        sq_settop(vm, top);
        return instance;
    });
}
}  // namespace script_internal
}  // namespace eve::transaction
