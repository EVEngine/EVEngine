#include "rpg/EquipmentUpgrade.h"

#include "inventory/Bag.h"
#include "inventory/Item.h"
#include "transaction/AtomicResourcePayment.h"

#include <array>
#include <cctype>
#include <charconv>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>

namespace eve::rpg {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

eve::Value encodeReservation(const EquipmentUpgradeRecipe& recipe, int bagSlot, int instanceId) {
    return eve::Value(eve::Value::Object{
        {"schema", eve::Value("eve.rpg.upgrade-reservation")},
        {"version", eve::Value(std::int64_t(1))},
        {"itemId", eve::Value(recipe.itemId)},
        {"fromLevel", eve::Value(std::int64_t(recipe.fromLevel))},
        {"toLevel", eve::Value(std::int64_t(recipe.toLevel))},
        {"bagSlot", eve::Value(std::int64_t(bagSlot))},
        {"instanceId", eve::Value(std::int64_t(instanceId))},
    });
}

struct DecodedUpgradeReservation {
    std::string itemId;
    int         fromLevel  = 0;
    int         toLevel    = 1;
    int         bagSlot    = -1;
    int         instanceId = 0;
};

eve::Result<DecodedUpgradeReservation> decodeReservation(const eve::Value& reservation) {
    const auto* object = reservation.getIf<eve::Value::Object>();
    if (object == nullptr)
        return failure<DecodedUpgradeReservation>(eve::DiagnosticCode::ParseError,
                                                  "upgrade reservation must be an object", "reservation");
    const auto schema  = object->find("schema");
    const auto version = object->find("version");
    if (schema == object->end() || !schema->second.isString() ||
        schema->second.asString() != "eve.rpg.upgrade-reservation" || version == object->end() ||
        !version->second.isInt64() || version->second.asInt() != 1)
        return failure<DecodedUpgradeReservation>(eve::DiagnosticCode::ParseError,
                                                  "upgrade reservation schema is invalid", "reservation");
    const auto itemId     = object->find("itemId");
    const auto fromLevel  = object->find("fromLevel");
    const auto toLevel    = object->find("toLevel");
    const auto bagSlot    = object->find("bagSlot");
    const auto instanceId = object->find("instanceId");
    if (itemId == object->end() || !itemId->second.isString() || itemId->second.asString().empty() ||
        fromLevel == object->end() || !fromLevel->second.isInt64() || toLevel == object->end() ||
        !toLevel->second.isInt64() || bagSlot == object->end() || !bagSlot->second.isInt64() ||
        instanceId == object->end() || !instanceId->second.isInt64())
        return failure<DecodedUpgradeReservation>(eve::DiagnosticCode::ParseError,
                                                  "upgrade reservation fields are invalid", "reservation");
    DecodedUpgradeReservation decoded;
    decoded.itemId     = itemId->second.asString();
    decoded.fromLevel  = static_cast<int>(fromLevel->second.asInt());
    decoded.toLevel    = static_cast<int>(toLevel->second.asInt());
    decoded.bagSlot    = static_cast<int>(bagSlot->second.asInt());
    decoded.instanceId = static_cast<int>(instanceId->second.asInt());
    if (decoded.fromLevel < 0 || decoded.toLevel <= decoded.fromLevel || decoded.bagSlot < 0 || decoded.instanceId <= 0)
        return failure<DecodedUpgradeReservation>(eve::DiagnosticCode::InvalidArgument,
                                                  "upgrade reservation levels or identity are invalid", "reservation");
    return eve::Result<DecodedUpgradeReservation>::success(std::move(decoded));
}

class ProductionParticipant final : public transaction::ITransactionParticipant {
public:
    ProductionParticipant(production::WorkQueue& queue, production::ProductionRequest request)
        : queue_(queue), request_(std::move(request)) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "rpg-upgrade-production"; }

    [[nodiscard]] eve::Result<void> prepare(const transaction::TransactionContext&) override {
        auto snapshot = queue_.snapshot();
        if (!snapshot) return eve::Result<void>::failure(snapshot.status());
        before_       = std::move(snapshot).takeValue();
        staged_       = std::make_unique<production::WorkQueue>();
        auto restored = staged_->restore(before_);
        if (!restored) return restored;
        auto enqueued = staged_->enqueue(request_);
        if (!enqueued) return eve::Result<void>::failure(enqueued.status());
        taskId_    = std::move(enqueued).takeValue();
        auto after = staged_->snapshot();
        if (!after) return eve::Result<void>::failure(after.status());
        after_    = std::move(after).takeValue();
        prepared_ = true;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> commit(const transaction::TransactionContext&) override {
        if (!prepared_ || committed_)
            return failure<void>(eve::DiagnosticCode::Conflict, "upgrade production is not prepared", "production");
        auto current = queue_.snapshot();
        if (!current) return eve::Result<void>::failure(current.status());
        if (current.value() != before_)
            return failure<void>(eve::DiagnosticCode::StaleHandle, "upgrade production queue changed while staged",
                                 "production");
        auto restored = queue_.restore(after_);
        if (!restored) return restored;
        committed_ = true;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> rollback(const transaction::TransactionContext&) override {
        if (committed_)
            return failure<void>(eve::DiagnosticCode::Conflict, "committed upgrade task requires compensation");
        staged_.reset();
        prepared_ = false;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> compensate(const transaction::TransactionContext&) override {
        if (!committed_) return failure<void>(eve::DiagnosticCode::Conflict, "upgrade task has not committed");
        auto current = queue_.snapshot();
        if (!current) return eve::Result<void>::failure(current.status());
        if (current.value() != after_)
            return failure<void>(eve::DiagnosticCode::StaleHandle,
                                 "upgrade production queue changed before compensation", "production");
        auto restored = queue_.restore(before_);
        if (!restored) return restored;
        staged_.reset();
        prepared_  = false;
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
    bool                                   prepared_  = false;
    bool                                   committed_ = false;
};

class SettlementParticipant final : public transaction::ITransactionParticipant {
public:
    SettlementParticipant(production::WorkQueue& queue, std::string taskId,
                          production::ProductionSettlementReceipt receipt)
        : queue_(queue), taskId_(std::move(taskId)), receipt_(std::move(receipt)) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "rpg-upgrade-settlement"; }

    [[nodiscard]] eve::Result<void> prepare(const transaction::TransactionContext&) override {
        auto before = queue_.snapshot();
        if (!before) return eve::Result<void>::failure(before.status());
        before_ = std::move(before).takeValue();
        production::WorkQueue staged;
        auto                  restored = staged.restore(before_);
        if (!restored) return restored;
        const auto task = staged.find(taskId_);
        if (!task) return failure<void>(eve::DiagnosticCode::NotFound, "upgrade task was not found", "taskId");
        if (task->get().state == production::TaskState::SettlementFailed) {
            auto retried = staged.retrySettlement(taskId_);
            if (!retried) return retried;
        }
        auto settled = staged.settle(taskId_, receipt_);
        if (!settled) return settled;
        auto after = staged.snapshot();
        if (!after) return eve::Result<void>::failure(after.status());
        after_    = std::move(after).takeValue();
        prepared_ = true;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> commit(const transaction::TransactionContext&) override {
        if (!prepared_ || committed_)
            return failure<void>(eve::DiagnosticCode::Conflict, "upgrade settlement is not prepared");
        auto current = queue_.snapshot();
        if (!current) return eve::Result<void>::failure(current.status());
        if (current.value() != before_)
            return failure<void>(eve::DiagnosticCode::StaleHandle, "upgrade queue changed while settlement was staged",
                                 "production");
        auto restored = queue_.restore(after_);
        if (!restored) return restored;
        committed_ = true;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> rollback(const transaction::TransactionContext&) override {
        prepared_ = false;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> compensate(const transaction::TransactionContext&) override {
        if (!committed_) return failure<void>(eve::DiagnosticCode::Conflict, "upgrade settlement has not committed");
        auto current = queue_.snapshot();
        if (!current) return eve::Result<void>::failure(current.status());
        if (current.value() != after_)
            return failure<void>(eve::DiagnosticCode::StaleHandle,
                                 "upgrade queue changed before settlement compensation", "production");
        auto restored = queue_.restore(before_);
        if (!restored) return restored;
        committed_ = false;
        prepared_  = false;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

private:
    production::WorkQueue&                  queue_;
    std::string                             taskId_;
    production::ProductionSettlementReceipt receipt_;
    std::string                             before_;
    std::string                             after_;
    bool                                    prepared_  = false;
    bool                                    committed_ = false;
};

class EnhanceParticipant final : public transaction::ITransactionParticipant {
public:
    EnhanceParticipant(inventory::Bag& bag, DecodedUpgradeReservation target) : bag_(bag), target_(std::move(target)) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "rpg-upgrade-enhance"; }

    [[nodiscard]] eve::Result<void> prepare(const transaction::TransactionContext&) override {
        if (target_.bagSlot < 0 || target_.bagSlot >= bag_.getSlotCount())
            return failure<void>(eve::DiagnosticCode::NotFound, "upgrade target slot is empty", "bagSlot");
        if (bag_.isSlotEmpty(target_.bagSlot))
            return failure<void>(eve::DiagnosticCode::NotFound, "upgrade target slot is empty", "bagSlot");
        if (bag_.getSlotItemId(target_.bagSlot) != target_.itemId ||
            bag_.getSlotInstanceId(target_.bagSlot) != target_.instanceId || bag_.getSlotQuantity(target_.bagSlot) != 1)
            return failure<void>(eve::DiagnosticCode::Conflict, "upgrade target instance no longer matches",
                                 "instanceId");
        auto current =
            EquipmentUpgrade::parseEnhanceLevel(bag_.getSlotProp(target_.bagSlot, kEquipmentEnhanceProp, "0"));
        if (!current) return eve::Result<void>::failure(current.status());
        if (current.value() != target_.fromLevel)
            return failure<void>(eve::DiagnosticCode::Conflict, "upgrade target enhance level changed", "enhance");
        previousEnhance_ = bag_.getSlotProp(target_.bagSlot, kEquipmentEnhanceProp, "0");
        prepared_        = true;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> commit(const transaction::TransactionContext&) override {
        if (!prepared_ || committed_)
            return failure<void>(eve::DiagnosticCode::Conflict, "upgrade enhance is not prepared", "bag");
        if (bag_.isSlotEmpty(target_.bagSlot) || bag_.getSlotItemId(target_.bagSlot) != target_.itemId ||
            bag_.getSlotInstanceId(target_.bagSlot) != target_.instanceId)
            return failure<void>(eve::DiagnosticCode::StaleHandle, "upgrade target changed before commit",
                                 "instanceId");
        bag_.setSlotProp(target_.bagSlot, kEquipmentEnhanceProp, std::to_string(target_.toLevel));
        appliedLevel_ = target_.toLevel;
        committed_    = true;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> rollback(const transaction::TransactionContext&) override {
        prepared_ = false;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> compensate(const transaction::TransactionContext&) override {
        if (!committed_) return failure<void>(eve::DiagnosticCode::Conflict, "upgrade enhance has not committed");
        if (!bag_.isSlotEmpty(target_.bagSlot) && bag_.getSlotInstanceId(target_.bagSlot) == target_.instanceId)
            bag_.setSlotProp(target_.bagSlot, kEquipmentEnhanceProp, previousEnhance_);
        committed_ = false;
        prepared_  = false;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] int appliedLevel() const noexcept { return appliedLevel_; }

private:
    inventory::Bag&           bag_;
    DecodedUpgradeReservation target_;
    std::string               previousEnhance_;
    int                       appliedLevel_ = 0;
    bool                      prepared_     = false;
    bool                      committed_    = false;
};

eve::Result<void> validateTargetStack(const inventory::Bag& bag, int bagSlot, int expectedInstanceId,
                                      const EquipmentUpgradeRecipe& recipe) {
    if (bagSlot < 0 || bagSlot >= bag.getSlotCount() || bag.isSlotEmpty(bagSlot))
        return failure<void>(eve::DiagnosticCode::NotFound, "upgrade requires a bag slot with the equipment",
                             "bagSlot");
    if (expectedInstanceId <= 0 || bag.getSlotInstanceId(bagSlot) != expectedInstanceId)
        return failure<void>(eve::DiagnosticCode::Conflict, "upgrade expectedInstanceId does not match the slot",
                             "expectedInstanceId");
    if (bag.getSlotItemId(bagSlot) != recipe.itemId)
        return failure<void>(eve::DiagnosticCode::Conflict, "upgrade item id does not match the recipe", "itemId");
    if (bag.getSlotQuantity(bagSlot) != 1)
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "upgrade target must be a single-item stack",
                             "bagSlot");
    const auto* definition = inventory::ItemRegistry::find(recipe.itemId);
    if (definition == nullptr)
        return failure<void>(eve::DiagnosticCode::NotFound, "upgrade item definition is missing", "itemId");
    if (definition->maxStack != 1)
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "upgrade targets must use maxStack=1 equipment items", "itemId");
    auto level = EquipmentUpgrade::parseEnhanceLevel(bag.getSlotProp(bagSlot, kEquipmentEnhanceProp, "0"));
    if (!level) return eve::Result<void>::failure(level.status());
    if (level.value() != recipe.fromLevel)
        return failure<void>(eve::DiagnosticCode::Conflict, "upgrade fromLevel does not match the instance",
                             "fromLevel");
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace

eve::Result<int> EquipmentUpgrade::parseEnhanceLevel(std::string_view text) {
    if (text.empty()) return eve::Result<int>::success(0);
    for (char ch : text) {
        if (!std::isdigit(static_cast<unsigned char>(ch)))
            return failure<int>(eve::DiagnosticCode::ParseError, "enhance prop must be a non-negative integer",
                                kEquipmentEnhanceProp);
    }
    int         value  = 0;
    const auto* begin  = text.data();
    const auto* end    = text.data() + text.size();
    const auto  parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end || value < 0)
        return failure<int>(eve::DiagnosticCode::ParseError, "enhance prop must be a non-negative integer",
                            kEquipmentEnhanceProp);
    return eve::Result<int>::success(value);
}

eve::Result<EquipmentUpgradeReceipt> EquipmentUpgrade::begin(EquipmentUpgradeRequest request) {
    if (request.production == nullptr || request.ingredients == nullptr || request.bag == nullptr)
        return failure<EquipmentUpgradeReceipt>(eve::DiagnosticCode::InvalidArgument,
                                                "upgrade requires production, ingredient account and bag ports",
                                                "request");
    if (request.owner.empty() || request.recipe.id.empty() || request.recipe.process.empty() ||
        request.recipe.itemId.empty())
        return failure<EquipmentUpgradeReceipt>(eve::DiagnosticCode::InvalidArgument,
                                                "upgrade owner, recipe id, process and itemId are required", "request");
    if (!request.recipe.ingredients.isValid() || request.recipe.duration.nanoseconds() <= 0 ||
        request.recipe.fromLevel < 0 || request.recipe.toLevel <= request.recipe.fromLevel)
        return failure<EquipmentUpgradeReceipt>(
            eve::DiagnosticCode::InvalidArgument,
            "upgrade recipe requires ingredients, positive duration and increasing levels", "recipe");
    auto valid = validateTargetStack(*request.bag, request.bagSlot, request.expectedInstanceId, request.recipe);
    if (!valid) return eve::Result<EquipmentUpgradeReceipt>::failure(valid.status());

    production::ProductionRequest productionRequest;
    productionRequest.owner       = std::move(request.owner);
    productionRequest.kind        = request.recipe.process;
    productionRequest.product     = request.recipe.id;
    productionRequest.duration    = request.recipe.duration;
    productionRequest.priority    = request.priority;
    productionRequest.definition  = request.recipe.definition;
    productionRequest.reservation = encodeReservation(request.recipe, request.bagSlot, request.expectedInstanceId);
    productionRequest.context     = eve::Value(eve::Value::Object{
            {"recipe", eve::Value(request.recipe.id)},
            {"process", eve::Value(request.recipe.process)},
            {"itemId", eve::Value(request.recipe.itemId)},
            {"fromLevel", eve::Value(std::int64_t(request.recipe.fromLevel))},
            {"toLevel", eve::Value(std::int64_t(request.recipe.toLevel))},
    });
    productionRequest.settlementRequired = true;

    if (request.transactionId.empty()) request.transactionId = "rpg.upgrade." + request.recipe.id;
    transaction::TransactionContext context(std::move(request.transactionId));
    ProductionParticipant           participant(*request.production, std::move(productionRequest));
    auto committed = transaction::AtomicResourcePayment::execute(context, *request.ingredients,
                                                                 request.recipe.ingredients, participant);
    if (!committed) return eve::Result<EquipmentUpgradeReceipt>::failure(committed.status());
    EquipmentUpgradeReceipt receipt{std::move(committed).takeValue(), participant.taskId()};
    return eve::Result<EquipmentUpgradeReceipt>::success(std::move(receipt),
                                                         eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<int> EquipmentUpgrade::settle(production::WorkQueue& queue, inventory::Bag& bag, std::string_view taskId) {
    auto task = queue.find(taskId);
    if (!task) return failure<int>(eve::DiagnosticCode::NotFound, "upgrade task was not found", "taskId");
    const std::string settlementId = "rpg.upgrade:" + std::string(taskId);
    if (task->get().state == production::TaskState::Completed && task->get().settlement.settlementId == settlementId) {
        auto decoded = decodeReservation(task->get().reservation);
        if (!decoded) return eve::Result<int>::failure(decoded.status());
        return eve::Result<int>::success(decoded.value().toLevel, eve::Status::success(eve::StatusCode::NoOp));
    }
    if (task->get().state != production::TaskState::ReadyToSettle &&
        task->get().state != production::TaskState::SettlementFailed)
        return failure<int>(eve::DiagnosticCode::Conflict, "upgrade task is not ready to settle", "taskId");
    auto decoded = decodeReservation(task->get().reservation);
    if (!decoded) return eve::Result<int>::failure(decoded.status());

    EnhanceParticipant                      enhanceParticipant(bag, decoded.value());
    production::ProductionSettlementReceipt receipt;
    receipt.settlementId = settlementId;
    receipt.payload      = eve::Value(eve::Value::Object{
             {"destination", eve::Value(bag.getId())},
             {"instanceId", eve::Value(std::int64_t(decoded.value().instanceId))},
             {"toLevel", eve::Value(std::int64_t(decoded.value().toLevel))},
    });
    SettlementParticipant           settlementParticipant(queue, std::string(taskId), std::move(receipt));
    transaction::TransactionContext context("rpg.upgrade.settle:" + std::string(taskId));
    std::array<transaction::ITransactionParticipant*, 2> participants{&settlementParticipant, &enhanceParticipant};
    transaction::Coordinator                             coordinator;
    auto committed = coordinator.execute(context, std::span<transaction::ITransactionParticipant*>(participants));
    if (!committed) {
        const auto* diagnostic = committed.error();
        auto        failed =
            queue.failSettlement(taskId, diagnostic == nullptr ? "upgrade enhance rejected" : diagnostic->message());
        const auto current = queue.find(taskId);
        if (!failed && (!current || current->get().state != production::TaskState::SettlementFailed))
            return eve::Result<int>::failure(failed.status());
        return eve::Result<int>::failure(committed.status());
    }
    return eve::Result<int>::success(enhanceParticipant.appliedLevel(), eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::rpg
