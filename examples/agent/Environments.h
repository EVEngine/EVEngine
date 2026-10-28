#pragma once

#include "agent/Agent.h"
#include "grid/GridProjection.h"
#include "ui/Layout.h"

#include <algorithm>
#include <cmath>

namespace agent_examples {

// A resettable grid game: move right/down to (5,5). The production grid
// projection is exercised on every action; reward and invariants are separate.
class GridGame final : public eve::agent::IEnvironment {
public:
    eve::Result<eve::agent::Observation> reset(std::uint64_t seed) override {
        x_ = y_       = 0;
        config_.cellW = 16.f + float(seed % 4);
        return eve::Result<eve::agent::Observation>::success(observe(0));
    }
    eve::Result<eve::agent::Observation> step(std::uint32_t action, double) override {
        const int before = x_ + y_;
        if (action == 0) x_ = std::min(5, x_ + 1);
        if (action == 1) y_ = std::min(5, y_ + 1);
        if (action == 2) x_ = std::max(0, x_ - 1);
        if (action == 3) y_ = std::max(0, y_ - 1);
        return eve::Result<eve::agent::Observation>::success(observe(double(x_ + y_ - before)));
    }

private:
    eve::agent::Observation observe(double reward) const {
        using namespace eve::agent;
        Observation o;
        o.features     = {float(x_) / 5, float(y_) / 5};
        o.legalActions = {0, 1, 2, 3};
        o.coverage     = {"cell:" + std::to_string(x_) + ":" + std::to_string(y_)};
        o.reward       = reward;
        float wx = 0, wy = 0;
        eve::grid::cellToWorld(config_, x_, y_, wx, wy);
        int actualX = -1, actualY = -1;
        eve::grid::worldToCell(config_, wx + config_.cellW / 2, wy + config_.cellH / 2, actualX, actualY, 6, 6);
        if (actualX != x_ || actualY != y_) {
            o.outcome = Outcome::Failure;
            o.finding = "grid.rectangle.roundtrip";
        } else if (x_ == 5 && y_ == 5)
            o.outcome = Outcome::Success;
        return o;
    }
    eve::grid::GridConfig config_;
    int                   x_ = 0, y_ = 0;
};

// Random/stateful testing of the actual retained-UI layout algorithm. This
// checks numeric layout contracts; it does not claim rendered visual coverage.
class UiLayout final : public eve::agent::IEnvironment {
public:
    eve::Result<eve::agent::Observation> reset(std::uint64_t seed) override {
        width_ = 100 + int(seed % 100);
        count_ = 2;
        row_   = true;
        gap_   = 0;
        return eve::Result<eve::agent::Observation>::success(observe());
    }
    eve::Result<eve::agent::Observation> step(std::uint32_t action, double) override {
        switch (action) {
            case 0: width_ = std::max(0, width_ - 20); break;
            case 1: width_ = std::min(1000, width_ + 20); break;
            case 2: count_ = count_ % 8 + 1; break;
            case 3: row_ = !row_; break;
            case 4: gap_ = (gap_ + 3) % 21; break;
        }
        return eve::Result<eve::agent::Observation>::success(observe());
    }

private:
    eve::agent::Observation observe() const {
        using namespace eve::agent;
        std::vector<eve::ui::FlexItemSpec> items(count_);
        for (auto& item : items) {
            item.basisMain  = 10;
            item.basisCross = 15;
            item.flexGrow   = 1;
        }
        const auto  layout = eve::ui::flexArrange(row_, float(gap_), float(width_), 100, eve::ui::FlexAlign::Start,
                                                  eve::ui::FlexJustify::Start, items);
        Observation o;
        o.features     = {float(width_) / 1000, float(count_) / 8, row_ ? 1.f : 0.f, float(gap_) / 21};
        o.legalActions = {0, 1, 2, 3, 4};
        o.coverage     = {"width:" + std::to_string(width_ / 20), "children:" + std::to_string(count_),
                          row_ ? "row" : "column", "gap:" + std::to_string(gap_)};
        for (const auto& rect : layout.items) {
            if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.w) || !std::isfinite(rect.h) ||
                rect.w < 0 || rect.h < 0) {
                o.outcome = Outcome::Failure;
                o.finding = "ui.flex.invalid-rectangle";
            }
        }
        return o;
    }
    int  width_ = 100, count_ = 2, gap_ = 0;
    bool row_ = true;
};

}  // namespace agent_examples
