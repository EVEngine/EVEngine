function basic() {
    if (eve.Crowd == null) {
        print("crowd module not built\n");
        return false;
    }
    local crowd = eve.Crowd();
    if (crowd.getName() != "Crowd") {
        print("Crowd name mismatch\n");
        return false;
    }

    // ---- 流场 ----
    crowd.resizeField(16, 16, 16.0, 0.0, 0.0);
    crowd.setBlocked(8, 3, true);
    crowd.setCellCost(8, 4, 5.0);
    crowd.buildFlowField(15, 15);
    if (!crowd.isFieldBuilt()) {
        print("field not built\n");
        return false;
    }
    if (!crowd.isReachable(0, 0)) {
        print("start unreachable\n");
        return false;
    }
    local f = crowd.flowAtWorld(10.0, 10.0);
    if (f.x * f.x + f.y * f.y < 0.5) {
        print("flow not unit length\n");
        return false;
    }
    if (crowd.costAtWorld(10.0, 10.0) <= 0.0) {
        print("bad cost\n");
        return false;
    }

    // ---- 单位 ----
    local id = crowd.addAgent(16.0, 16.0, 0.0, 5.0);
    if (id < 0) {
        print("addAgent failed\n");
        return false;
    }
    crowd.setAgentAction(id, "seek");
    crowd.setAgentTarget(id, 200.0, 200.0);
    crowd.setAgentSpeed(id, 100.0);
    crowd.setAgentTurnRate(id, 3.0);
    crowd.setAgentData(id, 42);
    if (crowd.getAgentAction(id) != "seek") {
        print("action mismatch\n");
        return false;
    }
    local s = crowd.getAgentState(id);
    if (s.action != 2 || s.data != 42 || s.x != 16.0 || s.y != 16.0) {
        print("state mismatch\n");
        return false;
    }

    crowd.step(0.016);
    if (crowd.getAgentCount() != 1) {
        print("count mismatch\n");
        return false;
    }

    // ---- 批量读取 ----
    local xs = array(crowd.getAgentCount());
    local ys = array(crowd.getAgentCount());
    local hs = array(crowd.getAgentCount());
    crowd.getPositions(xs, ys);
    crowd.getHeadings(hs);
    if (xs.len() != 1 || ys.len() != 1 || hs.len() != 1 || xs[0] == null) {
        print("bulk arrays mismatch\n");
        return false;
    }

    // ---- Boids 参数 + 推进 ----
    crowd.setAgentAction(id, "boids");
    crowd.setSeparationRadius(24.0);
    crowd.setPerceptionRadius(48.0);
    crowd.setSeparationWeight(1.0);
    crowd.setAlignmentWeight(0.5);
    crowd.setCohesionWeight(0.3);
    crowd.setWanderWeight(0.1);
    for (local i = 0; i < 30; i += 1) crowd.step(0.016);

    if (crowd.removeAgent(id) != true) {
        print("remove failed\n");
        return false;
    }
    if (crowd.getAgentCount() != 0) {
        print("count after remove mismatch\n");
        return false;
    }
    return true;
}

function invalidIds() {
    if (eve.Crowd == null) return true;
    local crowd = eve.Crowd();
    crowd.clearAgents();
    if (crowd.getAgentState(999).action != -1) return false;
    if (crowd.removeAgent(999) != false) return false;
    if (crowd.setAgentSpeed(999, 10.0) != false) return false;
    if (crowd.setAgentAction(999, "seek") != false) return false;
    if (crowd.getAgentAction(999) != "") return false;
    return true;
}
