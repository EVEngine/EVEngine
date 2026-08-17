// 场景节点 ↔ 脚本 ECS 打通示例。
// 演示：节点挂 eve.SceneEntity 行为（含数据组件槽）、eve.view 批量查询、
// scene.update(dt) 驱动 update + 变换、reconcile 后绑定保留。
// 运行：make run/win32-debug GAME=examples/scene-ecs

class MoveComp extends eve.Component {
    speed = 60.0
}

class Bounce extends eve.SceneEntity {
    move = MoveComp
    dir = 1.0

    function onAttach() {
        // 挂载时给节点一个初始位置
        local p = node().getPosition()
        if (p[1] <= 0.0) node().setPosition(p[0], 60.0, p[2])
    }

    function update(dt) {
        local p = node().getPosition()
        local y = p[1] + move.speed * dir * dt
        if (y > 360.0) dir = -1.0
        if (y < 60.0) dir = 1.0
        node().setPosition(p[0], y, p[2])
    }
}

class Spinner extends eve.SceneEntity {
    speed = 90.0
    function update(dt) {
        local r = node().getRotation()
        node().setRotation(r[0], r[1], r[2] + speed * dt)
    }
}

function buildBattlefield() {
    scene.beginBuild()
    scene.beginNode("root")
    scene.addNode("player")
    scene.setBuildPosition(120.0, 60.0, 0.0)
    scene.addNode("enemy")
    scene.setBuildPosition(360.0, 120.0, 0.0)
    scene.addNode("prop")
    scene.setBuildPosition(240.0, 210.0, 0.0)
    scene.end()
    scene.mountBuildAs("battle")

    scene.attachEntity("player", Bounce)
    scene.attachEntity("enemy", Bounce)
    scene.attachEntity("prop", Spinner)
}

playerRef <- null
enemyRef <- null
buildLabel <- null

eve_init = function() {
    gfx.setBackgroundColor(0.08, 0.10, 0.16, 1.0)
    buildBattlefield()

    playerRef = scene.getNodeRef("player")
    enemyRef = scene.getNodeRef("enemy")

    print("scene-ecs: attached=" + eve.view(Bounce).len() + " bouncers, " +
          eve.view(Spinner).len() + " spinner")
}

eve_update = function(dt) {
    scene.update(dt)   // 先同步 transforms，再驱动所有挂载实体的 update(dt)
}

eve_render = function() {
    gfx.clear()

    local p = playerRef.getPosition()
    local e = enemyRef.getPosition()
    local pr = scene.getNodeRef("prop")
    local s = pr.getPosition()

    gfx.drawSolidRect(p[0] - 14.0, p[1] - 14.0, 28.0, 28.0, 0.35, 0.85, 0.95, 1.0)
    gfx.drawSolidRect(e[0] - 14.0, e[1] - 14.0, 28.0, 28.0, 0.95, 0.45, 0.35, 1.0)
    gfx.drawSolidRect(s[0] - 10.0, s[1] - 10.0, 20.0, 20.0, 0.85, 0.75, 0.30, 1.0)
}

eve_quit = function() {}
