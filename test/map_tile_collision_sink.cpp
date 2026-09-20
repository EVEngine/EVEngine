#include "ScriptTest.h"

#include "common/Capability.h"
#include "common/ScriptProvider.h"
#include "map/Map.h"
#include "map/TileCollision.h"
#include "map/TileCollisionScriptSink.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// 决定③ 的第三个落地插座：`ITileCollisionSink` 可以由脚本提供。
//
// 这一片的前置是**接口改**（§9.71.1）：参数从 `const void* layer` 换成稳定身份 `TileLayerRef`
// （ECS 槽位 + 换代号）。脚本因此既能认出"这批矩形是哪一层的"，也看得懂过期引用。派发走的是既有
// 的 listener 面（`Map::publishCollision` 用 `cap::forEach`），所以原生 sink 与脚本 sink 可以同时
// 在位、按优先级依次收到同一批几何。
// ---------------------------------------------------------------------------

namespace {

using namespace eve;
using namespace eve::map;

/** 脚本侧记录的层身份形状：`"<槽位>:<换代号>,"`。 */
std::string formatLayer(TileLayerRef layer) {
    return std::to_string(layer.slot) + ":" + std::to_string(layer.generation) + ",";
}

const char* kScriptTileCollisionSink = R"SQ(
calls <- 0
seenLayerId <- -1
seenLayerGeneration <- -1
seenCount <- -1
seenX <- -1.0
seenY <- -1.0
seenWidth <- -1.0
seenHeight <- -1.0
recordedLayers <- ""
recordedSlots <- ""
selfLayerId <- -1
selfLayerGeneration <- -1
selfRects <- -1

class CollisionSink {
    function replaceTileCollision(payload) {
        calls = calls + 1
        seenLayerId = payload.layer.id
        seenLayerGeneration = payload.layer.generation
        seenCount = payload.rects.len()
        if (seenCount > 0) {
            seenX = payload.rects[0].x
            seenY = payload.rects[0].y
            seenWidth = payload.rects[0].width
            seenHeight = payload.rects[0].height
        }
    }
}

// 记录每一次发布的层身份：用来看"脚本认得出这是哪一层"。
class RecordingSink {
    function replaceTileCollision(payload) {
        recordedLayers = recordedLayers + format("%d:%d,", payload.layer.id, payload.layer.generation)
        recordedSlots = recordedSlots + format("%d,", payload.rects.len())
    }
}

class BrokenSink {
    function replaceTileCollision(payload) { throw "sink refused the geometry" }
}

class SelfSink {
    function replaceTileCollision(payload) {
        selfLayerId = payload.layer.id
        selfLayerGeneration = payload.layer.generation
        selfRects = payload.rects.len()
    }
}

sink <- CollisionSink()
recorder <- RecordingSink()
broken <- BrokenSink()
selfSink <- SelfSink()

// 整条回路都在脚本里：建层、发布 sink、派发、按 `getEntityId()` 认自己那一层、退订。
// 这就是这一片的验收点——**不需要任何 C++ 代码**。
function runCollisionLoopFromScript() {
    local map = eve.Map()
    local layer = map.newLayer(2, 2, 16.0, 16.0)
    if (layer == null) return false
    // 绑定要求传满每个实参（R-FACE-6）：`cost` 在 C++ 侧有默认值，脚本侧必须显式给。
    layer.setTileMetadata(9, 1, 1, false, 1.0)
    layer.fillRect(0, 0, 2, 1, 9)
    local published = eve.provider.publishFor("ITileCollisionSink", "test.collision.self", selfSink)
    if (!published.ok) return false
    local rects = map.publishCollision(layer)
    if (rects != 1) return false
    if (selfLayerId != layer.getEntityId()) return false
    if (selfLayerGeneration != layer.getEntityGeneration()) return false
    if (selfRects != 1) return false
    local unloaded = eve.provider.unload("test.collision.self")
    return unloaded.ok
}
)SQ";

}  // namespace

/** 接缝由 `Map` 模块构造时声明，所以夹具只准备 VM 与隔离。 */
class MapTileCollisionSinkTest : public ScriptTest {
public:
    MapTileCollisionSinkTest() : ScriptTest(kScriptTileCollisionSink) {
        auto& registry = eve::ScriptProviderRegistry::instance();
        registry.clearAll();
        declaredBefore_ = registry.capabilities();
        eve::cap::detail::clearAllRaw();
        eve::map::resetScriptTileCollisionSinkFailures();
    }

    ~MapTileCollisionSinkTest() {
        eve::map::withdrawScriptTileCollisionSinkSeam();
        eve::cap::detail::clearAllRaw();
        auto& registry = eve::ScriptProviderRegistry::instance();
        registry.clearAll();
        for (const std::string& capability : registry.capabilities()) {
            if (std::find(declaredBefore_.begin(), declaredBefore_.end(), capability) != declaredBefore_.end())
                continue;
            registry.removeCapability(capability).ignore("fixture teardown withdraws only this case's seams");
        }
    }

    /** 发布一个脚本提供方；返回结果表，测试据此断言接线成没成。 */
    ssq::Object publish(const char* name, const char* instance) {
        const std::string source = std::string("published <- eve.provider.publishFor(\"ITileCollisionSink\", \"") + name +
                                   "\", " + instance + ")";
        vm.run(vm.compileSource(source.c_str()));
        return vm.find("published");
    }

private:
    std::vector<std::string> declaredBefore_;
};

TEST_CASE_FIXTURE(MapTileCollisionSinkTest, "map.collision.scriptSinkReceivesLayerIdentityAndGeometry") {
    Map        map;
    TileLayer* layer = map.newLayer(5, 4, 16.f, 8.f);
    REQUIRE(layer != nullptr);
    layer->setTileMetadata(9, 1, 1, false);
    layer->fillRect(1, 1, 3, 2, 9);

    REQUIRE(eve::ScriptProviderRegistry::instance().hasCapabilitySeam("ITileCollisionSink"));
    REQUIRE_EQ(eve::cap::listenerCount<ITileCollisionSink>(), std::size_t{0});

    ssq::Object published = publish("test.collision", "sink");
    REQUIRE_EQ(published.getType(), ssq::Type::TABLE);
    REQUIRE(published.find("ok").toBool());
    REQUIRE_EQ(eve::cap::listenerCount<ITileCollisionSink>(), std::size_t{1});

    // 合并出来的那一个矩形：原点 (16, 8)、尺寸 48x16。
    // 带副作用的调用先落到局部变量：`REQUIRE_EQ` 会把实参各求值两次（§9.76.3），
    // 直接把 `publishCollision` 写进断言会派发两次。
    const int publishedRects = map.publishCollision(layer);
    REQUIRE_EQ(publishedRects, 1);
    REQUIRE_EQ(vm.find("calls").toInt(), 1);
    REQUIRE_EQ(vm.find("seenLayerId").toInt(), int(layer->layerRef().slot));
    REQUIRE_EQ(vm.find("seenLayerGeneration").toInt(), int(layer->layerRef().generation));
    REQUIRE_EQ(vm.find("seenCount").toInt(), 1);
    REQUIRE_EQ(vm.find("seenX").toFloat(), 16.0);
    REQUIRE_EQ(vm.find("seenY").toFloat(), 8.0);
    REQUIRE_EQ(vm.find("seenWidth").toFloat(), 48.0);
    REQUIRE_EQ(vm.find("seenHeight").toFloat(), 16.0);

    // 脚本 sink 不会替换 map 自己的权威矩形表：`collisionRects_` 仍然是唯一权威。
    REQUIRE_EQ(map.getCollisionRectCount(), 1);
    REQUIRE_EQ(map.getCollisionRectWidth(0), 48.f);

    // 退订之后派发停止，而 map 侧的行为完全不变。
    vm.run(vm.compileSource("unloaded <- eve.provider.unload(\"test.collision\")"));
    REQUIRE(vm.find("unloaded").find("ok").toBool());
    REQUIRE_EQ(eve::cap::listenerCount<ITileCollisionSink>(), std::size_t{0});
    const int rectsAfterUnload = map.publishCollision(layer);
    REQUIRE_EQ(rectsAfterUnload, 1);
    REQUIRE_EQ(vm.find("calls").toInt(), 1);
}

TEST_CASE_FIXTURE(MapTileCollisionSinkTest, "map.collision.scriptSinkCanTellLayersApart") {
    Map        map;
    TileLayer* first  = map.newLayer(3, 3, 16.f, 16.f);
    TileLayer* second = map.newLayer(3, 3, 16.f, 16.f);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(first->layerRef().slot != second->layerRef().slot);

    first->setTileMetadata(9, 1, 1, false);
    first->setTile(0, 0, 9);
    second->setTileMetadata(9, 1, 1, false);
    second->setTile(0, 0, 9);
    second->setTile(2, 0, 9);  // 与 (0,0) 不相邻，所以合并后是两个矩形

    ssq::Object published = publish("test.collision.recording", "recorder");
    REQUIRE(published.find("ok").toBool());

    const int firstRects  = map.publishCollision(first);
    const int secondRects = map.publishCollision(second);
    REQUIRE_EQ(firstRects, 1);
    REQUIRE_EQ(secondRects, 2);

    // 层身份进得来，所以脚本分得清两次发布属于哪一层（这正是换掉 `const void*` 的收益）。
    const std::string expectedLayers =
        formatLayer(first->layerRef()) + formatLayer(second->layerRef());
    REQUIRE_EQ(vm.find("recordedLayers").toString(), expectedLayers);
    REQUIRE_EQ(vm.find("recordedSlots").toString(), std::string("1,2,"));
}

TEST_CASE_FIXTURE(MapTileCollisionSinkTest, "map.collision.scriptSinkFailureIsCountedNotSwallowed") {
    Map        map;
    TileLayer* layer = map.newLayer(5, 4, 16.f, 8.f);
    REQUIRE(layer != nullptr);
    layer->setTileMetadata(9, 1, 1, false);
    layer->fillRect(1, 1, 3, 2, 9);

    ssq::Object published = publish("test.collision.broken", "broken");
    REQUIRE(published.find("ok").toBool());
    REQUIRE_EQ(eve::map::scriptTileCollisionSinkFailures(), std::uint64_t{0});

    // 接口是 `void`（通知不是事务），所以"脚本拒绝了"必须从计数上看出来——否则"没接上"和
    // "接上了但每次都失败"从外部完全一样。
    const int rejectedRects = map.publishCollision(layer);
    REQUIRE_EQ(rejectedRects, 1);
    REQUIRE_EQ(eve::map::scriptTileCollisionSinkFailures(), std::uint64_t{1});
    REQUIRE_EQ(map.getCollisionRectCount(), 1);

    // 提供方在失败后还活着：同一代继续派发，计数继续涨。
    const int rejectedAgain = map.publishCollision(layer);
    REQUIRE_EQ(rejectedAgain, 1);
    REQUIRE_EQ(eve::map::scriptTileCollisionSinkFailures(), std::uint64_t{2});

    // 换一个能用的脚本 sink：派发恢复，计数不再变。
    REQUIRE(eve::ScriptProviderRegistry::instance().unload(eve::ScriptProviderHandle{
                "test.collision.broken", eve::ScriptProviderRegistry::instance().generationOf("test.collision.broken")})
                .ok());
    REQUIRE(publish("test.collision.good", "sink").find("ok").toBool());
    const int recoveredRects = map.publishCollision(layer);
    REQUIRE_EQ(recoveredRects, 1);
    REQUIRE_EQ(eve::map::scriptTileCollisionSinkFailures(), std::uint64_t{2});
    REQUIRE_EQ(vm.find("calls").toInt(), 1);
}

TEST_CASE_FIXTURE(MapTileCollisionSinkTest, "map.collision.scriptOwnsTheWholeCollisionLoop") {
    // 建层、发布、派发、按身份认层、退订全在脚本里：这就是"用户只写脚本"的验收形态。
    // 身份对不对由脚本自己比（`selfLayerId != layer.getEntityId()` 就返回 false）；这里只确认
    // 脚本确实收到了那次派发——槽位从 0 开始，所以断言用**收到过**而不是"大于 0"。
    REQUIRE(callScript("runCollisionLoopFromScript"));
    REQUIRE_EQ(vm.find("selfRects").toInt(), 1);
    REQUIRE(vm.find("selfLayerGeneration").toInt() >= 1);
    REQUIRE_EQ(eve::cap::listenerCount<ITileCollisionSink>(), std::size_t{0});
}

TEST_CASE_FIXTURE(MapTileCollisionSinkTest, "map.collision.scriptSinkSeamIsAbsentWithoutTheModule") {
    // 没有 `Map` 实例 = 模块从没声明过接缝：发布被明确拒绝（`not_found`），而不是"绑了但不响"。
    REQUIRE(!eve::ScriptProviderRegistry::instance().hasCapabilitySeam("ITileCollisionSink"));
    ssq::Object refused = publish("test.collision.orphan", "sink");
    REQUIRE_EQ(refused.getType(), ssq::Type::TABLE);
    REQUIRE(!refused.find("ok").toBool());
    REQUIRE_EQ(refused.find("code").toString(), std::string("not_found"));
    REQUIRE_EQ(eve::cap::listenerCount<ITileCollisionSink>(), std::size_t{0});
}
