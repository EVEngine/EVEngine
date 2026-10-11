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

// ---- 非 float 形状：vec2 / vec3 / color / quat 的脚本 builder + 脚本 sink ----

vec2Sum <- -1.0
vec3Sum <- -1.0
colorSum <- -1.0
colorLastA <- -1.0
quatSum <- -1.0

class Vec2Sink { function write(x, y) { vec2Sum = x + y } }
class Vec3Sink { function write(x, y, z) { vec3Sum = x + y + z } }
class ColorSink { function write(r, g, b, a) { colorSum = r + g + b; colorLastA = a } }
class QuatSink { function write(x, y, z, w) { quatSum = x + y + z + w } }

vec2Sink <- Vec2Sink()
vec3Sink <- Vec3Sink()
colorSink <- ColorSink()
quatSink <- QuatSink()

// 四种形状各起一条，全部绑脚本 sink：这就是"最后四个 IMotion*Sink 也接上了"。
function startNonFloatMotions() {
    local anim = eve.Animation()
    local two = anim.newMotionVec2({ x = 0.0, y = 0.0 }, { x = 2.0, y = 4.0 }, 1.0).bind(vec2Sink)
    local three = anim.newMotionVec3({ x = 0.0, y = 0.0, z = 0.0 },
                                     { x = 1.0, y = 2.0, z = 3.0 }, 1.0).bind(vec3Sink)
    local color = anim.newMotionColor({ r = 0.0, g = 0.0, b = 0.0, a = 0.5 },
                                      { r = 1.0, g = 2.0, b = 3.0, a = 1.0 }, 1.0).bind(colorSink)
    // 四元数走 slerp：端点必须是**单位**四元数，否则它插到的位置不是你以为的那个（见用例断言）。
    local quat = anim.newMotionQuat({ x = 0.0, y = 0.0, z = 0.0, w = 1.0 },
                                    { x = 0.0, y = 0.0, z = 0.70710678, w = 0.70710678 }, 1.0).bind(quatSink)
    return two != null && three != null && color != null && quat != null
}

// 形状不对的表：要抛，而不是悄悄按全零跑一条动画。
function startMotionWithBadTable() {
    local anim = eve.Animation()
    try {
        local m = anim.newMotionVec3({ x = 0.0, y = 0.0 }, { x = 1.0, y = 1.0, z = 1.0 }, 1.0)
        return m != null
    } catch (e) {
        tableError = e
        return false
    }
}
tableError <- ""
)SQ";

class MotionScriptSinkTest : public ScriptTest {
public:
    MotionScriptSinkTest() : ScriptTest(kScriptMotionSink) { ScriptProviderRegistry::instance().clearAll(); }

    ~MotionScriptSinkTest() {
        eve::animation::clearScriptMotionSinkAdapters();
        ScriptProviderRegistry::instance().clearAll();
    }

    /** 推进一次动画：`Animation::advance` 是唯一的求值入口。 */
    void advanceAnimation(eve::animation::Animation& target, double seconds, std::uint64_t tick) {
        const eve::SimulationStep step{eve::SimulationTick{tick}, eve::Duration::fromSeconds(seconds).expect("dt")};
        auto                      result = target.advance(step);
        REQUIRE(result.ok());
    }
};

}  // namespace

TEST_CASE_FIXTURE(MotionScriptSinkTest, "animation.motion.scriptSinkReceivesEveryEvaluatedValue") {
    REQUIRE(callScript("startMotionWithSink"));
    eve::animation::Animation* module = eve::animation::Animation::create();
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
    eve::animation::Animation* module = eve::animation::Animation::create();
    REQUIRE(module != nullptr);
    REQUIRE_EQ(module->motions().activeCount(), 1);
    advanceAnimation(*module, 0.5, 1);
    REQUIRE_EQ(vm.find("writeCount").toInt(), 0);
    REQUIRE_EQ(ScriptProviderRegistry::instance().size(), std::size_t{0});
}

TEST_CASE_FIXTURE(MotionScriptSinkTest, "animation.motion.nonFloatShapesReachScriptSinksToo") {
    REQUIRE(callScript("startNonFloatMotions"));
    eve::animation::Animation* module = eve::animation::Animation::create();
    REQUIRE(module != nullptr);
    REQUIRE_EQ(module->motions().activeCount(), 4);

    // 四条一起跑：每次 advance 每个 sink 都收到自己那几维的值（位置参数编组）。
    advanceAnimation(*module, 0.25, 1);
    REQUIRE(vm.find("vec2Sum").toFloat() > 0.0);
    REQUIRE(vm.find("vec3Sum").toFloat() > 0.0);
    REQUIRE(vm.find("colorSum").toFloat() > 0.0);
    REQUIRE(vm.find("colorLastA").toFloat() > 0.0);
    REQUIRE(vm.find("quatSum").toFloat() > 0.0);

    // 跑到 1 秒结束：终点值就是 to 那一端（vec2 6、vec3 6、color 6/1、单位四元数 z+w≈1.414）。
    advanceAnimation(*module, 1.0, 2);
    REQUIRE_EQ(module->motions().activeCount(), 0);
    REQUIRE(std::fabs(vm.find("vec2Sum").toFloat() - 6.0) < 1e-3);
    REQUIRE(std::fabs(vm.find("vec3Sum").toFloat() - 6.0) < 1e-3);
    REQUIRE(std::fabs(vm.find("colorSum").toFloat() - 6.0) < 1e-3);
    REQUIRE(std::fabs(vm.find("colorLastA").toFloat() - 1.0) < 1e-3);
    REQUIRE(std::fabs(vm.find("quatSum").toFloat() - 1.41421356) < 1e-3);
}

TEST_CASE_FIXTURE(MotionScriptSinkTest, "animation.motion.badValueTablesAreRejectedNotZeroFilled") {
    // 缺字段的表要明确抛错：静默按 0 跑一条动画会让"我传错了"变成"动画看起来没生效"。
    REQUIRE(!callScript("startMotionWithBadTable"));
    REQUIRE(!vm.find("tableError").toString().empty());
    eve::animation::Animation* module = eve::animation::Animation::create();
    REQUIRE(module != nullptr);
    REQUIRE_EQ(module->motions().activeCount(), 0);
}

TEST_CASE_FIXTURE(MotionScriptSinkTest, "animation.motion.scriptSinkFailureCancelsInsteadOfBeingSwallowed") {
    eve::animation::Animation* module = eve::animation::Animation::create();
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
