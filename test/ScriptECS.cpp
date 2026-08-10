#include "ScriptTest.h"

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
    if (!("ShaderSystem" in eve)) return false
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
)SQ";

UnitSciptTest(ScriptEcsTest, kScriptEcsContent);

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.ready") {
    CHECK(vm.callFunc(vm.findFunc("testEcsReady"), vm).toBool());
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.defineClassAndComponent") {
    CHECK(vm.callFunc(vm.findFunc("testDefineClassAndComponent"), vm).toBool());
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.viewIncludesSubclass") {
    CHECK(vm.callFunc(vm.findFunc("testViewIncludesSubclass"), vm).toBool());
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.systemUpdate") {
    CHECK(vm.callFunc(vm.findFunc("testSystemUpdate"), vm).toBool());
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.typeMarkersAndEntityContainer") {
    CHECK(vm.callFunc(vm.findFunc("testTypeMarkersAndEntityContainer"), vm).toBool());
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.staticComponentsTable") {
    CHECK(vm.callFunc(vm.findFunc("testStaticComponentsTable"), vm).toBool());
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.shaderSystemClassExists") {
    CHECK(vm.callFunc(vm.findFunc("testShaderSystemClassExists"), vm).toBool());
}
