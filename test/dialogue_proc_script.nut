function basicPools() {
    if (eve.Dialogue == null) return false;
    local dlg = eve.Dialogue();
    dlg.clearPools();
    dlg.clearVars("all");
    dlg.setRandomSeed(7);

    dlg.setVar("mood", "happy", "scene");
    dlg.setVar("hour", 20, "global");
    dlg.setVar("ratio", 1.5, "global");
    dlg.setVar("flag", true, "global");

    if (dlg.getVarType("mood", "scene") != "string") return false;
    if (dlg.getVarInt("hour", 0, "global") != 20) return false;
    if (dlg.getVarFloat("ratio", 0.0, "global") != 1.5) return false;
    if (!dlg.getVarBool("flag", false, "global")) return false;
    if (dlg.hasVar("mood", "global")) return false;

    local n = dlg.loadPoolsFromTable({
        pools = {
            "alice.greet" : {
                noRepeat = 2,
                lines = [
                    { id = "a1", speaker = "alice", text = "你好，{name}！", weight = 2,
                      when = { var = "mood", op = "eq", value = "happy" },
                      meta = { expression = "happy", voice = "vo/a1.ogg" },
                      tags = ["greet"] },
                    { id = "a2", speaker = "alice", text = "晚安。", weight = 1,
                      when = { var = "mood", op = "eq", value = "sad" } },
                    { id = "a3", speaker = "", text = "（旁白）" },
                ],
            },
        },
    });
    if (n != 1) return false;
    if (!dlg.hasPool("alice.greet")) return false;
    if (dlg.getPoolCount() != 1) return false;

    if (!dlg.evalCondition({ var = "mood", op = "eq", value = "happy" })) return false;
    if (dlg.evalCondition({ var = "mood", op = "eq", value = "sad" })) return false;
    if (!dlg.evalCondition({
        all = [ { var = "hour", op = "ge", value = 18 },
                { var = "mood", op = "eq", value = "happy" } ],
    })) return false;

    dlg.registerCondition("isNight", function(ctx) {
        return ctx.vars.hour >= 18;
    });
    if (!dlg.evalCondition({ script = "isNight" })) return false;
    dlg.unregisterCondition("isNight");
    if (dlg.evalCondition({ script = "isNight" })) return false;

    local lineId = dlg.pickLine("alice.greet", { name = "Alice" });
    if (lineId == "") return false;
    if (!dlg.playLine(lineId, { name = "Bob" })) return false;
    if (dlg.getCurrentLineId() != lineId) return false;
    if (dlg.getFullText() != "你好，Bob！") return false;
    if (dlg.getCurrentLineMeta("voice") != "vo/a1.ogg") return false;
    local tags = dlg.getCurrentLineTags();
    if (tags.len() != 1 || tags[0] != "greet") return false;

    if (!dlg.playPool("alice.greet", { name = "Carol" })) return false;
    if (dlg.getCurrentLineId() == "") return false;

    // scene 区隔离：清 scene 不影响 global。
    dlg.clearVars("scene");
    if (dlg.hasVar("mood", "scene")) return false;
    if (!dlg.hasVar("hour", "global")) return false;

    dlg.clearPools();
    return true;
}
