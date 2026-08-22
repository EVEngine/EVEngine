#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ScriptTest.h"

// Runtime binding self-check (companion to scripts/check_bindings.py): builds
// a bare script VM with every registered module exposed, then verifies curated
// "hot" bindings on light modules actually exist. Constructing a module class
// is a real smoke: it exercises ModuleManager::create + expose. (eve.reflect is
// covered by test/ReflectScript.cpp, which uses the full Runtime.)
TEST_CASE("binding.selfcheck.scriptSurface") {
    ScriptTest t(R"(
        local missing = [];

        // Light modules: constructor must succeed and hot bindings must exist.
        local hot = {
            Math = ["newVec2", "newMat4", "setRandomSeed"],
            DataModule = ["newByteData", "decodeJson", "encodeJson"],
            Event = ["pump", "poll", "pushData"],
            Timer = ["getTime", "step"],
            Rx = ["newSubject"],
            Spatial = ["newQuadTree", "newOctree"],
            IK = ["newSkeleton2D", "newSolver2D"],
            RPG = ["newActor"],
            Inventory = ["newBag"],
        };
        foreach (cls, methods in hot) {
            if (!(cls in eve)) { missing.push(cls + ":ctor-missing"); continue; }
            local inst = null;
            try {
                inst = eve[cls]();
            } catch (e) {
                missing.push(cls + ":ctor:" + e);
                continue;
            }
            if (inst == null) { missing.push(cls + ":ctor-null"); continue; }
            foreach (fn in methods) if (!(fn in inst)) missing.push(cls + "." + fn);
        }

        local msg = "";
        for (local i = 0; i < missing.len(); i++) {
            if (i > 0) msg += ",";
            msg += missing[i];
        }
        if (missing.len() > 0) throw "binding self-check failed: " + msg;
    )");
}
