#include "ScriptTest.h"

#include "common/ECS.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

static const char* kScriptEcsContent = R"SQ(
function testEcsReady() {
    return eve.ecsReady()
}

function testDefineClassAndComponent() {
    class Position extends eve.Component {
        x = 0.0
        y = 0.0
    }
    class Velocity extends eve.Component {
        x = 1.0
        y = 2.0
    }
    class Moveable extends eve.Entity {
        pos = Position
        vel = Velocity
    }

    local e = Moveable.create()
    if (e == null) return false
    if (!e.isAlive()) return false
    if (e.getId() <= 0) return false
    if (e.pos.x != 0.0 || e.pos.y != 0.0) return false
    if (e.vel.x != 1.0 || e.vel.y != 2.0) return false

    e.pos.x = 10.0
    e.vel.x = 3.0
    if (e.pos.x != 10.0) return false
    if (e.getComponent(Position).x != 10.0) return false
    if (!e.hasComponent(Velocity)) return false
    return true
}

function testViewIncludesSubclass() {
    class Health extends eve.Component {
        hp = 100
    }
    class Actor extends eve.Entity {
        hp = Health
    }
    class Enemy extends Actor {
    }

    local a = Actor.create()
    local b = Enemy.create()
    a.hp.hp = 50
    b.hp.hp = 80

    local actors = eve.view(Actor)
    if (actors.len() < 2) return false

    local enemies = eve.view(Enemy)
    if (enemies.len() < 1) return false

    local foundEnemy = false
    foreach (e in actors) {
        if (e == b) foundEnemy = true
    }
    if (!foundEnemy) return false
    return true
}

function testSystemUpdate() {
    class Position extends eve.Component {
        x = 0.0
    }
    class Velocity extends eve.Component {
        x = 5.0
    }
    class Mover extends eve.Entity {
        pos = Position
        vel = Velocity
    }
    class MoveSys extends eve.System {
        constructor() {
            base.constructor(Mover)
        }
        function update(dt) {
            foreach (e in entities()) {
                e.pos.x += e.vel.x * dt
            }
        }
    }

    local e = Mover.create()
    local sys = MoveSys()
    sys.update(2.0)
    if (e.pos.x != 10.0) return false
    e.destroy()
    if (e.isAlive()) return false
    // destroyed entities leave the view
    foreach (ent in eve.view(Mover)) {
        if (ent == e) return false
    }
    return true
}

function testSystemDynamicSpawnDespawn() {
    class Position extends eve.Component { x = 0.0 }
    class Velocity extends eve.Component { x = 1.0 }
    class Mover extends eve.Entity {
        pos = Position
        vel = Velocity
    }
    class MoveSys extends eve.System {
        constructor() { base.constructor(Mover) }
        function update(dt) {
            local dying = []
            foreach (e in entities()) {
                e.pos.x += e.vel.x * dt
                if (e.pos.x >= 2.0) dying.push(e)
            }
            foreach (e in dying) e.destroy()
        }
    }

    local sys = MoveSys()
    local a = Mover.create()
    local b = Mover.create()
    a.vel.x = 1.0
    b.vel.x = 1.0
    if (eve.view(Mover).len() != 2) return false

    sys.update(1.0)
    if (a.pos.x != 1.0 || b.pos.x != 1.0) return false
    if (eve.view(Mover).len() != 2) return false

    sys.update(1.0)
    if (a.isAlive() || b.isAlive()) return false
    if (eve.view(Mover).len() != 0) return false

    local c = Mover.create()
    c.vel.x = 1.0
    sys.update(0.5)
    if (!c.isAlive() || c.pos.x != 0.5) return false
    if (eve.view(Mover).len() != 1) return false

    // A later entities() call in the same update sees newly created instances.
    class SpawnSys extends eve.System {
        snapshot = 0
        constructor() { base.constructor(Mover) }
        function update(dt) {
            snapshot = entities().len()
            local extra = Mover.create()
            extra.pos.x = 99.0
            if (entities().len() != snapshot + 1) return false
            foreach (e in entities()) {
                if (e.pos.x >= 2.0) e.destroy()
            }
            return true
        }
    }
    local spawnSys = SpawnSys()
    if (!spawnSys.update(0.0)) return false
    if (spawnSys.snapshot != 1) return false
    if (eve.view(Mover).len() != 1) return false
    foreach (e in eve.view(Mover)) {
        if (e != c) return false
        e.destroy()
    }
    return eve.view(Mover).len() == 0
}

function testSystemQueryArrayAndSubclass() {
    class Hp extends eve.Component { v = 1 }
    class Actor extends eve.Entity { hp = Hp }
    class Enemy extends Actor {}
    class Prop extends eve.Entity { hp = Hp }

    class TickSys extends eve.System {
        constructor() { base.constructor([Actor, Prop]) }
        function update(dt) {
            foreach (e in entities()) e.hp.v += 1
        }
    }

    local actor = Actor.create()
    local enemy = Enemy.create()
    local prop = Prop.create()
    local sys = TickSys()
    sys.update(0.0)
    if (actor.hp.v != 2 || enemy.hp.v != 2 || prop.hp.v != 2) return false

    sys.setQuery(Enemy)
    sys.update(0.0)
    if (actor.hp.v != 2 || enemy.hp.v != 3 || prop.hp.v != 2) return false

    local emptySys = eve.System()
    if (emptySys.entities().len() != 0) return false

    actor.destroy()
    enemy.destroy()
    prop.destroy()
    return true
}

function testTypeMarkersAndEntityContainer() {
    class Status extends eve.Component {
        alive = eve.Boolean
        name = eve.String
        score = eve.Number
    }
    class Thing extends eve.EntityContainer {
        status = Status
    }
    local t = Thing.create()
    if (t.status.alive != false) return false
    if (t.status.name != "") return false
    if (t.status.score != 0.0) return false
    t.status.alive = true
    t.status.name = "orb"
    t.status.score = 3.5
    if (!t.status.alive || t.status.name != "orb" || t.status.score != 3.5) return false
    return eve.EntityContainer == eve.Entity
}

function testStaticComponentsTable() {
    class Pos extends eve.Component {
        x = 1.0
    }
    class Node extends eve.Entity {
        static components = { pos = Pos }
        pos = null
    }
    local n = Node.create()
    if (n.pos == null) return false
    if (n.pos.x != 1.0) return false
    return true
}

function testShaderSystemClassExists() {
    // Feature-detect by touching the name, not with `"ShaderSystem" in eve`: the `in`
    // operator is _OP_EXISTS, which SQVM::Get evaluates with GET_FLAG_RAW, so it never
    // runs the engine's lazy _get binder and is false for every name that has not been
    // resolved yet (eve.moduleList is only visible to `in` because Run.cpp sets it
    // eagerly). Accessing a name that resolves to nothing raises, hence the try/catch.
    local shaderSystem = null
    try { shaderSystem = eve.ShaderSystem } catch (e) { return false }
    if (shaderSystem == null) return false
    // Construction without GPU is allowed; update() no-ops until setGpgpu.
    class Pos extends eve.Component { x = 0.0; y = 0.0 }
    class Vel extends eve.Component { x = 1.0; y = 0.0 }
    class Mover extends eve.Entity { pos = Pos; vel = Vel }
    local sys = eve.ShaderSystem(Mover)
    sys.bindFields(0, "pos", ["x", "y"])
    sys.bindFields(1, "vel", ["x", "y"])
    sys.update(0.016)
    return true
}

function testShaderSystemGpuResidentTransfers() {
    class Pos extends eve.Component { x = 0.0 }
    class Node extends eve.Entity { pos = Pos }
    class FakeBackend {
        dispatches = 0
        records = 0
        function ensureBuffer(binding, count) { return binding }
        function getBuffer(binding) { return binding }
        function dispatch(count, dt) { dispatches += 1 }
        function recordDispatch(sequence, count, dt) { records += 1 }
    }

    local uploads = 0
    local downloads = 0
    local uploadRange = null
    local downloadRange = null
    eve.packEcsFloats <- function(ents, slot, fields, buf) { uploads += 1 }
    eve.unpackEcsFloats <- function(ents, slot, fields, buf, count) { downloads += 1 }
    eve.packEcsFloatsRange <- function(ents, slot, fields, buf, first, count) {
        uploadRange = [first, count]
    }
    eve.unpackEcsFloatsRange <- function(ents, slot, fields, buf, first, count) {
        downloadRange = [first, count]
    }

    local node = Node.create()
    local second = Node.create()
    local third = Node.create()
    local sys = eve.ShaderSystem(Node)
    sys._gpu = true
    sys._backend = FakeBackend()
    sys.bindFields(0, "pos", ["x"], false, false)

    sys.update(0.016)
    sys.update(0.016)
    if (uploads != 1 || downloads != 0 || sys._backend.dispatches != 2) return false

    sys.requestUpload(0).requestReadback(0)
    sys.update(0.016)
    if (uploads != 2 || downloads != 1) return false

    sys.requestUploadRange(0, 1, 1).requestUploadRange(0, 2, 1)
    sys.requestReadbackRange(0, 1, 2)
    sys.update(0.016)
    if (uploadRange == null || uploadRange[0] != 1 || uploadRange[1] != 2) return false
    if (downloadRange == null || downloadRange[0] != 1 || downloadRange[1] != 2) return false
    if (uploads != 2 || downloads != 1) return false

    local batch = sys.record(true, 0.016)
    sys.completeRecorded(batch)
    if (sys._backend.records != 1) return false

    // Typed write-only schemas allocate resident output without packing stale CPU data.
    local outputSys = eve.ShaderSystem(Node)
    outputSys._gpu = true
    outputSys._backend = FakeBackend()
    outputSys.bindSchema(0, { slot = "pos", fields = ["x"], scalar = "f32",
                              access = "write" })
    outputSys.update(0.016)
    if (uploads != 2) return false
    local schema = outputSys.getBindingSchema(0)
    if (schema == null || schema.strideFloats != 1 || schema.access != "write") return false
    if (outputSys.shaderDeclarations("glsl") !=
        "layout(set = 0, binding = 0) writeonly buffer EcsBinding0 { float data[]; } ecs0;\n")
        return false
    if (outputSys.shaderDeclarations("wgsl") !=
        "@group(0) @binding(0) var<storage, read_write> ecs0 : array<f32>;\n")
        return false

    local rejected = false
    try { outputSys.bindSchema(1, { slot = "pos", fields = ["x"], scalar = "i32" }) }
    catch (e) { rejected = true }
    if (!rejected) return false

    // A structural revision invalidates resident packing even at the same capacity.
    local fourth = Node.create()
    sys.update(0.016)
    if (uploads != 3 || downloads != 1) return false
    node.destroy()
    second.destroy()
    third.destroy()
    fourth.destroy()
    return true
}

function testViewCacheStableAndInvalidated() {
    class Pos extends eve.Component { x = 0.0 }
    class Node extends eve.Entity { pos = Pos }
    class Sub extends Node {}

    local a = Node.create()
    local b = Sub.create()

    // No changes between calls → same *content*. The returned array is fresh every
    // call on purpose: the cache stays internal, so a caller that sorts, pops or
    // appends to its view cannot change what the next caller sees. (The view cache
    // still saves the expensive part — walking every registered type.)
    local v1 = eve.view(Node)
    if (v1.len() != 2) return false
    local v2 = eve.view(Node)
    if (v1 == v2) return false
    if (v2.len() != 2) return false

    // Mutating a view is local to the caller and must not leak into the cache.
    v1.pop()
    if (v1.len() != 1) return false
    if (eve.view(Node).len() != 2) return false

    local sv1 = eve.view(Sub)
    if (sv1.len() != 1) return false

    // create() invalidates the cached view (and subclass views via the chain)
    local c = Node.create()
    if (eve.view(Node).len() != 3) return false
    if (eve.view(Sub).len() != 1) return false

    // destroy() invalidates too; dead entities leave the view immediately
    a.destroy()
    b.destroy()
    c.destroy()
    if (eve.view(Node).len() != 0) return false
    return true
}

function testViewCacheBaseClassIncludesSubclasses() {
    class Pos extends eve.Component { x = 0.0 }
    class Node extends eve.Entity { pos = Pos }
    class Sub extends Node {}

    local n = Node.create()
    local s = Sub.create()
    if (eve.view(Node).len() != 2) return false
    if (eve.view(eve.Entity).len() < 2) return false
    n.destroy()
    s.destroy()
    return true
}

function testComponentDefaultsAndSlotsCached() {
    class C extends eve.Component { v = eve.Number; s = eve.String }
    class E extends eve.Entity { c = C }

    local a = E.create()
    if (a.c.v != 0.0 || a.c.s != "") return false
    a.c.v = 5.0

    // Second instance gets fresh resolved defaults, not the mutated values.
    local b = E.create()
    if (b.c.v != 0.0 || b.c.s != "") return false
    if (a.c.v != 5.0) return false
    if (a.getComponent(C) != a.c) return false
    if (!a.hasComponent(C)) return false

    a.destroy()
    b.destroy()
    return true
}

function testDestroyClearsComponentState() {
    class Pos extends eve.Component { x = 0.0 }
    class Node extends eve.Entity { pos = Pos }

    local n = Node.create()
    local pos = n.getComponent(Pos)
    if (pos == null) return false
    if (!n.hasComponent(Pos)) return false
    if (pos.x != 0.0) return false
    pos.x = 5.0

    n.destroy()

    // Destroyed means gone: the component is unreachable and no longer silently
    // writable-but-ignored through either path.
    if (n.isAlive()) return false
    if (n.getComponent(Pos) != null) return false
    if (n.hasComponent(Pos)) return false
    // 字段槽位也必须清：只清 `_comps` 会让 `n.pos` 继续指向一个活着的组件实例，
    // 于是 `n.pos.x = 1` 静默改一个任何 view 都看不到的孤儿（§9.38 实测到的漏洞）。
    if (n.pos != null) return false
    if (eve.view(Node).len() != 0) return false

    // Destroying twice stays a no-op.
    n.destroy()
    if (n.getComponent(Pos) != null) return false
    if (n.pos != null) return false
    return true
}

function testFieldSlotIsTheSameComponentInstance() {
    class Pos extends eve.Component { x = 0.0; y = 0.0 }
    class Node extends eve.Entity { pos = Pos }

    local a = Node.create()
    local b = Node.create()

    // 已声明槽位的字段与槽位表指向**同一个**组件实例（这曾是 §9.14.2 记错的地方：
    // 它断言 `e.pos` 读到的是组件类对象、写的是类级字段）。
    if (a.pos != a.getComponent(Pos)) return false
    if (typeof a.pos != "instance") return false

    // 逐实体隔离：如果写的是类级共享状态，新建实体会立刻看到 10.0。
    a.pos.x = 10.0
    if (a.getComponent(Pos).x != 10.0) return false
    if (b.pos.x != 0.0) return false
    if (b.getComponent(Pos).x != 0.0) return false

    // 反向：经槽位表写，字段路径要读得到。
    a.getComponent(Pos).y = 4.0
    if (a.pos.y != 4.0) return false

    // 清掉字段（`=` 对已声明成员是普通赋值，不抛）：槽位表仍持有实例，所以显式把两边
    // 都处理掉才是销毁该做的事——这里只钉住"字段可写"这条语义。
    local key = "pos"
    a[key] = null
    if (a.pos != null) return false
    return true
}

function testRegistryOnlySlotHasNoField() {
    class Pos extends eve.Component { x = 1.0 }
    class Node extends eve.Entity {
        static components = { pos = Pos }
    }

    local n = Node.create()
    // 只登记在 components 表、没有同名字段的槽位只能经 getComponent()/hasComponent() 访问。
    if (n.getComponent(Pos) == null) return false
    if (!n.hasComponent(Pos)) return false
    if (n.getComponent(Pos).x != 1.0) return false

    // Squirrel 拒绝对 class 实例新建槽位：未声明的键读不到，
    // `=`（_OP_SET）与 `<-`（new-slot 操作码）报的是**两句不同**的话。这三条语义是
    // create()/destroy() 分流实现的依据，所以钉在这里。
    local readFailed = false
    try { local ignored = n.pos } catch (ex) { readFailed = true }
    if (!readFailed) return false

    local setMessage = ""
    try { n.pos = Pos() } catch (ex) { setMessage = "" + ex }
    if (setMessage.find("does not exist") == null) return false

    local newSlotMessage = ""
    try { n.pos <- Pos() } catch (ex) { newSlotMessage = "" + ex }
    if (newSlotMessage.find("new slot operator") == null) return false
    return true
}

function testGpuDrivenBindingSurface() {
    local gpu
    try { gpu = eve.Gpgpu() } catch (e) { return false }
    local required = ["setGpuDrivenEnabled", "isGpuDrivenEnabled",
                      "gpuDrivenMeshRecord", "gpuDrivenMaterialRecord",
                      "gpuDrivenMaterialUsable", "getGpuDrivenInstanceStride",
                      "getGpuResidentOffsetAlignment", "writeGpuDrivenInstance",
                      "submitResidentInstances"]
    foreach (name in required) if (!(name in gpu)) return false
    // 160, not the old 80: GpuInstance grew reflection-probe fields, and
    // GpuDrivenTypes.h pins the new size with static_assert(sizeof(GpuInstance) == 160).
    return gpu.getGpuDrivenInstanceStride() == 160 &&
           gpu.getGpuResidentOffsetAlignment() == 256
}

function testGpgpuSurfaceWithoutAGraphicsDevice() {
    // 上面那条钉的是"模块面齐全"；这条钉的是**设备缺席**时的契约：本 fixture 有 gpgpu 模块类，
    // 但从未调用 `openGfxWindow`，所以 `isAvailable()` 必须为 false，而
    // `eve.ShaderSystem` 仍允许构造、`bindFields`/`update()` 是 no-op 且**不抛**
    // （`EcsGpu.h` 与 `docs/dev/2026-08-30-ecs-gpu-efficiency.md` 的声明）。
    // 实测（探针，跑完已撤）：`gpuNonnull=true isAvailable=false update=ok`。
    // "设备存在"半边由 `GpuDriven.squirrelResidentInstanceSubmit` 在真机上覆盖。
    local gpu = eve.Gpgpu()
    if (gpu == null) return false
    if (gpu.isAvailable()) return false

    class Pos extends eve.Component { x = 0.0; y = 0.0 }
    class Vel extends eve.Component { x = 1.0; y = 0.0 }
    class Mover extends eve.Entity { pos = Pos; vel = Vel }
    local sys = eve.ShaderSystem(Mover)
    sys.bindFields(0, "pos", ["x", "y"])
    sys.bindFields(1, "vel", ["x", "y"])
    sys.update(0.016)
    return true
}

// 视图顺序 = 实体 id 升序（= 创建顺序）。
//
// `_ecsUnregisterInstance` 用 swap-remove 保持 O(1) 销毁，数组本身在删除后会失序（实测
// [1,2,3,4] 删掉 2 得到 [1,4,3]），而 GPU System 按视图顺序打包成 buffer 的行、回读时又按
// 同一顺序写回，`requestUploadRange/ReadbackRange(first, count)` 直接按行号寻址——顺序一变，
// 回读的值会落到另一个实体上。所以这里把顺序钉死。
function testViewOrderIsEntityIdOrder() {
    class Node extends eve.Entity { }
    local a = Node.create()
    local b = Node.create()
    local c = Node.create()
    local d = Node.create()
    local removedId = b.getId()

    local seen = []
    foreach (e in eve.view(Node)) seen.push(e.getId())
    if (seen.len() != 4) return false
    for (local i = 1; i < seen.len(); ++i) if (seen[i] <= seen[i - 1]) return false

    // 删掉中间一个：幸存者仍按 id 升序（改前这里是 1,4,3）
    b.destroy()
    seen = []
    foreach (e in eve.view(Node)) seen.push(e.getId())
    if (seen.len() != 3) return false
    for (local i = 1; i < seen.len(); ++i) if (seen[i] <= seen[i - 1]) return false
    if (seen.find(a.getId()) == null || seen.find(c.getId()) == null || seen.find(d.getId()) == null) return false

    // 新建实体排在最后；删除最新的一个不影响幸存者的相对顺序
    local e5 = Node.create()
    seen = []
    foreach (e in eve.view(Node)) seen.push(e.getId())
    if (seen.len() != 4 || seen[3] != e5.getId()) return false
    e5.destroy()
    seen = []
    foreach (e in eve.view(Node)) seen.push(e.getId())
    if (seen.len() != 3 || seen[2] != d.getId()) return false

    // id 不复用：这是"按 id 排序"能当稳定键的前提
    for (local i = 0; i < 8; ++i) if (Node.create().getId() == removedId) return false

    // 同一类两次取视图顺序一致
    local first = []
    local second = []
    foreach (e in eve.view(Node)) first.push(e.getId())
    foreach (e in eve.view(Node)) second.push(e.getId())
    if (first.len() != second.len()) return false
    for (local i = 0; i < first.len(); ++i) if (first[i] != second[i]) return false
    return true
}

// 跨子类的视图同样按创建顺序交错：此前按 `_ecsTypes` 的散列顺序分组，同类会聚在一起
// （例如 Left,Left,Right,Right 而不是创建顺序），于是 id 序列不是升序。
function testViewOrderSpansSubclassesByCreation() {
    class Base extends eve.Entity { }
    class Left extends Base { }
    class Right extends Base { }

    Left.create()
    Right.create()
    Left.create()
    Right.create()

    local seen = []
    foreach (e in eve.view(Base)) seen.push(e.getId())
    if (seen.len() != 4) return false
    for (local i = 1; i < seen.len(); ++i) if (seen[i] <= seen[i - 1]) return false
    return true
}
)SQ";

UnitSciptTest(ScriptEcsTest, kScriptEcsContent);

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.ready") { REQUIRE(callScript("testEcsReady")); }

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.defineClassAndComponent") {
    REQUIRE(callScript("testDefineClassAndComponent"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.viewIncludesSubclass") { REQUIRE(callScript("testViewIncludesSubclass")); }

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.systemUpdate") { REQUIRE(callScript("testSystemUpdate")); }

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.systemDynamicSpawnDespawn") {
    REQUIRE(callScript("testSystemDynamicSpawnDespawn"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.systemQueryArrayAndSubclass") {
    REQUIRE(callScript("testSystemQueryArrayAndSubclass"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.typeMarkersAndEntityContainer") {
    REQUIRE(callScript("testTypeMarkersAndEntityContainer"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.staticComponentsTable") {
    REQUIRE(callScript("testStaticComponentsTable"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.shaderSystemClassExists") {
    REQUIRE(callScript("testShaderSystemClassExists"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.shaderSystemGpuResidentTransfers") {
    REQUIRE(callScript("testShaderSystemGpuResidentTransfers"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.viewCacheStableAndInvalidated") {
    REQUIRE(callScript("testViewCacheStableAndInvalidated"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.viewCacheBaseClassIncludesSubclasses") {
    REQUIRE(callScript("testViewCacheBaseClassIncludesSubclasses"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.componentDefaultsAndSlotsCached") {
    REQUIRE(callScript("testComponentDefaultsAndSlotsCached"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.destroyClearsComponentState") {
    REQUIRE(callScript("testDestroyClearsComponentState"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.fieldSlotIsTheSameComponentInstance") {
    REQUIRE(callScript("testFieldSlotIsTheSameComponentInstance"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.registryOnlySlotHasNoField") {
    REQUIRE(callScript("testRegistryOnlySlotHasNoField"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.gpuDrivenBindingSurface") {
    REQUIRE(callScript("testGpuDrivenBindingSurface"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.gpgpuSurfaceWithoutAGraphicsDevice") {
    REQUIRE(callScript("testGpgpuSurfaceWithoutAGraphicsDevice"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.viewOrderIsEntityIdOrder") {
    REQUIRE(callScript("testViewOrderIsEntityIdOrder"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.viewOrderSpansSubclassesByCreation") {
    REQUIRE(callScript("testViewOrderSpansSubclassesByCreation"));
}

static const char* kComponentRegistryScript = R"SQ(
function registerProbeComponent() {
    class Probe extends eve.Component { v = 0 }
    return eve.component("Probe", 1, Probe).ok
}

// 同一个 VM 内用同名同版本再注册一次：走"换一代"分支，此时旧引用属于**当前** VM，
// 必须照常释放——跨 VM 的修复不能把这条正常路径也一起关掉。
function registerProbeComponentAgain() {
    class Probe extends eve.Component { v = 1 }
    return eve.component("Probe", 1, Probe).ok
}
)SQ";

// `callScript` 是 protected，而本用例需要在同一个 TEST_CASE 里先后构造两个 VM。
class ComponentRegistryTwiceTest : public ScriptTest {
public:
    ComponentRegistryTwiceTest() : ScriptTest(kComponentRegistryScript) {}
    bool registerProbeComponent() { return callScript("registerProbeComponent"); }
    bool registerProbeComponentAgain() { return callScript("registerProbeComponentAgain"); }
};

TEST_CASE("ScriptECS.componentRegistrationCrossesVmsSafely") {
    {
        ComponentRegistryTwiceTest first;
        REQUIRE(first.registerProbeComponent());
        REQUIRE(first.registerProbeComponentAgain());  // 同 VM 替换：照常释放
    }
    // 第一个 VM 已在此处销毁。用同一个名字在第二个 VM 里再注册一次：
    // 注册表是进程级静态、名字相同就会走"替换旧条目"的分支，若那条分支去释放
    // 属于已销毁 VM 的引用，就是 use-after-free（见方案文档 §9.49）。
    ComponentRegistryTwiceTest second;
    REQUIRE(second.registerProbeComponent());
    REQUIRE(second.registerProbeComponentAgain());
}

// ---------------------------------------------------------------------------
// 组件注册表：名字 + 版本 + 代际（热重载迁移与引擎侧存储的共同前置）
// ---------------------------------------------------------------------------

static const char* kComponentIdentityScript = R"SQ(
class Position extends eve.Component { x = 0.0 }
class PositionV2 extends eve.Component { x = 0.0; y = 0.0 }

function registerOne() {
    return eve.component("Position", 1, Position).ok
}

function registerSecondVersion() {
    return eve.component("Position", 2, PositionV2).ok
}

// 同名同版本再登记：热重载换了一代。
function registerSameVersionAgain() {
    class Position extends eve.Component { x = 1.0 }
    return eve.component("Position", 1, Position).ok
}

function registerWithBadArguments() {
    local emptyName = eve.component("", 1, Position)
    local zeroVersion = eve.component("Zero", 0, Position)
    local notAClass = eve.component("NotAClass", 1, 42)
    return emptyName.ok || zeroVersion.ok || notAClass.ok
}

// 声明顺序 ≠ 成员表遍历顺序（§10.17 实测是哈希顺序），所以这条用例用一组刻意乱序的字段名。
class Inventory extends eve.Component {
    zeta = 1.0
    alpha = 2.0
    mid = 3.0
    beta = 4.0
    omega = 5.0
}

// 继承：基类字段在前。
class BaseStats extends eve.Component { hp = 0.0 }
class EnemyStats extends BaseStats { speed = 0.0 }

function registerSchemaCases() {
    local ordered = eve.component("Inventory", 1, Inventory).ok
    local derived = eve.component("EnemyStats", 1, EnemyStats).ok
    return ordered && derived
}

// 同名同版本换了一代，且字段集变了：墓碑要留住旧代的字段列表，迁移才知道搬什么。
function registerReshapedGeneration() {
    class Reshaped extends eve.Component { x = 0.0 }
    local first = eve.component("Reshaped", 1, Reshaped).ok
    class Reshaped2 extends eve.Component { x = 0.0; y = 0.0 }
    local second = eve.component("Reshaped", 1, Reshaped2).ok
    return first && second
}
)SQ";

UnitSciptTest(ComponentIdentityTest, kComponentIdentityScript);

// `callScript` 是 protected，而"注册表按 VM 隔离"这条要在同一个 TEST_CASE 里先后建两个 VM。
class ComponentIdentityTwiceTest : public ScriptTest {
public:
    ComponentIdentityTwiceTest() : ScriptTest(kComponentIdentityScript) {}
    bool registerOne() { return callScript("registerOne"); }
};

TEST_CASE_FIXTURE(ComponentIdentityTest, "ScriptECS.componentRegistrationIsReadableFromCpp") {
    REQUIRE(callScript("registerOne"));

    const eve::ScriptComponentRecord* found = eve::findScriptComponent("Position", eve::SchemaVersion(1));
    REQUIRE(found != nullptr);
    REQUIRE_EQ(found->generation.value(), std::uint64_t(1));
    REQUIRE(!found->retired);

    const auto all = eve::scriptComponentRecords();
    REQUIRE_EQ(all.size(), std::size_t(1));
    REQUIRE_EQ(all[0].name, std::string("Position"));
    REQUIRE_EQ(all[0].version.value(), std::uint64_t(1));
    REQUIRE(!all[0].retired);
}

TEST_CASE_FIXTURE(ComponentIdentityTest, "ScriptECS.componentVersionSeparatesIdentities") {
    REQUIRE(callScript("registerOne"));
    REQUIRE(callScript("registerSecondVersion"));

    // 同一名字的两个版本是两条独立身份，各自可查。
    const auto* v1 = eve::findScriptComponent("Position", eve::SchemaVersion(1));
    const auto* v2 = eve::findScriptComponent("Position", eve::SchemaVersion(2));
    REQUIRE(v1 != nullptr);
    REQUIRE(v2 != nullptr);
    REQUIRE_EQ(v1->generation.value(), std::uint64_t(1));
    REQUIRE_EQ(v2->generation.value(), std::uint64_t(1));
    REQUIRE(eve::findScriptComponent("Position", eve::SchemaVersion(3)) == nullptr);
    REQUIRE_EQ(eve::scriptComponentRecords().size(), std::size_t(2));
}

TEST_CASE_FIXTURE(ComponentIdentityTest, "ScriptECS.componentReRegistrationAdvancesTheGeneration") {
    REQUIRE(callScript("registerOne"));
    REQUIRE(callScript("registerSameVersionAgain"));

    // 当前那条是新代，旧代以墓碑形式留痕——热重载迁移要靠这条链判断"从哪一代搬到哪一代"。
    const auto* current = eve::findScriptComponent("Position", eve::SchemaVersion(1));
    REQUIRE(current != nullptr);
    REQUIRE_EQ(current->generation.value(), std::uint64_t(2));
    REQUIRE(!current->retired);

    const auto all = eve::scriptComponentRecords();
    REQUIRE_EQ(all.size(), std::size_t(2));
    REQUIRE_EQ(all[0].generation.value(), std::uint64_t(1));
    REQUIRE(all[0].retired);
    REQUIRE_EQ(all[1].generation.value(), std::uint64_t(2));
    REQUIRE(!all[1].retired);
}

TEST_CASE_FIXTURE(ComponentIdentityTest, "ScriptECS.componentRegistrationReportsBadArguments") {
    // 参数非法时必须让脚本看得见（返回结果表的 ok=false），而不是静默什么都没发生。
    REQUIRE(!callScript("registerWithBadArguments"));
    REQUIRE(eve::findScriptComponent("", eve::SchemaVersion(1)) == nullptr);
    REQUIRE(eve::findScriptComponent("Zero", eve::SchemaVersion(0)) == nullptr);
    REQUIRE(eve::findScriptComponent("NotAClass", eve::SchemaVersion(1)) == nullptr);
    REQUIRE(eve::scriptComponentRecords().empty());
}

TEST_CASE_FIXTURE(ComponentIdentityTest, "ScriptECS.componentSchemaKeepsDeclarationOrder") {
    REQUIRE(callScript("registerSchemaCases"));

    // 声明是 zeta, alpha, mid, beta, omega；成员表遍历是哈希顺序（§10.17），两者不同。
    const auto* inventory = eve::findScriptComponent("Inventory", eve::SchemaVersion(1));
    REQUIRE(inventory != nullptr);
    const std::vector<std::string> expected{"zeta", "alpha", "mid", "beta", "omega"};
    REQUIRE_EQ(inventory->fields, expected);

    // 基类字段在前，自身字段在后。
    const auto* enemy = eve::findScriptComponent("EnemyStats", eve::SchemaVersion(1));
    REQUIRE(enemy != nullptr);
    const std::vector<std::string> expectedDerived{"hp", "speed"};
    REQUIRE_EQ(enemy->fields, expectedDerived);
}

TEST_CASE_FIXTURE(ComponentIdentityTest, "ScriptECS.componentRetiredGenerationKeepsItsOwnFields") {
    REQUIRE(callScript("registerReshapedGeneration"));

    // 当前代是 {x, y}，墓碑代是 {x}：迁移要按这两组字段对照着搬。
    const auto* current = eve::findScriptComponent("Reshaped", eve::SchemaVersion(1));
    REQUIRE(current != nullptr);
    const std::vector<std::string> expectedCurrent{"x", "y"};
    REQUIRE_EQ(current->fields, expectedCurrent);

    const auto all = eve::scriptComponentRecords();
    REQUIRE_EQ(all.size(), std::size_t(2));
    REQUIRE(all[0].retired);
    const std::vector<std::string> expectedRetired{"x"};
    REQUIRE_EQ(all[0].fields, expectedRetired);
}

TEST_CASE("ScriptECS.componentRegistryIsPerVm") {
    // 代际数的是"同一个 VM 内重载了几次"，所以新 VM 必须从空开始。清理时机沿用既有约定
    // （与 cppEntityViews 同因）：**为新 VM 准备时**清空，而不是上一个 VM 析构时——所以
    // 这里不断言"第一个 VM 一销毁就为空"，而是断言第二个 VM 注册后**没有历史残留**。
    {
        ComponentIdentityTwiceTest first;
        REQUIRE(first.registerOne());
        REQUIRE(eve::findScriptComponent("Position", eve::SchemaVersion(1)) != nullptr);
    }

    ComponentIdentityTwiceTest second;
    REQUIRE(second.registerOne());
    const eve::ScriptComponentRecord* found = eve::findScriptComponent("Position", eve::SchemaVersion(1));
    REQUIRE(found != nullptr);
    // 不是 2：新 VM 没有上一代的记录；也不是两条：旧 VM 的登记没有残留。
    REQUIRE_EQ(found->generation.value(), std::uint64_t(1));
    REQUIRE_EQ(eve::scriptComponentRecords().size(), std::size_t(1));
}
