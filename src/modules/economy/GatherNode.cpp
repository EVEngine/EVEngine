#include "economy/GatherNode.h"

#include "common/Assert.h"

#include <algorithm>
#include <utility>

namespace eve::economy {

GatherNode::GatherNode(std::string type, int capacity, int workerSlots, int regenPerTick)
    : type_(std::move(type)), capacity_(capacity), amount_(capacity), regenPerTick_(regenPerTick),
      workerSlots_(workerSlots) {
    EV_PARAM_CHECK(capacity >= 0, "node capacity must be >= 0");
    EV_PARAM_CHECK(workerSlots > 0, "node workerSlots must be > 0");
    EV_PARAM_CHECK(regenPerTick >= 0, "node regenPerTick must be >= 0");
    if (capacity < 0) capacity_ = amount_ = 0;
    if (workerSlots <= 0) workerSlots_ = 1;
    if (regenPerTick < 0) regenPerTick_ = 0;
}

std::string GatherNode::resourceType() const { return type_; }

int GatherNode::amount() const { return amount_; }

int GatherNode::capacity() const { return capacity_; }

bool GatherNode::depleted() const { return amount_ <= 0; }

int GatherNode::extract(int amount) {
    if (amount <= 0 || amount_ <= 0) return 0;
    const int take = std::min(amount, amount_);
    amount_ -= take;
    return take;
}

bool GatherNode::tryOccupySlot() {
    if (occupiedSlots_ >= workerSlots_) return false;
    ++occupiedSlots_;
    return true;
}

void GatherNode::releaseSlot() {
    EV_ASSERT(occupiedSlots_ > 0, "releaseSlot without an occupied slot");
    if (occupiedSlots_ > 0) --occupiedSlots_;
}

int GatherNode::freeSlots() const { return workerSlots_ - occupiedSlots_; }

void GatherNode::regenTick() {
    if (regenPerTick_ > 0 && amount_ < capacity_)
        amount_ = std::min(capacity_, amount_ + regenPerTick_);
}

void GatherNode::destroy() { delete this; }

}  // namespace eve::economy
