#include "editor/EditorSimulationPreview.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

namespace {
class FakeSimulation final : public IEditorSimulationBackend {
public:
    std::unique_ptr<IEditorSimulationBackend> cloneForPreview() const override {
        return std::make_unique<FakeSimulation>(*this);
    }
    EditorResult<void> step(std::uint64_t tick, double fixedDelta) override {
        if (tick <= tick_)
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("fake.tick"), "Ticks must increase");
        tick_ = tick;
        position_ += fixedDelta * 3.0;
        return eve::editing::applied<void>();
    }
    EditorResult<std::vector<SimulationObjectSample>> capture() const override {
        SimulationObjectSample sample;
        sample.object    = "body-1";
        sample.positionX = position_;
        return eve::editing::applied<std::vector<SimulationObjectSample>>({sample});
    }
    double position() const { return position_; }

private:
    std::uint64_t tick_     = 0;
    double        position_ = 0.0;
};
}  // namespace

TEST_CASE("editor.simulation.preview_pauses_steps_and_bakes_without_mutating_source") {
    FakeSimulation              source;
    SimulationPreviewController controller;
    CHECK_EQ(static_cast<int>(controller.update(source).code()), static_cast<int>(EditorStatus::NoOp));
    REQUIRE(controller.setFixedDelta(0.5).ok());
    auto frame = controller.singleStep(source);
    REQUIRE(frame.ok());
    CHECK_EQ(frame.value().objects[0].positionX, 1.5);
    frame = controller.singleStep(source);
    REQUIRE(frame.ok());
    CHECK_EQ(frame.value().objects[0].positionX, 3.0);
    CHECK_EQ(source.position(), 0.0);
    auto baked = controller.bake(source, 42, 3, 10);
    CHECK_EQ(static_cast<int>(baked.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(baked.sourceRevision, 42U);
    CHECK_EQ(baked.frames.size(), 4U);
    CHECK_EQ(baked.frames.back().objects[0].positionX, 4.5);
    CHECK_EQ(source.position(), 0.0);
    CHECK_EQ(static_cast<int>(controller.bake(source, 42, 11, 10).status), static_cast<int>(EditorStatus::Rejected));
}
