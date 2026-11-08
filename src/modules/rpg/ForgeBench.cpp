#include "rpg/ForgeBench.h"

#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "rpg/EquipmentSystem.h"
#include "rpg/RPGActor.h"
#include "transaction/AtomicBatch.h"

#include <utility>

namespace eve::rpg {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

std::string forgeTaskStateName(production::TaskState state) {
    switch (state) {
        case production::TaskState::Queued: return "queued";
        case production::TaskState::Running: return "running";
        case production::TaskState::Paused: return "paused";
        case production::TaskState::ReadyToSettle: return "ready_to_settle";
        case production::TaskState::SettlementFailed: return "settlement_failed";
        case production::TaskState::Completed: return "completed";
        case production::TaskState::Cancelled: return "cancelled";
        case production::TaskState::Failed: return "failed";
    }
    return {};
}

eve::Result<CraftingRecipe> parseCraftRecipe(const eve::Value& root) {
    const auto* object = root.getIf<eve::Value::Object>();
    if (object == nullptr)
        return failure<CraftingRecipe>(eve::DiagnosticCode::InvalidArgument, "craft recipe must be an object",
                                       "recipe");
    const auto id          = object->find("id");
    const auto process     = object->find("process");
    const auto ingredients = object->find("ingredients");
    const auto outputs     = object->find("outputs");
    const auto duration    = object->find("duration");
    if (id == object->end() || !id->second.isString() || id->second.asString().empty() ||
        ingredients == object->end() || outputs == object->end() || !outputs->second.isArray() ||
        duration == object->end() || !duration->second.isNumeric())
        return failure<CraftingRecipe>(eve::DiagnosticCode::InvalidArgument,
                                       "craft recipe requires id, ingredients, outputs and duration", "recipe");
    auto ingredientsJson = ingredients->second.toJson();
    if (!ingredientsJson) return eve::Result<CraftingRecipe>::failure(ingredientsJson.status());
    auto cost = transaction::AtomicBatch::parseCost(ingredientsJson.value());
    if (!cost) return eve::Result<CraftingRecipe>::failure(cost.status());
    auto seconds = Duration::fromSeconds(duration->second.isInt64() ? static_cast<double>(duration->second.asInt())
                                                                    : duration->second.asDouble());
    if (!seconds) return eve::Result<CraftingRecipe>::failure(seconds.status());
    CraftingRecipe recipe;
    recipe.id          = id->second.asString();
    recipe.process     = (process != object->end() && process->second.isString() && !process->second.asString().empty())
                             ? process->second.asString()
                             : "craft";
    recipe.ingredients = std::move(cost).takeValue();
    recipe.duration    = std::move(seconds).takeValue();
    const auto* outputValues = outputs->second.getIf<eve::Value::Array>();
    for (const auto& value : *outputValues) {
        const auto* entry = value.getIf<eve::Value::Object>();
        if (entry == nullptr)
            return failure<CraftingRecipe>(eve::DiagnosticCode::InvalidArgument, "craft output must be an object",
                                           "outputs");
        const auto item     = entry->find("item");
        const auto quantity = entry->find("quantity");
        if (item == entry->end() || !item->second.isString() || item->second.asString().empty() ||
            quantity == entry->end() || !quantity->second.isInt64() || quantity->second.asInt() <= 0)
            return failure<CraftingRecipe>(eve::DiagnosticCode::InvalidArgument,
                                           "craft outputs require item and positive quantity", "outputs");
        recipe.outputs.push_back({item->second.asString(), static_cast<int>(quantity->second.asInt())});
    }
    if (recipe.outputs.empty())
        return failure<CraftingRecipe>(eve::DiagnosticCode::InvalidArgument, "craft recipe has no outputs", "outputs");
    return eve::Result<CraftingRecipe>::success(std::move(recipe));
}

eve::Result<EquipmentUpgradeRecipe> parseUpgradeRecipe(const eve::Value& root) {
    const auto* object = root.getIf<eve::Value::Object>();
    if (object == nullptr)
        return failure<EquipmentUpgradeRecipe>(eve::DiagnosticCode::InvalidArgument, "upgrade recipe must be an object",
                                               "recipe");
    const auto id          = object->find("id");
    const auto process     = object->find("process");
    const auto itemId      = object->find("itemId");
    const auto fromLevel   = object->find("fromLevel");
    const auto toLevel     = object->find("toLevel");
    const auto ingredients = object->find("ingredients");
    const auto duration    = object->find("duration");
    if (id == object->end() || !id->second.isString() || id->second.asString().empty() || itemId == object->end() ||
        !itemId->second.isString() || itemId->second.asString().empty() || fromLevel == object->end() ||
        !fromLevel->second.isInt64() || toLevel == object->end() || !toLevel->second.isInt64() ||
        ingredients == object->end() || duration == object->end() || !duration->second.isNumeric())
        return failure<EquipmentUpgradeRecipe>(
            eve::DiagnosticCode::InvalidArgument,
            "upgrade recipe requires id, itemId, fromLevel, toLevel, ingredients and duration", "recipe");
    auto ingredientsJson = ingredients->second.toJson();
    if (!ingredientsJson) return eve::Result<EquipmentUpgradeRecipe>::failure(ingredientsJson.status());
    auto cost = transaction::AtomicBatch::parseCost(ingredientsJson.value());
    if (!cost) return eve::Result<EquipmentUpgradeRecipe>::failure(cost.status());
    auto seconds = Duration::fromSeconds(duration->second.isInt64() ? static_cast<double>(duration->second.asInt())
                                                                    : duration->second.asDouble());
    if (!seconds) return eve::Result<EquipmentUpgradeRecipe>::failure(seconds.status());
    EquipmentUpgradeRecipe recipe;
    recipe.id          = id->second.asString();
    recipe.process     = (process != object->end() && process->second.isString() && !process->second.asString().empty())
                             ? process->second.asString()
                             : "upgrade";
    recipe.itemId      = itemId->second.asString();
    recipe.fromLevel   = static_cast<int>(fromLevel->second.asInt());
    recipe.toLevel     = static_cast<int>(toLevel->second.asInt());
    recipe.ingredients = std::move(cost).takeValue();
    recipe.duration    = std::move(seconds).takeValue();
    return eve::Result<EquipmentUpgradeRecipe>::success(std::move(recipe));
}

}  // namespace

ForgeBench::ForgeBench() {
    auto allocated = production::Production::newQueueHandle();
    if (allocated) queueRef_ = std::move(allocated).takeValue();
}

ForgeBench::~ForgeBench() {
    unbind();
    if (queueRef_.isValid()) production::Production::release(queueRef_).ignore("forge bench queue release");
    queueRef_ = {};
}

ForgeBench::ForgeBench(ForgeBench&& other) noexcept
    : queueRef_(std::exchange(other.queueRef_, {})),
      bag_(std::exchange(other.bag_, nullptr)),
      equipment_(std::exchange(other.equipment_, nullptr)),
      actor_(std::exchange(other.actor_, nullptr)),
      tick_(other.tick_) {}

ForgeBench& ForgeBench::operator=(ForgeBench&& other) noexcept {
    if (this == &other) return *this;
    unbind();
    if (queueRef_.isValid()) production::Production::release(queueRef_).ignore("forge bench queue release");
    queueRef_  = std::exchange(other.queueRef_, {});
    bag_       = std::exchange(other.bag_, nullptr);
    equipment_ = std::exchange(other.equipment_, nullptr);
    actor_     = std::exchange(other.actor_, nullptr);
    tick_      = other.tick_;
    return *this;
}

eve::Result<void> ForgeBench::bind(inventory::Bag* bag, inventory::EquipmentSet* equipment, RPGActor* actor) {
    if (bag == nullptr) return failure<void>(eve::DiagnosticCode::InvalidArgument, "forge bench requires a bag", "bag");
    if (!queueRef_.isValid())
        return failure<void>(eve::DiagnosticCode::Failed, "forge bench production queue is unavailable", "queue");
    bag_       = bag;
    equipment_ = equipment;
    actor_     = actor;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void ForgeBench::unbind() {
    bag_       = nullptr;
    equipment_ = nullptr;
    actor_     = nullptr;
}

eve::ResultRef<production::WorkQueue> ForgeBench::queue() const {
    if (!queueRef_.isValid())
        return eve::ResultRef<production::WorkQueue>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "forge bench queue is stale", "queue"));
    return production::Production::resolve(queueRef_);
}

eve::Result<CraftingReceipt> ForgeBench::craft(CraftingRecipe recipe, std::uint32_t batchSize, int priority) {
    if (bag_ == nullptr)
        return failure<CraftingReceipt>(eve::DiagnosticCode::InvalidArgument, "forge bench is not bound", "bag");
    auto queueView = queue();
    if (!queueView) return eve::Result<CraftingReceipt>::failure(queueView.status());
    inventory::InventoryResourceAccount account(*bag_);
    CraftingRequest                     request;
    request.production  = &queueView.value().get();
    request.ingredients = &account;
    request.owner       = bag_->getId().empty() ? "forge" : bag_->getId();
    request.recipe      = std::move(recipe);
    request.batchSize   = batchSize;
    request.priority    = priority;
    return Crafting::begin(std::move(request));
}

eve::Result<CraftingReceipt> ForgeBench::craftFromJson(const std::string& json, std::uint32_t batchSize, int priority) {
    auto value = eve::Value::fromJson(json);
    if (!value) return eve::Result<CraftingReceipt>::failure(value.status());
    auto recipe = parseCraftRecipe(value.value());
    if (!recipe) return eve::Result<CraftingReceipt>::failure(recipe.status());
    return craft(std::move(recipe).takeValue(), batchSize, priority);
}

eve::Result<EquipmentUpgradeReceipt> ForgeBench::upgrade(EquipmentUpgradeRecipe recipe, int bagSlot,
                                                         int expectedInstanceId, int priority) {
    if (bag_ == nullptr)
        return failure<EquipmentUpgradeReceipt>(eve::DiagnosticCode::InvalidArgument, "forge bench is not bound",
                                                "bag");
    auto queueView = queue();
    if (!queueView) return eve::Result<EquipmentUpgradeReceipt>::failure(queueView.status());
    inventory::InventoryResourceAccount account(*bag_);
    EquipmentUpgradeRequest             request;
    request.production         = &queueView.value().get();
    request.ingredients        = &account;
    request.bag                = bag_;
    request.bagSlot            = bagSlot;
    request.expectedInstanceId = expectedInstanceId;
    request.owner              = bag_->getId().empty() ? "forge" : bag_->getId();
    request.recipe             = std::move(recipe);
    request.priority           = priority;
    return EquipmentUpgrade::begin(std::move(request));
}

eve::Result<EquipmentUpgradeReceipt> ForgeBench::upgradeFromJson(const std::string& json, int bagSlot,
                                                                 int expectedInstanceId, int priority) {
    auto value = eve::Value::fromJson(json);
    if (!value) return eve::Result<EquipmentUpgradeReceipt>::failure(value.status());
    auto recipe = parseUpgradeRecipe(value.value());
    if (!recipe) return eve::Result<EquipmentUpgradeReceipt>::failure(recipe.status());
    return upgrade(std::move(recipe).takeValue(), bagSlot, expectedInstanceId, priority);
}

eve::Result<void> ForgeBench::advance(eve::SimulationTick tick, eve::Duration dt) {
    auto queueView = queue();
    if (!queueView) return eve::Result<void>::failure(queueView.status());
    auto advanced = queueView.value().get().advance({tick, dt});
    if (!advanced) return advanced;
    tick_ = tick;
    return advanced;
}

eve::Result<int> ForgeBench::settle(std::string_view taskId) {
    if (bag_ == nullptr) return failure<int>(eve::DiagnosticCode::InvalidArgument, "forge bench is not bound", "bag");
    auto queueView = queue();
    if (!queueView) return eve::Result<int>::failure(queueView.status());
    auto task = queueView.value().get().find(taskId);
    if (!task) return failure<int>(eve::DiagnosticCode::NotFound, "forge task was not found", "taskId");
    auto settled = (task->get().kind == "upgrade") ? EquipmentUpgrade::settle(queueView.value().get(), *bag_, taskId)
                                                   : Crafting::settle(queueView.value().get(), *bag_, taskId);
    if (settled) (void)syncEquipment();
    return settled;
}

eve::Result<int> ForgeBench::settleReady() {
    if (bag_ == nullptr) return failure<int>(eve::DiagnosticCode::InvalidArgument, "forge bench is not bound", "bag");
    auto queueView = queue();
    if (!queueView) return eve::Result<int>::failure(queueView.status());
    std::vector<std::string> ready;
    for (int i = 0; i < queueView.value().get().taskCount(); ++i) {
        const auto task = queueView.value().get().taskAt(i);
        if (!task) continue;
        if (task->get().state == production::TaskState::ReadyToSettle ||
            task->get().state == production::TaskState::SettlementFailed)
            ready.push_back(task->get().id);
    }
    int applied = 0;
    for (const std::string& taskId : ready) {
        auto settled = settle(taskId);
        if (!settled) return settled;
        if (settled.status().code() == eve::StatusCode::Applied) ++applied;
    }
    return eve::Result<int>::success(applied, eve::Status::success(eve::StatusCode::Applied));
}

std::uint64_t ForgeBench::queueHandle() const noexcept { return queueRef_.isValid() ? queueRef_.packed() : 0; }

int ForgeBench::taskCount() const {
    auto queueView = queue();
    return queueView ? queueView.value().get().taskCount() : 0;
}

std::string ForgeBench::taskIdAt(int index) const {
    auto queueView = queue();
    if (!queueView) return {};
    const auto task = queueView.value().get().taskAt(index);
    return task ? task->get().id : std::string{};
}

std::string ForgeBench::taskStateAt(int index) const {
    auto queueView = queue();
    if (!queueView) return {};
    const auto task = queueView.value().get().taskAt(index);
    return task ? forgeTaskStateName(task->get().state) : std::string{};
}

std::string ForgeBench::taskKindAt(int index) const {
    auto queueView = queue();
    if (!queueView) return {};
    const auto task = queueView.value().get().taskAt(index);
    return task ? task->get().kind : std::string{};
}

float ForgeBench::taskProgressAt(int index) const {
    auto queueView = queue();
    if (!queueView) return 0.f;
    const auto task = queueView.value().get().taskAt(index);
    return task ? static_cast<float>(task->get().progress.seconds()) : 0.f;
}

float ForgeBench::taskDurationAt(int index) const {
    auto queueView = queue();
    if (!queueView) return 0.f;
    const auto task = queueView.value().get().taskAt(index);
    return task ? static_cast<float>(task->get().duration.seconds()) : 0.f;
}

int ForgeBench::syncEquipment() const {
    if (actor_ == nullptr || equipment_ == nullptr) return 0;
    return EquipmentSystem::syncEquipModifiers(actor_, equipment_);
}

}  // namespace eve::rpg
