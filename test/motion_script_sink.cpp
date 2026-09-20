#include "ScriptTest.h"

#include "animation/Animation.h"
#include "animation/MotionRuntime.h"
#include "animation/MotionScriptBindings.h"
#include "common/ScriptProvider.h"
#include "common/Time.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// 阶段 5 的最后一个插座：动画侧的 `IMotionFloatSink` 可以由脚本对象扮演。
//
// 这一条的形态和其它插座不同：`MotionRuntime` 的槽位**直接持有** sink 指针（每个 motion 一个 sink，
// 不是全局能力槽），所以接法不是 `cap::provide`，而是**让 builder 接受一个脚本 sink 对象**
// （§9.71.1 给的正是这一条路）。适配器实现 `IMotionFloatSink`，脚本对象实现 `write(value)`，而
// `write` 的失败会原样回到 runtime —— `cancelOnError` 因此对脚本 sink 同样有效。
//
// 类型全部写全限定名：这一 TU 里 `animation` 是命名空间，用 `using namespace eve::animation` 会与
// 局部变量名打架。
// ---------------------------------------------------------------------------

namespace {

using namespace eve;

const char* kScriptMotionSink = R"SQ(
received <- []
writeCount <- 0
lastValue <- -1.0

class RecordingSink {
    function write(value) {
        received.append(value)
        writeCount = writeCount + 1
        lastValue = value
    }
}

class BrokenSink {
    function write(value) { throw "motion sink refused the value" }
}

// 初始化值（from = 0）收下，之后任何一个值都拒绝：用来看"运行中失败"这条路。
class LateBrokenSink {
    function write(value) { if (value > 0.0) throw "motion sink refused a later value" }
}

sink <- RecordingSink()
broken <- BrokenSink()
lateBroken <- LateBrokenSink()
bindError <- ""

function startMotionWithSink() {
    local anim = eve.Animation()
    // `bind(sink)` = 绑上脚本 sink 并开跑（对应 C++ 的 MotionBuilder::bind）；
    // 要放进 sequence 就先 `to(sink)` 再 append，那条路不启动。
    local motion = anim.newMotion(0.0, 10.0, 1.0).bind(sink)
    return motion != null && motion.isActive()
}

// spawn 会先推一次初始化值，所以"一上来就拒绝"的 sink 会让 bind 自己失败。
function startMotionWithImmediateBrokenSink() {
    local anim = eve.Animation()
    try {
        local motion = anim.newMotion(0.0, 10.0, 1.0).bind(broken)
        return motion != null
    } catch (e) {
        bindError = e
        return false
    }
}

function startMotionWithLateBrokenSink() {
    local anim = eve.Animation()
    local motion = anim.newMotion(0.0, 10.0, 1.0).bind(lateBroken)
    return motion != null && motion.isActive()
}

function startMotionWithoutSink() {
    local anim = eve.Animation()
    local motion = anim.newMotion(0.0, 10.0, 1.0).run()
    return motion != null
}
)SQ";

class MotionScriptSinkTest : public ScriptTest {
public:
    MotionScriptSinkTest() : ScriptTest(kScriptMotionSink) {
        ScriptProviderRegistry::instance().clearAll();
    }

    ~MotionScriptSinkTest() {
        eve::animation::clearScriptMotionSinkAdapters();
        ScriptProviderRegistry::instance().clearAll();
    }

    /** 推进一次动画：`Animation::advance` 是唯一的求值入口。 */
    void advanceAnimation(eve::animation::Animation &target, double seconds, std::uint64_t tick) {
        const eve::SimulationStep step{eve::SimulationTick{tick}, eve::Duration::fromSeconds(seconds).expect("dt")};
        auto                      result = target.advance(step);
        REQUIRE(result.ok());
    }
};

}  // namespace

TEST_CASE_FIXTURE(MotionScriptSinkTest, "animation.motion.scriptSinkReceivesEveryEvaluatedValue") {
    REQUIRE(callScript("startMotionWithSink"));
    eve::animation::Animation *module = eve::animation::Animation::create();
    REQUIRE(module != nullptr);
    REQUIRE_EQ(module->motions().activeCount(), 1);

    advanceAnimation(*module, 0.25, 1);
    advanceAnimation(*module, 0.25, 2);
    advanceAnimation(*module, 0.25, 3);

    // 每次 advance 都推一个值，而且单调上升（0→10 的线性 tween）。
    REQUIRE(vm.find("writeCount").toInt() >= 3);
    REQUIRE_EQ(vm.find("received").getType(), ssq::Type::ARRAY);
    const double last = vm.find("lastValue").toFloat();
    REQUIRE(last > 0.0);
    REQUIRE(last <= 10.0);

    // 跑完 1 秒的 tween：motion 结束，不再活跃。
    advanceAnimation(*module, 0.5, 4);
    REQUIRE_EQ(module->motions().activeCount(), 0);
    REQUIRE(std::fabs(vm.find("lastValue").toFloat() - 10.0) < 1e-3);

    // 撤回适配器与 provider 登记（模块析构走同一条路）。
    eve::animation::clearScriptMotionSinkAdapters();
    REQUIRE(!ScriptProviderRegistry::instance().contains("animation.motion-sink.1"));
}

TEST_CASE_FIXTURE(MotionScriptSinkTest, "animation.motion.sinkIsOptionalAndOnlyBindsWhenAsked") {
    // 不调用 `to()`：motion 照跑，脚本一次都不会被调（sink 是可选的，不是隐式默认）。
    REQUIRE(callScript("startMotionWithoutSink"));
    eve::animation::Animation *module = eve::animation::Animation::create();
    REQUIRE(module != nullptr);
    REQUIRE_EQ(module->motions().activeCount(), 1);
    advanceAnimation(*module, 0.5, 1);
    REQUIRE_EQ(vm.find("writeCount").toInt(), 0);
    REQUIRE_EQ(ScriptProviderRegistry::instance().size(), std::size_t{0});
}

TEST_CASE_FIXTURE(MotionScriptSinkTest, "animation.motion.scriptSinkFailureCancelsInsteadOfBeingSwallowed") {
    eve::animation::Animation *module = eve::animation::Animation::create();
    REQUIRE(module != nullptr);

    // ① sink 连初始化值都拒绝：`bind` 自己失败（脚本看得见），没有 motion 留在运行中。
    REQUIRE(!callScript("startMotionWithImmediateBrokenSink"));
    REQUIRE(!vm.find("bindError").toString().empty());
    REQUIRE_EQ(module->motions().activeCount(), 0);

    // ② sink 在后续采样上失败：`advance` 返回失败，并且 motion 被取消（不是继续跑在一个已经
    //    不再接收值的目标上——吞掉失败就会变成那样）。
    REQUIRE(callScript("startMotionWithLateBrokenSink"));
    REQUIRE_EQ(module->motions().activeCount(), 1);
    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.25).expect("dt")};
    const auto                advanced = module->advance(step);
    REQUIRE(!advanced.ok());
    REQUIRE_EQ(module->motions().activeCount(), 0);
}
