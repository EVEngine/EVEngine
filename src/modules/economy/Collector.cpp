#include "economy/Collector.h"

#include "common/Assert.h"
#include "economy/EconomySystem.h"
#include "economy/GatherNode.h"

#include <algorithm>

namespace eve::economy {

Collector::Collector(int carryCapacity, int gatherRatePerTick, int travelTicks)
    : carryCapacity_(carryCapacity), gatherRatePerTick_(gatherRatePerTick),
      travelTicks_(travelTicks) {
    EV_PARAM_CHECK(carryCapacity > 0, "carry capacity must be > 0");
    EV_PARAM_CHECK(gatherRatePerTick > 0, "gather rate must be > 0");
    EV_PARAM_CHECK(travelTicks >= 0, "travel ticks must be >= 0");
}

bool Collector::assign(GatherNode* node) {
    if (node == node_) return true;
    if (node_) {
        node_->releaseSlot();
        node_ = nullptr;
    }
    if (node == nullptr || !node->tryOccupySlot()) {
        state_ = State::Idle;
        return false;
    }
    node_   = node;
    type_   = node->resourceType();
    cargo_  = 0;
    startMoveToNode();
    return true;
}

void Collector::clearAssignment() {
    if (node_) node_->releaseSlot();
    node_  = nullptr;
    state_ = State::Idle;
}

void Collector::tick(int player) {
    if (state_ == State::Idle) return;
    if (node_ == nullptr) {
        state_ = State::Idle;
        return;
    }
    switch (state_) {
        case State::MovingToNode:
            if (--travelRemaining_ <= 0) state_ = State::Gathering;
            break;
        case State::Gathering: {
            const int freeSpace = carryCapacity_ - cargo_;
            const int take = std::min({freeSpace, node_->amount(), gatherRatePerTick_});
            if (take > 0) cargo_ += node_->extract(take);
            if (cargo_ >= carryCapacity_ || node_->depleted()) startMoveToDrop();
            break;
        }
        case State::MovingToDrop:
            if (--travelRemaining_ <= 0) state_ = State::Depositing;
            break;
        case State::Depositing:
            deposit(player);
            break;
    }
}

bool Collector::isIdle() const { return state_ == State::Idle; }

Collector::State Collector::state() const { return state_; }

std::string Collector::stateName() const {
    switch (state_) {
        case State::Idle:
            return "idle";
        case State::MovingToNode:
            return "moving_to_node";
        case State::Gathering:
            return "gathering";
        case State::MovingToDrop:
            return "moving_to_drop";
        case State::Depositing:
            return "depositing";
    }
    return "idle";
}

int Collector::cargo() const { return cargo_; }

int Collector::carryCapacity() const { return carryCapacity_; }

std::string Collector::resourceType() const { return type_; }

int Collector::totalGathered() const { return totalGathered_; }

int Collector::trips() const { return trips_; }

void Collector::startMoveToNode() {
    state_           = State::MovingToNode;
    travelRemaining_ = travelTicks_;
}

void Collector::startMoveToDrop() {
    state_           = State::MovingToDrop;
    travelRemaining_ = travelTicks_;
}

void Collector::deposit(int player) {
    if (cargo_ > 0) {
        totalGathered_ += EconomySystem::credit(player, type_, cargo_);
        ++trips_;
        cargo_ = 0;
    }
    if (node_ && !node_->depleted()) {
        startMoveToNode();
    } else {
        if (node_) {
            node_->releaseSlot();
            node_ = nullptr;
        }
        state_ = State::Idle;
    }
}

void Collector::destroy() { delete this; }

}  // namespace eve::economy
