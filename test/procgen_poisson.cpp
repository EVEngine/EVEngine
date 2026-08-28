#include "procgen/PointSet.h"
#include "procgen/Procgen.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <utility>

using namespace eve::procgen;

namespace {

/** @brief Owns a Procgen point-set handle for one test's lifetime. */
class PointSetLease {
public:
    PointSetLease() = default;
    PointSetLease(Procgen& owner, ProcgenPointSetHandleRef handle) : owner_(&owner), handle_(handle) {}

    PointSetLease(const PointSetLease&)            = delete;
    PointSetLease& operator=(const PointSetLease&) = delete;
    PointSetLease(PointSetLease&& other) noexcept : owner_(other.owner_), handle_(other.handle_) {
        other.owner_  = nullptr;
        other.handle_ = {};
    }
    PointSetLease& operator=(PointSetLease&& other) noexcept {
        if (this == &other) return *this;
        reset();
        owner_        = other.owner_;
        handle_       = other.handle_;
        other.owner_  = nullptr;
        other.handle_ = {};
        return *this;
    }
    ~PointSetLease() { reset(); }

    /** @brief Releases the owned handle and explicitly observes the cleanup Result. */
    void reset() noexcept {
        if (!owner_ || !handle_.isValid()) return;
        auto result = owner_->releasePointSet(handle_);
        result.ignore("test point-set cleanup");
        owner_  = nullptr;
        handle_ = {};
    }

    /** @brief Resolves the handle for the duration of the current test operation. */
    [[nodiscard]] eve::script::Borrowed<PointSet> view() const noexcept {
        return owner_ ? owner_->resolvePointSet(handle_) : eve::script::Borrowed<PointSet>();
    }

private:
    Procgen*                 owner_ = nullptr;
    ProcgenPointSetHandleRef handle_{};
};

/** @brief Converts a checked point-set allocation Result into a test lease. */
PointSetLease requirePointSet(Procgen& proc, eve::Result<ProcgenPointSetHandleRef>&& result) {
    const bool ok = result.ok();
    if (!ok) {
        const eve::Diagnostic* diagnostic = result.error();
        REQUIRE(diagnostic != nullptr);
    }
    REQUIRE(ok);
    return PointSetLease(proc, std::move(result).takeValue());
}

}  // namespace

TEST_CASE("procgen.poisson.deterministicAndSeeded") {
    Procgen proc;

    auto a     = requirePointSet(proc, proc.poissonDiskHandle(40, 30, 2.5f, 99, 300));
    auto b     = requirePointSet(proc, proc.poissonDiskHandle(40, 30, 2.5f, 99, 300));
    auto c     = requirePointSet(proc, proc.poissonDiskHandle(40, 30, 2.5f, 100, 300));
    auto aView = a.view();
    auto bView = b.view();
    auto cView = c.view();
    REQUIRE(aView.isBound());
    REQUIRE(bView.isBound());
    REQUIRE(cView.isBound());
    CHECK_EQ(aView->getCount(), bView->getCount());
    CHECK(aView->getCount() > 0);
    for (int i = 0; i < aView->getCount(); ++i) {
        CHECK_EQ(aView->getX(i), bView->getX(i));
        CHECK_EQ(aView->getZ(i), bView->getZ(i));
    }
}

TEST_CASE("procgen.poisson.respectsMinSpacing") {
    Procgen proc;
    auto    points = requirePointSet(proc, proc.poissonDiskHandle(50, 50, 2.f, 7, 400));
    auto    view   = points.view();
    REQUIRE(view.isBound());
    CHECK(view->getCount() > 1);

    const float minDist = 1.9f;  // allow a little float slack
    for (int i = 0; i < view->getCount(); ++i) {
        for (int j = i + 1; j < view->getCount(); ++j) {
            const float dx = view->getX(i) - view->getX(j);
            const float dz = view->getZ(i) - view->getZ(j);
            const float d  = std::sqrt(dx * dx + dz * dz);
            CHECK(d >= minDist);
        }
    }
}

TEST_CASE("procgen.poisson.maxPointsCapsOutput") {
    Procgen proc;
    auto    capped     = requirePointSet(proc, proc.poissonDiskHandle(100, 100, 0.5f, 3, 50));
    auto    cappedView = capped.view();
    REQUIRE(cappedView.isBound());
    CHECK(cappedView->getCount() <= 50);

    auto tiny     = requirePointSet(proc, proc.poissonDiskHandle(0, 0, 1.f, 3, 100));
    auto tinyView = tiny.view();
    REQUIRE(tinyView.isBound());
    CHECK_EQ(tinyView->getCount(), 0);
}

TEST_CASE("procgen.poisson.allPointsInsideArea") {
    Procgen proc;
    auto    points = requirePointSet(proc, proc.poissonDiskHandle(20, 20, 1.5f, 11, 200));
    auto    view   = points.view();
    REQUIRE(view.isBound());
    for (int i = 0; i < view->getCount(); ++i) {
        CHECK(view->getX(i) >= 0.f);
        CHECK(view->getX(i) <= 20.f);
        CHECK(view->getZ(i) >= 0.f);
        CHECK(view->getZ(i) <= 20.f);
        CHECK_EQ(view->getY(i), 0.f);
    }
}
