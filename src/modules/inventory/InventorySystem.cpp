#include "inventory/InventorySystem.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/Item.h"

#include <algorithm>
#include <cmath>

namespace eve::inventory {

struct PreparedInventoryAdd::State {
    Bag                                  *target = nullptr;
    std::unique_ptr<Bag>                  baseline;
    std::unique_ptr<Bag>                  candidate;
    std::vector<InventoryChangeEvent>     events;
    int                                   totalAdded = 0;
};

struct PreparedInventoryRemove::State {
    Bag                              *target = nullptr;
    std::unique_ptr<Bag>              baseline;
    std::unique_ptr<Bag>              candidate;
    InventoryChangeEvent              event;
    int                               totalRemoved = 0;
};

PreparedInventoryAdd::PreparedInventoryAdd() = default;
PreparedInventoryAdd::~PreparedInventoryAdd() = default;
PreparedInventoryAdd::PreparedInventoryAdd(PreparedInventoryAdd &&) noexcept = default;
PreparedInventoryAdd &PreparedInventoryAdd::operator=(PreparedInventoryAdd &&) noexcept = default;
PreparedInventoryRemove::PreparedInventoryRemove() = default;
PreparedInventoryRemove::~PreparedInventoryRemove() = default;
PreparedInventoryRemove::PreparedInventoryRemove(PreparedInventoryRemove &&) noexcept = default;
PreparedInventoryRemove &PreparedInventoryRemove::operator=(PreparedInventoryRemove &&) noexcept = default;

namespace {

bool tagsIntersect(const std::vector<std::string> &a, const std::vector<std::string> &b) {
    for (const auto &t : a) {
        if (std::find(b.begin(), b.end(), t) != b.end()) return true;
    }
    return false;
}

bool propsEqual(const std::unordered_map<std::string, std::string> &a,
                const std::unordered_map<std::string, std::string> &b) {
    return a == b;
}

}  // namespace

std::unordered_map<std::string, InventorySystem::AcceptFn> &InventorySystem::acceptRules() {
    static std::unordered_map<std::string, AcceptFn> t;
    return t;
}

std::unordered_map<std::string, InventorySystem::CapacityFn> &InventorySystem::capacityPolicies() {
    static std::unordered_map<std::string, CapacityFn> t;
    return t;
}

std::unordered_map<std::string, InventorySystem::StackFn> &InventorySystem::stackRules() {
    static std::unordered_map<std::string, StackFn> t;
    return t;
}

std::unordered_map<std::string, InventorySystem::ChangeHook> &InventorySystem::changeHooks() {
    static std::unordered_map<std::string, ChangeHook> t;
    return t;
}

std::vector<InventoryChangeEvent> &InventorySystem::eventQueue() {
    static std::vector<InventoryChangeEvent> q;
    return q;
}

int &InventorySystem::instanceCounter() {
    static int c = 1;
    return c;
}

bool &InventorySystem::builtinsReady() {
    static bool ready = false;
    return ready;
}

bool &InventorySystem::changeHooksSuppressed() {
    static bool suppressed = false;
    return suppressed;
}

void InventorySystem::registerAcceptRule(const std::string &name, AcceptFn fn) {
    if (name.empty() || !fn) return;
    acceptRules()[name] = std::move(fn);
}

void InventorySystem::unregisterAcceptRule(const std::string &name) { acceptRules().erase(name); }

bool InventorySystem::hasAcceptRule(const std::string &name) {
    ensureBuiltins();
    return acceptRules().count(name) > 0;
}

void InventorySystem::registerCapacityPolicy(const std::string &name, CapacityFn fn) {
    if (name.empty() || !fn) return;
    capacityPolicies()[name] = std::move(fn);
}

void InventorySystem::unregisterCapacityPolicy(const std::string &name) {
    capacityPolicies().erase(name);
}

bool InventorySystem::hasCapacityPolicy(const std::string &name) {
    ensureBuiltins();
    return capacityPolicies().count(name) > 0;
}

void InventorySystem::registerStackRule(const std::string &name, StackFn fn) {
    if (name.empty() || !fn) return;
    stackRules()[name] = std::move(fn);
}

void InventorySystem::unregisterStackRule(const std::string &name) { stackRules().erase(name); }

bool InventorySystem::hasStackRule(const std::string &name) {
    ensureBuiltins();
    return stackRules().count(name) > 0;
}

void InventorySystem::registerChangeHook(const std::string &name, ChangeHook fn) {
    if (name.empty() || !fn) return;
    changeHooks()[name] = std::move(fn);
}

void InventorySystem::unregisterChangeHook(const std::string &name) { changeHooks().erase(name); }

bool InventorySystem::hasChangeHook(const std::string &name) {
    return changeHooks().count(name) > 0;
}

void InventorySystem::ensureBuiltins() {
    if (builtinsReady()) return;
    builtinsReady() = true;

    registerAcceptRule("any", [](const Bag &, const ItemDefinition &, int, std::string *) {
        return true;
    });

    registerAcceptRule("default", [](const Bag &bag, const ItemDefinition &def, int,
                                     std::string *reason) {
        if (!bag.rejectTags().empty() && tagsIntersect(def.tags, bag.rejectTags())) {
            if (reason) *reason = "rejected_tag";
            return false;
        }
        if (!bag.acceptTags().empty() && !tagsIntersect(def.tags, bag.acceptTags())) {
            if (reason) *reason = "accept_tag_mismatch";
            return false;
        }
        return true;
    });

    auto slotsOk = [](const Bag &bag, const ItemDefinition &def, int quantity, std::string *reason) {
        int need = quantity;
        for (int i = 0; i < bag.getSlotCount() && need > 0; ++i) {
            int space = freeSpaceInSlot(bag, i, def);
            if (space > 0) need -= space;
        }
        if (need > 0) {
            if (reason) *reason = "no_slot";
            return false;
        }
        return true;
    };

    auto weightOk = [](const Bag &bag, const ItemDefinition &def, int quantity, std::string *reason) {
        if (bag.getMaxWeight() <= 0.f) return true;
        float add = def.weight * float(quantity);
        if (usedWeight(&bag) + add > bag.getMaxWeight() + 1e-6f) {
            if (reason) *reason = "over_weight";
            return false;
        }
        return true;
    };

    auto volumeOk = [](const Bag &bag, const ItemDefinition &def, int quantity, std::string *reason) {
        if (bag.getMaxVolume() <= 0.f) return true;
        float add = def.volume * float(quantity);
        if (usedVolume(&bag) + add > bag.getMaxVolume() + 1e-6f) {
            if (reason) *reason = "over_volume";
            return false;
        }
        return true;
    };

    registerCapacityPolicy("unlimited",
                           [](const Bag &, const ItemDefinition &, int, std::string *) { return true; });
    registerCapacityPolicy("slots", slotsOk);
    registerCapacityPolicy("weight", weightOk);
    registerCapacityPolicy("volume", volumeOk);
    registerCapacityPolicy("slotsAndWeight", [slotsOk, weightOk](const Bag &bag,
                                                                const ItemDefinition &def,
                                                                int quantity, std::string *reason) {
        return slotsOk(bag, def, quantity, reason) && weightOk(bag, def, quantity, reason);
    });
    registerCapacityPolicy("slotsWeightVolume",
                           [slotsOk, weightOk, volumeOk](const Bag &bag, const ItemDefinition &def,
                                                         int quantity, std::string *reason) {
                               return slotsOk(bag, def, quantity, reason) &&
                                      weightOk(bag, def, quantity, reason) &&
                                      volumeOk(bag, def, quantity, reason);
                           });

    registerStackRule("sameItem",
                      [](const ItemStack &a, const ItemStack &b, const ItemDefinition &) {
                          return !a.empty() && !b.empty() && a.itemId == b.itemId;
                      });
    registerStackRule("sameItemAndProps",
                      [](const ItemStack &a, const ItemStack &b, const ItemDefinition &) {
                          return !a.empty() && !b.empty() && a.itemId == b.itemId &&
                                 propsEqual(a.props, b.props) &&
                                 std::fabs(a.durability - b.durability) < 1e-6f;
                      });
    registerStackRule("never", [](const ItemStack &, const ItemStack &, const ItemDefinition &) {
        return false;
    });
}

int InventorySystem::nextInstanceId() { return instanceCounter()++; }

void InventorySystem::ensureNextInstanceIdAbove(int usedInstanceId) noexcept {
    if (instanceCounter() <= usedInstanceId) instanceCounter() = usedInstanceId + 1;
}

std::unique_ptr<Bag> InventorySystem::cloneBag(const Bag &source) {
    auto clone = std::make_unique<Bag>(source.getSlotCount());
    clone->id_ = source.id_;
    clone->kind_ = source.kind_;
    clone->maxWeight_ = source.maxWeight_;
    clone->maxVolume_ = source.maxVolume_;
    clone->acceptRule_ = source.acceptRule_;
    clone->capacityPolicy_ = source.capacityPolicy_;
    clone->stackRule_ = source.stackRule_;
    clone->acceptTags_ = source.acceptTags_;
    clone->rejectTags_ = source.rejectTags_;
    clone->slots_ = source.slots_;
    clone->extra_ = source.extra_;
    return clone;
}

bool InventorySystem::bagsEqual(const Bag &target, const Bag &baseline) {
    if (target.id_ != baseline.id_ || target.kind_ != baseline.kind_ ||
        target.maxWeight_ != baseline.maxWeight_ || target.maxVolume_ != baseline.maxVolume_ ||
        target.acceptRule_ != baseline.acceptRule_ ||
        target.capacityPolicy_ != baseline.capacityPolicy_ || target.stackRule_ != baseline.stackRule_ ||
        target.acceptTags_ != baseline.acceptTags_ || target.rejectTags_ != baseline.rejectTags_ ||
        target.extra_ != baseline.extra_ || target.slots_.size() != baseline.slots_.size())
        return false;
    for (std::size_t i = 0; i < target.slots_.size(); ++i) {
        const auto &a = target.slots_[i];
        const auto &b = baseline.slots_[i];
        if (a.instanceId != b.instanceId || a.itemId != b.itemId || a.quantity != b.quantity ||
            a.durability != b.durability || a.props != b.props || a.tags != b.tags)
            return false;
    }
    return true;
}

void InventorySystem::emit(InventoryChangeEvent ev) {
    if (!changeHooksSuppressed()) {
        for (auto &kv : changeHooks()) {
            if (!kv.second) continue;
            try {
                kv.second(ev);
            } catch (...) {
                // Hooks are post-commit observers. One faulty observer must not
                // unwind an already committed authoritative inventory change or
                // prevent the remaining observers and poll queue from seeing it.
            }
        }
    }
    eventQueue().push_back(std::move(ev));
}

void InventorySystem::pushEvent(InventoryChangeEvent ev) { emit(std::move(ev)); }

void InventorySystem::pollEvents(std::vector<InventoryChangeEvent> &out) {
    out = eventQueue();
    eventQueue().clear();
}

void InventorySystem::clearEvents() { eventQueue().clear(); }

const std::vector<InventoryChangeEvent> &InventorySystem::events() { return eventQueue(); }

bool InventorySystem::canStackTogether(const Bag &bag, const ItemStack &a, const ItemStack &b,
                                       const ItemDefinition &def) {
    ensureBuiltins();
    auto it = stackRules().find(bag.getStackRule());
    if (it == stackRules().end() || !it->second) {
        return !a.empty() && !b.empty() && a.itemId == b.itemId;
    }
    return it->second(a, b, def);
}

bool InventorySystem::checkAccept(const Bag &bag, const ItemDefinition &def, int quantity,
                                  std::string *reason) {
    ensureBuiltins();
    auto it = acceptRules().find(bag.getAcceptRule());
    if (it == acceptRules().end() || !it->second) {
        if (reason) *reason = "unknown_accept_rule";
        return false;
    }
    return it->second(bag, def, quantity, reason);
}

bool InventorySystem::checkCapacity(const Bag &bag, const ItemDefinition &def, int quantity,
                                    std::string *reason) {
    ensureBuiltins();
    auto it = capacityPolicies().find(bag.getCapacityPolicy());
    if (it == capacityPolicies().end() || !it->second) {
        if (reason) *reason = "unknown_capacity_policy";
        return false;
    }
    return it->second(bag, def, quantity, reason);
}

int InventorySystem::freeSpaceInSlot(const Bag &bag, int slot, const ItemDefinition &def) {
    if (slot < 0 || slot >= bag.getSlotCount()) return 0;
    const auto &s = bag.slots()[size_t(slot)];
    int maxStack = def.maxStack > 0 ? def.maxStack : 1;
    // If the bag's stack rule rejects merging two identical probes, treat every
    // slot as capacity 1 (e.g. built-in "never").
    {
        ItemStack probeA;
        probeA.itemId = def.id;
        probeA.quantity = 1;
        ItemStack probeB = probeA;
        probeB.instanceId = 1;
        if (!canStackTogether(bag, probeA, probeB, def)) maxStack = 1;
    }
    if (s.empty()) return maxStack;
    ItemStack probe;
    probe.itemId = def.id;
    probe.quantity = 1;
    if (!canStackTogether(bag, s, probe, def)) return 0;
    return std::max(0, maxStack - s.quantity);
}

bool InventorySystem::canAdd(Bag *bag, const std::string &itemId, int quantity,
                             std::string *reason) {
    if (!bag || quantity <= 0) {
        if (reason) *reason = "invalid_args";
        return false;
    }
    const ItemDefinition *def = ItemRegistry::find(itemId);
    if (!def) {
        if (reason) *reason = "unknown_item";
        return false;
    }
    if (!checkAccept(*bag, *def, quantity, reason)) return false;
    if (!checkCapacity(*bag, *def, quantity, reason)) return false;
    return true;
}

int InventorySystem::addItem(Bag *bag, const std::string &itemId, int quantity) {
    return addItemImpl(bag, itemId, quantity, true);
}

int InventorySystem::addItemImpl(Bag *bag, const std::string &itemId, int quantity,
                                 bool publish) {
    if (!bag || quantity <= 0) return 0;
    const ItemDefinition *def = ItemRegistry::find(itemId);
    if (!def) return 0;
    // Accept is all-or-nothing for the requested item type (not quantity-scaled).
    if (!checkAccept(*bag, *def, 1, nullptr)) return 0;

    int remaining = quantity;
    int added = 0;

    auto maxFittable = [&](int space) -> int {
        int lo = 0, hi = std::min(space, remaining);
        while (lo < hi) {
            int mid = lo + (hi - lo + 1) / 2;
            if (checkCapacity(*bag, *def, mid, nullptr))
                lo = mid;
            else
                hi = mid - 1;
        }
        return lo;
    };

    // Pass 1: fill existing stacks.
    for (int i = 0; i < bag->getSlotCount() && remaining > 0; ++i) {
        int space = freeSpaceInSlot(*bag, i, *def);
        auto &s = bag->slots()[size_t(i)];
        if (s.empty() || space <= 0) continue;
        int put = maxFittable(space);
        if (put <= 0) continue;
        s.quantity += put;
        remaining -= put;
        added += put;
    }

    // Pass 2: empty slots.
    for (int i = 0; i < bag->getSlotCount() && remaining > 0; ++i) {
        auto &s = bag->slots()[size_t(i)];
        if (!s.empty()) continue;
        int space = freeSpaceInSlot(*bag, i, *def);
        int put = maxFittable(space);
        if (put <= 0) continue;
        s.instanceId = nextInstanceId();
        s.itemId = def->id;
        s.quantity = put;
        s.durability = -1.f;
        s.props.clear();
        s.tags.clear();
        remaining -= put;
        added += put;
    }

    if (publish && added > 0) {
        InventoryChangeEvent ev;
        ev.action = "add";
        ev.bagId = bag->getId();
        ev.itemId = itemId;
        ev.quantity = added;
        emit(std::move(ev));
    }
    return added;
}

eve::Result<PreparedInventoryAdd>
InventorySystem::prepareAddBatch(Bag *bag, const std::vector<InventoryItemGrant> &grants) {
    if (!bag)
        return eve::Result<PreparedInventoryAdd>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "inventory batch requires a Bag", "bag"));
    if (grants.empty())
        return eve::Result<PreparedInventoryAdd>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "inventory batch requires at least one grant", "grants"));

    PreparedInventoryAdd prepared;
    prepared.state_ = std::make_unique<PreparedInventoryAdd::State>();
    prepared.state_->target = bag;
    prepared.state_->baseline = cloneBag(*bag);
    prepared.state_->candidate = cloneBag(*bag);
    prepared.state_->events.reserve(grants.size());
    for (std::size_t index = 0; index < grants.size(); ++index) {
        const auto &grant = grants[index];
        if (grant.itemId.empty() || grant.quantity <= 0)
            return eve::Result<PreparedInventoryAdd>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "inventory grant requires an item id and positive quantity",
                "grants[" + std::to_string(index) + "]"));
        const int added = addItemImpl(prepared.state_->candidate.get(), grant.itemId, grant.quantity, false);
        if (added != grant.quantity)
            return eve::Result<PreparedInventoryAdd>::failure(eve::Diagnostic::error(
                ItemRegistry::find(grant.itemId) ? eve::DiagnosticCode::PreconditionViolation
                                                 : eve::DiagnosticCode::NotFound,
                ItemRegistry::find(grant.itemId) ? "inventory cannot fit the complete reward batch"
                                                 : "inventory reward references an unknown item",
                "grants[" + std::to_string(index) + "].itemId", {{"itemId", grant.itemId}},
                "inventory.prepare-add-batch"));
        prepared.state_->totalAdded += added;
        InventoryChangeEvent event;
        event.action = "add";
        event.bagId = bag->getId();
        event.itemId = grant.itemId;
        event.quantity = added;
        prepared.state_->events.push_back(std::move(event));
    }
    return eve::Result<PreparedInventoryAdd>::success(std::move(prepared),
                                                       eve::Status::success(eve::StatusCode::Pending));
}

eve::Result<int> InventorySystem::commitAddBatch(PreparedInventoryAdd prepared) {
    if (!prepared.state_ || !prepared.state_->target || !prepared.state_->baseline ||
        !prepared.state_->candidate)
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "inventory batch is empty or already consumed", "prepared"));
    Bag &target = *prepared.state_->target;
    const Bag &baseline = *prepared.state_->baseline;
    if (!bagsEqual(target, baseline))
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "Bag changed after inventory batch preparation", "bag"));

    target.slots_.swap(prepared.state_->candidate->slots_);
    const int totalAdded = prepared.state_->totalAdded;
    for (auto &event : prepared.state_->events) emit(std::move(event));
    prepared.state_.reset();
    return eve::Result<int>::success(totalAdded, eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<PreparedInventoryRemove>
InventorySystem::prepareRemove(Bag *bag, const std::string &itemId, int quantity) {
    if (!bag || itemId.empty() || quantity <= 0)
        return eve::Result<PreparedInventoryRemove>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "inventory removal requires a Bag, item id, and positive quantity", "removal"));
    if (!ItemRegistry::find(itemId))
        return eve::Result<PreparedInventoryRemove>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "inventory removal references an unknown item", "itemId",
            {{"itemId", itemId}}, "inventory.prepare-remove"));
    if (countItem(bag, itemId) < quantity)
        return eve::Result<PreparedInventoryRemove>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation,
            "inventory does not contain the complete removal quantity", "quantity",
            {{"itemId", itemId}}, "inventory.prepare-remove"));

    PreparedInventoryRemove prepared;
    prepared.state_ = std::make_unique<PreparedInventoryRemove::State>();
    prepared.state_->target = bag;
    prepared.state_->baseline = cloneBag(*bag);
    prepared.state_->candidate = cloneBag(*bag);
    int remaining = quantity;
    for (auto &stack : prepared.state_->candidate->slots_) {
        if (remaining <= 0) break;
        if (stack.empty() || stack.itemId != itemId) continue;
        const int take = std::min(stack.quantity, remaining);
        stack.quantity -= take;
        remaining -= take;
        if (stack.quantity <= 0) stack.clear();
    }
    prepared.state_->totalRemoved = quantity;
    prepared.state_->event.action = "remove";
    prepared.state_->event.bagId = bag->getId();
    prepared.state_->event.itemId = itemId;
    prepared.state_->event.quantity = quantity;
    return eve::Result<PreparedInventoryRemove>::success(
        std::move(prepared), eve::Status::success(eve::StatusCode::Pending));
}

eve::Result<int> InventorySystem::commitRemove(PreparedInventoryRemove prepared) {
    if (!prepared.state_ || !prepared.state_->target || !prepared.state_->baseline ||
        !prepared.state_->candidate)
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "inventory removal is empty or already consumed", "prepared"));
    Bag &target = *prepared.state_->target;
    if (!bagsEqual(target, *prepared.state_->baseline))
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "Bag changed after inventory removal preparation", "bag"));
    target.slots_.swap(prepared.state_->candidate->slots_);
    const int removed = prepared.state_->totalRemoved;
    emit(std::move(prepared.state_->event));
    prepared.state_.reset();
    return eve::Result<int>::success(removed, eve::Status::success(eve::StatusCode::Applied));
}

int InventorySystem::removeItem(Bag *bag, const std::string &itemId, int quantity) {
    if (!bag || quantity <= 0 || itemId.empty()) return 0;
    int remaining = quantity;
    int removed = 0;
    for (int i = 0; i < bag->getSlotCount() && remaining > 0; ++i) {
        auto &s = bag->slots()[size_t(i)];
        if (s.empty() || s.itemId != itemId) continue;
        int take = std::min(s.quantity, remaining);
        s.quantity -= take;
        remaining -= take;
        removed += take;
        if (s.quantity <= 0) s.clear();
    }
    if (removed > 0) {
        InventoryChangeEvent ev;
        ev.action = "remove";
        ev.bagId = bag->getId();
        ev.itemId = itemId;
        ev.quantity = removed;
        emit(std::move(ev));
    }
    return removed;
}

int InventorySystem::removeAt(Bag *bag, int slot, int quantity) {
    if (!bag || slot < 0 || slot >= bag->getSlotCount() || quantity <= 0) return 0;
    auto &s = bag->slots()[size_t(slot)];
    if (s.empty()) return 0;
    int take = std::min(s.quantity, quantity);
    std::string itemId = s.itemId;
    s.quantity -= take;
    if (s.quantity <= 0) s.clear();
    InventoryChangeEvent ev;
    ev.action = "remove";
    ev.bagId = bag->getId();
    ev.itemId = itemId;
    ev.quantity = take;
    ev.slot = slot;
    emit(std::move(ev));
    return take;
}

bool InventorySystem::swapSlots(Bag *bag, int slotA, int slotB) {
    if (!bag || slotA < 0 || slotB < 0 || slotA >= bag->getSlotCount() ||
        slotB >= bag->getSlotCount() || slotA == slotB)
        return false;
    std::swap(bag->slots()[size_t(slotA)], bag->slots()[size_t(slotB)]);
    InventoryChangeEvent ev;
    ev.action = "swap";
    ev.bagId = bag->getId();
    ev.slot = slotA;
    ev.otherSlot = slotB;
    emit(std::move(ev));
    return true;
}

bool InventorySystem::moveSlot(Bag *bag, int fromSlot, int toSlot) {
    if (!bag || fromSlot < 0 || toSlot < 0 || fromSlot >= bag->getSlotCount() ||
        toSlot >= bag->getSlotCount() || fromSlot == toSlot)
        return false;
    auto &from = bag->slots()[size_t(fromSlot)];
    auto &to = bag->slots()[size_t(toSlot)];
    if (from.empty()) return false;

    if (to.empty()) {
        to = from;
        from.clear();
        InventoryChangeEvent ev;
        ev.action = "move";
        ev.bagId = bag->getId();
        ev.itemId = to.itemId;
        ev.quantity = to.quantity;
        ev.slot = fromSlot;
        ev.otherSlot = toSlot;
        emit(std::move(ev));
        return true;
    }

    const ItemDefinition *def = ItemRegistry::find(from.itemId);
    if (!def) return false;
    if (canStackTogether(*bag, from, to, *def)) {
        int maxStack = def->maxStack > 0 ? def->maxStack : 1;
        int space = maxStack - to.quantity;
        if (space <= 0) return false;
        int put = std::min(space, from.quantity);
        to.quantity += put;
        from.quantity -= put;
        if (from.quantity <= 0) from.clear();
        InventoryChangeEvent ev;
        ev.action = "merge";
        ev.bagId = bag->getId();
        ev.itemId = to.itemId;
        ev.quantity = put;
        ev.slot = fromSlot;
        ev.otherSlot = toSlot;
        emit(std::move(ev));
        return true;
    }
    return swapSlots(bag, fromSlot, toSlot);
}

bool InventorySystem::splitStack(Bag *bag, int slot, int quantity, int toSlot) {
    if (!bag || slot < 0 || toSlot < 0 || slot >= bag->getSlotCount() ||
        toSlot >= bag->getSlotCount() || slot == toSlot || quantity <= 0)
        return false;
    auto &from = bag->slots()[size_t(slot)];
    auto &to = bag->slots()[size_t(toSlot)];
    if (from.empty() || !to.empty()) return false;
    if (quantity >= from.quantity) return false;

    to = from;
    to.instanceId = nextInstanceId();
    to.quantity = quantity;
    from.quantity -= quantity;

    InventoryChangeEvent ev;
    ev.action = "split";
    ev.bagId = bag->getId();
    ev.itemId = from.itemId;
    ev.quantity = quantity;
    ev.slot = slot;
    ev.otherSlot = toSlot;
    emit(std::move(ev));
    return true;
}

int InventorySystem::transfer(Bag *from, Bag *to, const std::string &itemId, int quantity) {
    if (!from || !to || quantity <= 0 || itemId.empty()) return 0;
    int available = countItem(from, itemId);
    int want = std::min(available, quantity);
    if (want <= 0) return 0;

    for (int n = want; n >= 1; --n) {
        if (!canAdd(to, itemId, n, nullptr)) continue;

        // Detach from source without emitting per-slot remove events.
        int remaining = n;
        for (int i = 0; i < from->getSlotCount() && remaining > 0; ++i) {
            auto &s = from->slots()[size_t(i)];
            if (s.empty() || s.itemId != itemId) continue;
            int take = std::min(s.quantity, remaining);
            s.quantity -= take;
            remaining -= take;
            if (s.quantity <= 0) s.clear();
        }

        int added = addItem(to, itemId, n);
        if (added < n) {
            // Roll back shortfall into source (best-effort).
            int needRestore = n - added;
            addItem(from, itemId, needRestore);
        }
        if (added > 0) {
            InventoryChangeEvent ev;
            ev.action = "transfer";
            ev.bagId = from->getId();
            ev.otherBagId = to->getId();
            ev.itemId = itemId;
            ev.quantity = added;
            emit(std::move(ev));
        }
        return added;
    }
    return 0;
}

int InventorySystem::transferSlot(Bag *from, int fromSlot, Bag *to, int quantity) {
    if (!from || !to || fromSlot < 0 || fromSlot >= from->getSlotCount() || quantity <= 0)
        return 0;
    auto &s = from->slots()[size_t(fromSlot)];
    if (s.empty()) return 0;
    int want = std::min(s.quantity, quantity);
    std::string itemId = s.itemId;

    for (int n = want; n >= 1; --n) {
        if (!canAdd(to, itemId, n, nullptr)) continue;

        // Prefer preserving instance/props when moving a whole stack into an empty target slot.
        if (n == s.quantity) {
            int empty = -1;
            for (int i = 0; i < to->getSlotCount(); ++i) {
                if (to->slots()[size_t(i)].empty()) {
                    empty = i;
                    break;
                }
            }
            const ItemDefinition *def = ItemRegistry::find(itemId);
            bool canPlaceWhole = empty >= 0 && def != nullptr;
            if (canPlaceWhole) {
                // Also allow merging into an existing compatible stack instead.
                bool merged = false;
                for (int i = 0; i < to->getSlotCount(); ++i) {
                    auto &ts = to->slots()[size_t(i)];
                    if (ts.empty()) continue;
                    if (!canStackTogether(*to, s, ts, *def)) continue;
                    int maxStack = def->maxStack > 0 ? def->maxStack : 1;
                    int space = maxStack - ts.quantity;
                    if (space < n) continue;
                    if (!checkCapacity(*to, *def, n, nullptr)) continue;
                    ts.quantity += n;
                    s.clear();
                    InventoryChangeEvent ev;
                    ev.action = "transfer";
                    ev.bagId = from->getId();
                    ev.otherBagId = to->getId();
                    ev.itemId = itemId;
                    ev.quantity = n;
                    ev.slot = fromSlot;
                    ev.otherSlot = i;
                    emit(std::move(ev));
                    merged = true;
                    break;
                }
                if (merged) return n;

                if (checkCapacity(*to, *def, n, nullptr) && checkAccept(*to, *def, n, nullptr)) {
                    to->slots()[size_t(empty)] = s;
                    s.clear();
                    InventoryChangeEvent ev;
                    ev.action = "transfer";
                    ev.bagId = from->getId();
                    ev.otherBagId = to->getId();
                    ev.itemId = itemId;
                    ev.quantity = n;
                    ev.slot = fromSlot;
                    ev.otherSlot = empty;
                    emit(std::move(ev));
                    return n;
                }
            }
        }

        s.quantity -= n;
        if (s.quantity <= 0) s.clear();
        int added = addItem(to, itemId, n);
        if (added < n) {
            // Restore remainder into original slot if possible.
            auto &fs = from->slots()[size_t(fromSlot)];
            if (fs.empty()) {
                fs.itemId = itemId;
                fs.quantity = n - added;
                fs.instanceId = nextInstanceId();
            } else if (fs.itemId == itemId) {
                fs.quantity += (n - added);
            } else {
                addItem(from, itemId, n - added);
            }
        }
        if (added > 0) {
            InventoryChangeEvent ev;
            ev.action = "transfer";
            ev.bagId = from->getId();
            ev.otherBagId = to->getId();
            ev.itemId = itemId;
            ev.quantity = added;
            ev.slot = fromSlot;
            emit(std::move(ev));
        }
        return added;
    }
    return 0;
}

int InventorySystem::countItem(const Bag *bag, const std::string &itemId) {
    if (!bag) return 0;
    int n = 0;
    for (const auto &s : bag->slots()) {
        if (!s.empty() && s.itemId == itemId) n += s.quantity;
    }
    return n;
}

int InventorySystem::findItem(const Bag *bag, const std::string &itemId) {
    if (!bag) return -1;
    for (int i = 0; i < bag->getSlotCount(); ++i) {
        const auto &s = bag->slots()[size_t(i)];
        if (!s.empty() && s.itemId == itemId) return i;
    }
    return -1;
}

int InventorySystem::findItemByTag(const Bag *bag, const std::string &tag) {
    if (!bag || tag.empty()) return -1;
    for (int i = 0; i < bag->getSlotCount(); ++i) {
        const auto &s = bag->slots()[size_t(i)];
        if (s.empty()) continue;
        if (s.hasTag(tag)) return i;
        const ItemDefinition *def = ItemRegistry::find(s.itemId);
        if (def && def->hasTag(tag)) return i;
    }
    return -1;
}

float InventorySystem::usedWeight(const Bag *bag) {
    if (!bag) return 0.f;
    float w = 0.f;
    for (const auto &s : bag->slots()) {
        if (s.empty()) continue;
        const ItemDefinition *def = ItemRegistry::find(s.itemId);
        if (def) w += def->weight * float(s.quantity);
    }
    return w;
}

float InventorySystem::usedVolume(const Bag *bag) {
    if (!bag) return 0.f;
    float v = 0.f;
    for (const auto &s : bag->slots()) {
        if (s.empty()) continue;
        const ItemDefinition *def = ItemRegistry::find(s.itemId);
        if (def) v += def->volume * float(s.quantity);
    }
    return v;
}

int InventorySystem::usedSlotCount(const Bag *bag) {
    if (!bag) return 0;
    int n = 0;
    for (const auto &s : bag->slots()) {
        if (!s.empty()) ++n;
    }
    return n;
}

void InventorySystem::clearBag(Bag *bag) {
    if (!bag) return;
    for (auto &s : bag->slots()) s.clear();
    InventoryChangeEvent ev;
    ev.action = "remove";
    ev.bagId = bag->getId();
    ev.quantity = 0;
    emit(std::move(ev));
}

bool InventorySystem::equipFromBag(EquipmentSet *eq, const std::string &equipSlot, Bag *bag,
                                    int bagSlot) {
    if (!eq || !bag) return false;
    std::string reason;
    if (!eq->canEquipFromBag(bag, bagSlot, equipSlot, &reason)) return false;

    auto &bs = bag->slots()[size_t(bagSlot)];
    ItemStack moving = bs;
    // Equipment slots hold one logical item (quantity preserved but typically 1).
    bs.clear();

    auto *dest = eq->stackAt(equipSlot);
    if (!dest) {
        // restore
        bag->slots()[size_t(bagSlot)] = moving;
        return false;
    }

    // If slot occupied, try swap into bagSlot (now empty).
    if (!dest->empty()) {
        bag->slots()[size_t(bagSlot)] = *dest;
    }
    *dest = moving;

    InventoryChangeEvent ev;
    ev.action = "equip";
    ev.bagId = bag->getId();
    ev.itemId = dest->itemId;
    ev.quantity = dest->quantity;
    ev.slot = bagSlot;
    ev.equipSlot = equipSlot;
    emit(std::move(ev));
    return true;
}

bool InventorySystem::unequipToBag(EquipmentSet *eq, const std::string &equipSlot, Bag *bag) {
    if (!eq || !bag) return false;
    auto *src = eq->stackAt(equipSlot);
    if (!src || src->empty()) return false;

    // Find empty slot first to preserve instance/props.
    int empty = -1;
    for (int i = 0; i < bag->getSlotCount(); ++i) {
        if (bag->slots()[size_t(i)].empty()) {
            empty = i;
            break;
        }
    }
    if (empty < 0) {
        // Fall back to addItem (may stack, losing instance identity).
        if (!canAdd(bag, src->itemId, src->quantity, nullptr)) return false;
        std::string itemId = src->itemId;
        int qty = src->quantity;
        src->clear();
        int added = addItem(bag, itemId, qty);
        if (added < qty) {
            // restore
            src->itemId = itemId;
            src->quantity = qty - added;
            src->instanceId = nextInstanceId();
        }
        InventoryChangeEvent ev;
        ev.action = "unequip";
        ev.bagId = bag->getId();
        ev.itemId = itemId;
        ev.quantity = added;
        ev.equipSlot = equipSlot;
        emit(std::move(ev));
        return added > 0;
    }

    bag->slots()[size_t(empty)] = *src;
    std::string itemId = src->itemId;
    int qty = src->quantity;
    src->clear();

    InventoryChangeEvent ev;
    ev.action = "unequip";
    ev.bagId = bag->getId();
    ev.itemId = itemId;
    ev.quantity = qty;
    ev.slot = empty;
    ev.equipSlot = equipSlot;
    emit(std::move(ev));
    return true;
}

}  // namespace eve::inventory
