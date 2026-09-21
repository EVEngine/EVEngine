#include "rpg/Crafting.h"

#include "inventory/Bag.h"
#include "transaction/AtomicResourcePayment.h"

#include <limits>
#include <memory>
#include <utility>

namespace eve::rpg {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

eve::Value encodeReservation(const CraftingRecipe& recipe, std::uint32_t batchSize) {
    eve::Value::Array outputs;
    outputs.reserve(recipe.outputs.size());
    for (const auto& output : recipe.outputs) {
        outputs.emplace_back(eve::Value::Object{
            {"item", eve::Value(output.itemId)},
            {"quantity", eve::Value(std::int64_t(output.quantity) * static_cast<std::int64_t>(batchSize))}});
    }
    return eve::Value(eve::Value::Object{{"schema", eve::Value("eve.rpg.crafting-reservation")},
                                         {"version", eve::Value(std::int64_t(1))},
                                         {"outputs", eve::Value(std::move(outputs))}});
}

eve::Result<eve::resource::CostSpec> scaleCost(
    const eve::resource::CostSpec& source, std::uint32_t batchSize) {
    const auto multiplier = static_cast<std::int64_t>(batchSize);
    std::vector<eve::resource::ResourceCost> items;
    items.reserve(source.items().size());
    for (const auto& item : source.items()) {
        if (item.amount.value() > std::numeric_limits<std::int64_t>::max() / multiplier)
            return failure<eve::resource::CostSpec>(eve::DiagnosticCode::InvalidArgument,
                                                    "crafting batch ingredient quantity overflows", "batchSize");
        auto scaled = eve::resource::ResourceCost::create(
            item.resource.value(), item.amount.value() * multiplier);
        if (!scaled) return eve::Result<eve::resource::CostSpec>::failure(scaled.status());
        items.push_back(std::move(scaled).takeValue());
    }
    return eve::resource::CostSpec::create(std::move(items));
}

eve::Result<std::vector<inventory::InventoryItemGrant>> decodeOutputs(const eve::Value& reservation) {
    const auto* object = reservation.getIf<eve::Value::Object>();
    if (object == nullptr)
        return failure<std::vector<inventory::InventoryItemGrant>>(
            eve::DiagnosticCode::ParseError, "crafting reservation must be an object", "reservation");
    const auto schema = object->find("schema");
    const auto version = object->find("version");
    const auto outputs = object->find("outputs");
    if (schema == object->end() || !schema->second.isString() ||
        schema->second.asString() != "eve.rpg.crafting-reservation" || version == object->end() ||
        !version->second.isInt64() || version->second.asInt() != 1 || outputs == object->end() ||
        !outputs->second.isArray())
        return failure<std::vector<inventory::InventoryItemGrant>>(
            eve::DiagnosticCode::ParseError, "crafting reservation schema is invalid", "reservation");
    std::vector<inventory::InventoryItemGrant> result;
    const auto* outputValues = outputs->second.getIf<eve::Value::Array>();
    for (const auto& value : *outputValues) {
        const auto* entry = value.getIf<eve::Value::Object>();
        if (entry == nullptr)
            return failure<std::vector<inventory::InventoryItemGrant>>(
                eve::DiagnosticCode::ParseError, "crafting output must be an object", "reservation.outputs");
        const auto item = entry->find("item");
        const auto quantity = entry->find("quantity");
        if (item == entry->end() || !item->second.isString() || item->second.asString().empty() ||
            quantity == entry->end() || !quantity->second.isInt64() || quantity->second.asInt() <= 0 ||
            quantity->second.asInt() > std::numeric_limits<int>::max())
            return failure<std::vector<inventory::InventoryItemGrant>>(
                eve::DiagnosticCode::ParseError, "crafting output fields are invalid", "reservation.outputs");
        result.push_back({item->second.asString(), static_cast<int>(quantity->second.asInt())});
    }
    if (result.empty())
        return failure<std::vector<inventory::InventoryItemGrant>>(
            eve::DiagnosticCode::InvalidArgument, "crafting reservation has no outputs", "reservation.outputs");
    return eve::Result<std::vector<inventory::InventoryItemGrant>>::success(std::move(result));
}

class ProductionParticipant final : public transaction::ITransactionParticipant {
public:
    ProductionParticipant(production::WorkQueue& queue, production::ProductionRequest request)
        : queue_(queue), request_(std::move(request)) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "rpg-crafting-production"; }

    [[nodiscard]] eve::Result<void> prepare(const transaction::TransactionContext&) override {
        auto snapshot = queue_.snapshot();
        if (!snapshot) return eve::Result<void>::failure(snapshot.status());
        before_ = std::move(snapshot).takeValue();
        staged_ = std::make_unique<production::WorkQueue>();
        auto restored = staged_->restore(before_);
        if (!restored) return restored;
        auto enqueued = staged_->enqueue(request_);
        if (!enqueued) return eve::Result<void>::failure(enqueued.status());
        taskId_ = std::move(enqueued).takeValue();
        auto after = staged_->snapshot();
        if (!after) return eve::Result<void>::failure(after.status());
        after_ = std::move(after).takeValue();
        prepared_ = true;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> commit(const transaction::TransactionContext&) override {
        if (!prepared_ || committed_)
            return failure<void>(eve::DiagnosticCode::Conflict, "crafting production is not prepared", "production");
        auto current = queue_.snapshot();
        if (!current) return eve::Result<void>::failure(current.status());
        if (current.value() != before_)
            return failure<void>(eve::DiagnosticCode::StaleHandle,
                                 "crafting production queue changed while staged", "production");
        auto restored = queue_.restore(after_);
        if (!restored) return restored;
        committed_ = true;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> rollback(const transaction::TransactionContext&) override {
        if (committed_)
            return failure<void>(eve::DiagnosticCode::Conflict, "committed crafting task requires compensation");
        staged_.reset();
        prepared_ = false;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> compensate(const transaction::TransactionContext&) override {
        if (!committed_)
            return failure<void>(eve::DiagnosticCode::Conflict, "crafting task has not committed");
        auto current = queue_.snapshot();
        if (!current) return eve::Result<void>::failure(current.status());
        if (current.value() != after_)
            return failure<void>(eve::DiagnosticCode::StaleHandle,
                                 "crafting production queue changed before compensation", "production");
        auto restored = queue_.restore(before_);
        if (!restored) return restored;
        staged_.reset();
        prepared_ = false;
        committed_ = false;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] const std::string& taskId() const noexcept { return taskId_; }

private:
    production::WorkQueue&                 queue_;
    production::ProductionRequest          request_;
    std::unique_ptr<production::WorkQueue> staged_;
    std::string                            before_;
    std::string                            after_;
    std::string                            taskId_;
    bool                                   prepared_ = false;
    bool                                   committed_ = false;
};

}  // namespace

eve::Result<CraftingReceipt> Crafting::begin(CraftingRequest request) {
    if (request.production == nullptr || request.ingredients == nullptr)
        return failure<CraftingReceipt>(eve::DiagnosticCode::InvalidArgument,
                                        "crafting requires production and ingredient account ports", "request");
    if (request.owner.empty() || request.recipe.id.empty() || request.recipe.process.empty())
        return failure<CraftingReceipt>(eve::DiagnosticCode::InvalidArgument,
                                        "crafting owner, recipe id and process are required", "request");
    if (!request.recipe.ingredients.isValid() || request.recipe.outputs.empty() || request.batchSize == 0 ||
        request.recipe.duration.nanoseconds() <= 0)
        return failure<CraftingReceipt>(eve::DiagnosticCode::InvalidArgument,
                                        "crafting recipe requires ingredients, outputs and positive duration",
                                        "recipe");
    for (const auto& output : request.recipe.outputs) {
        if (output.itemId.empty() || output.quantity <= 0 ||
            output.quantity > std::numeric_limits<int>::max() / request.batchSize)
            return failure<CraftingReceipt>(eve::DiagnosticCode::InvalidArgument,
                                            "crafting outputs require item ids and positive quantities",
                                            "recipe.outputs");
    }

    production::ProductionRequest productionRequest;
    productionRequest.owner = std::move(request.owner);
    productionRequest.kind = request.recipe.process;
    productionRequest.product = request.recipe.id;
    productionRequest.duration = request.recipe.duration;
    productionRequest.priority = request.priority;
    productionRequest.definition = request.recipe.definition;
    productionRequest.reservation = encodeReservation(request.recipe, request.batchSize);
    productionRequest.batchSize = request.batchSize;
    productionRequest.context = eve::Value(eve::Value::Object{{"recipe", eve::Value(request.recipe.id)},
                                                               {"process", eve::Value(request.recipe.process)}});
    productionRequest.settlementRequired = true;

    if (request.transactionId.empty()) request.transactionId = "rpg.crafting." + request.recipe.id;
    auto batchIngredients = scaleCost(request.recipe.ingredients, request.batchSize);
    if (!batchIngredients) return eve::Result<CraftingReceipt>::failure(batchIngredients.status());
    transaction::TransactionContext context(std::move(request.transactionId));
    ProductionParticipant participant(*request.production, std::move(productionRequest));
    auto committed = transaction::AtomicResourcePayment::execute(
        context, *request.ingredients, batchIngredients.value(), participant);
    if (!committed) return eve::Result<CraftingReceipt>::failure(committed.status());
    CraftingReceipt receipt{std::move(committed).takeValue(), participant.taskId()};
    return eve::Result<CraftingReceipt>::success(std::move(receipt),
                                                  eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<int> Crafting::settle(production::WorkQueue& queue, inventory::Bag& output,
                                  std::string_view taskId) {
    auto task = queue.find(taskId);
    if (!task)
        return failure<int>(eve::DiagnosticCode::NotFound, "crafting task was not found", "taskId");
    const std::string settlementId = "rpg.crafting:" + std::string(taskId);
    if (task->get().state == production::TaskState::Completed &&
        task->get().settlement.settlementId == settlementId)
        return eve::Result<int>::success(0, eve::Status::success(eve::StatusCode::NoOp));
    if (task->get().state != production::TaskState::ReadyToSettle &&
        task->get().state != production::TaskState::SettlementFailed)
        return failure<int>(eve::DiagnosticCode::Conflict, "crafting task is not ready to settle", "taskId");
    auto outputs = decodeOutputs(task->get().reservation);
    if (!outputs) return eve::Result<int>::failure(outputs.status());
    auto prepared = inventory::InventorySystem::prepareAddBatch(&output, outputs.value());
    if (!prepared) {
        const auto* diagnostic = prepared.error();
        auto failed = queue.failSettlement(taskId,
                                           diagnostic == nullptr ? "crafting output rejected" : diagnostic->message());
        if (!failed && task->get().state != production::TaskState::SettlementFailed)
            return eve::Result<int>::failure(failed.status());
        return eve::Result<int>::failure(prepared.status());
    }
    if (task->get().state == production::TaskState::SettlementFailed) {
        auto retried = queue.retrySettlement(taskId);
        if (!retried) return eve::Result<int>::failure(retried.status());
    }
    production::ProductionSettlementReceipt receipt;
    receipt.settlementId = settlementId;
    receipt.payload = eve::Value(eve::Value::Object{{"destination", eve::Value(output.getId())}});
    auto settled = queue.settle(taskId, std::move(receipt));
    if (!settled) return eve::Result<int>::failure(settled.status());
    auto added = inventory::InventorySystem::commitAddBatch(std::move(prepared).takeValue());
    if (!added)
        return failure<int>(eve::DiagnosticCode::InvariantViolation,
                            "prepared crafting output failed after queue settlement", "output");
    return added;
}

}  // namespace eve::rpg
