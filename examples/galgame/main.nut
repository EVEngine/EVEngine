// 潮汐电台：一段可分支、可存读档、可回看的原创 Galgame 短篇。

gal <- persist("gal", function() {
    return {
        mode = "title", step = 0, route = "", trust = 0, ending = "",
        uiMode = "", lineRecorded = false, toast = "", toastLeft = 0.0,
        mouseWas = false, touchWas = false,
        bg = null, radioBg = null, dawnBg = null,
        linTex = null, zhouTex = null, lin = null, zhou = null, ux = null,
        scene = "station", previousScene = "station", chapter = "序章 · 归站",
        fx = "rain", time = 0.0, effectTime = 0.0, transition = 0.0,
        entrance = 0.0, shake = 0.0, chapterLeft = 0.0,
        qaRoute = ""
    };
});

local story = [
    { who = "", text = "雨停在末班车到站前。七年没有回来的海鸣站，仍在播放那段无人认领的电台杂音。", scene = "station", fx = "rain", chapter = "序章 · 归站" },
    { who = "", text = "林澄拖着一只旧行李箱走下车。站牌被盐雾磨白，时刻表永远停在七年前的九月。" },
    { who = "林澄", text = "如果那封信没有骗我，今晚九点，旧频率会再次响起。", action = "enter" },
    { who = "", text = "她从大衣口袋里摸出折了四次的信。信上只有一行字：想知道你父亲最后说了什么，就回海鸣。" },
    { who = "林澄", text = "寄信人连名字都不敢写，却知道我每年都会换地址。真会给人添麻烦。" },
    { who = "", text = "站台尽头传来鞋跟踏过积水的声音。先是一把深蓝色的伞，然后是伞下熟悉又陌生的脸。", fx = "lightning" },
    { who = "周岚", text = "你果然还是来了。伞还是以前那把，修过三次的那把。", action = "enter" },
    { who = "林澄", text = "周岚？这封信是你寄的？" },
    { who = "周岚", text = "嗯。我在邮局柜台前站了四十分钟，最后还是忘了署名。" },
    { who = "林澄", text = "你也还是老样子——把想问的话藏在收音机后面。" },
    { who = "周岚", text = "你倒变了。以前生气会先喊我的全名，现在只挑最疼的地方说。" },
    { who = "", text = "两个人隔着七年的空白相望。雨水从铁棚边缘坠落，在她们之间拉起透明的帘。" },
    { who = "林澄", text = "我只待到明早。把录音给我，然后我们就当今晚没见过。" },
    { who = "周岚", text = "录音不在我身上。在旧灯塔的值班室，而且它只会在九点收到完整信号。" },
    { who = "林澄", text = "那座灯塔七年前就封了。" },
    { who = "周岚", text = "所以我带了钥匙，也带了向镇公所写检讨的觉悟。走吧，离九点还有四十分钟。" },
    { who = "", text = "她转身时故意放慢了一步。林澄看见她伞柄上还系着中学时代交换的蓝色丝带。" },
    { who = "林澄", text = "等一下。你的伞偏了。" },
    { who = "周岚", text = "我知道。以前也是偏向你这边。", action = "close" },
    { who = "", text = "通往灯塔的坡道被海风吹得发亮。远处浪峰接连碎开，像黑暗中翻动的信纸。", fx = "wind" },
    { who = "林澄", text = "镇上怎么样？车站、学校，还有那家总把盐当糖放的面包店。" },
    { who = "周岚", text = "车站要拆了，学校只剩一个班。面包店换了招牌，但老板还是会放错。" },
    { who = "林澄", text = "听起来什么都变了，又什么都没变。" },
    { who = "周岚", text = "有人也一样。每周五替你父亲擦灯塔的纪念牌，却从来不敢告诉你。" },
    { who = "林澄", text = "为什么现在才说？" },
    { who = "周岚", text = "因为我曾经相信沉默能保护你。后来才明白，沉默只是在替恐惧保守秘密。" },
    { who = "", text = "一道闪电劈开海面，灯塔的轮廓在山崖上显现。周岚下意识抓住林澄的手腕。", fx = "lightning", action = "shake" },
    { who = "林澄", text = "你还怕雷？" },
    { who = "周岚", text = "不怕。只是确认你没有又一个人跑掉。" },
    { who = "林澄", text = "这次不会。至少在听完之前。" },
    { who = "", text = "铁门在锈蚀的轨道上发出长鸣。尘封的值班室里，仪表竟仍亮着微弱的琥珀色。", scene = "radio", fx = "fade", chapter = "第一章 · 四十秒录音" },
    { who = "周岚", text = "备用电源每个月自动测试一次。我偷偷换过三次电池。" },
    { who = "林澄", text = "所以你一直在等这台机器醒来？" },
    { who = "周岚", text = "在等一个能和我一起听的人。" },
    { who = "", text = "周岚旋动调谐钮。噪声从扬声器涌出，时而像暴雨，时而像许多人隔海低语。", fx = "signal" },
    { who = "林澄", text = "频率是 87.9……我父亲值夜班时总让我守着它。" },
    { who = "周岚", text = "七年前风暴登陆那晚，这里收到一段四十秒录音。镇公所只公开了前十二秒。" },
    { who = "林澄", text = "公开的部分说灯塔设备失灵，管理员擅自留守。所有人都说他为了工作抛下家人。" },
    { who = "周岚", text = "而我父亲负责事故调查。他临终前把原始磁带交给我，要我等你愿意回来。" },
    { who = "林澄", text = "你们替我决定了七年。现在又要我感谢你们吗？", action = "shake" },
    { who = "周岚", text = "不用。你可以恨我。但请把决定权拿回去——听，或者不听。" },
    { who = "", text = "墙上的钟跳到八点五十九分。磁带轮缓慢转动，红色录音灯像一颗迟到了七年的心脏。", fx = "signal" }
];

local routeTruth = [
    { who = "林澄", text = "播放吧。真相不会因为我们转身而消失。", scene = "radio", fx = "signal", chapter = "第二章 · 让海说完" },
    { who = "", text = "九点整，接收指示灯从红转绿。一个被风撕碎的男声穿过噪音抵达此刻。", fx = "flash" },
    { who = "父亲的录音", text = "海堤东段已经决口。不是设备故障，重复，不是设备故障。请立刻拉响全镇警报。" },
    { who = "父亲的录音", text = "自动警报线路被雷击熔断。我会手动启动灯塔汽笛，争取十五分钟。所有船只向北湾避难。" },
    { who = "林澄", text = "这是他的声音……他那天明明答应回来给我过生日。" },
    { who = "父亲的录音", text = "如果澄澄以后听见这段话，告诉她，对不起。爸爸不是选择灯塔，是选择让她生活的小镇。" },
    { who = "", text = "录音在剧烈的撞击声中中断。灯塔外，真正的雷声与七年前重叠，整间值班室轻轻震动。", fx = "lightning", action = "shake" },
    { who = "周岚", text = "那晚汽笛真的响了。镇上三千多人因此提前撤离，没有一艘渔船失踪。" },
    { who = "林澄", text = "可事故报告为什么写成擅自留守？" },
    { who = "周岚", text = "旧海堤偷工减料。承包方和当年的镇长压下录音，把责任推给无法申辩的人。" },
    { who = "林澄", text = "你父亲参与了调查，所以他才一直内疚。" },
    { who = "周岚", text = "嗯。他留下证据，却没有勇气公开。我也把他的胆怯继承得很完整。" },
    { who = "林澄", text = "不。你把磁带保存了七年，还站在车站等我。这已经比他们勇敢。" },
    { who = "周岚", text = "那你呢？准备拿它怎么办？" },
    { who = "林澄", text = "复制三份。明早交给记者、警署和每一个曾叫他逃兵的人。我要他们听完整整四十秒。" },
    { who = "", text = "周岚按下复制键。两盘磁带同时转动，微小而坚定，像终于开始校正的时间。", fx = "signal" },
    { who = "周岚", text = "林澄，对不起。七年前你离开时，我明明追到了车站，却躲在柱子后面。" },
    { who = "林澄", text = "我知道。我在车窗倒影里看见你了。" },
    { who = "周岚", text = "那你为什么没有下车？" },
    { who = "林澄", text = "因为我也在等你先开口。我们两个胆小鬼，白白输给一扇车门七年。" },
    { who = "", text = "风暴在凌晨四点越过海岸。两人抱着装有真相的磁带走出灯塔，东方已经泛白。", scene = "dawn", fx = "dawn", chapter = "终章 · 归航" },
    { who = "周岚", text = "镇上第一班面包刚出炉。要不要赌老板今天放的是糖还是盐？", action = "close" },
    { who = "林澄", text = "我只待到明早——本来是这么计划的。" },
    { who = "周岚", text = "那现在呢？" },
    { who = "林澄", text = "现在我想先把车票退掉，再陪你去擦一次纪念牌。以后每个星期五都可以。" },
    { who = "周岚", text = "欢迎回家，林澄。" },
    { who = "", text = "日出越过灯塔，潮水把漫长的黑夜推回海里。没有谁再被留在过去。", fx = "ending" }
];

local routeSilence = [
    { who = "林澄", text = "先关掉吧。今晚我不想让一个死去的人替活着的人回答所有问题。", scene = "radio", fx = "fade", chapter = "第二章 · 此刻的声音" },
    { who = "", text = "周岚没有劝她，只伸手按下停止键。旋转的磁带慢下来，值班室忽然只剩雨声。" },
    { who = "周岚", text = "好。磁带不会消失，你什么时候想听都可以。" },
    { who = "林澄", text = "我想先知道，你为什么一直等在这里。不是你父亲的遗愿，是你的理由。" },
    { who = "周岚", text = "因为有人答应过，等我们都不再害怕，就回来一起看流星。" },
    { who = "林澄", text = "那是十四岁时说的话。你居然当真？" },
    { who = "周岚", text = "我还留着你画的星图。虽然你把北斗七星画成了八颗。" },
    { who = "林澄", text = "多出来的那颗是给你的。你当时没看懂。" },
    { who = "周岚", text = "现在好像懂了。迟到七年，还算数吗？", action = "close" },
    { who = "林澄", text = "要看你愿不愿意重新说一遍。" },
    { who = "周岚", text = "林澄，我希望你留下。不是为了灯塔，不是为了真相，是因为我想和你拥有明天。" },
    { who = "", text = "窗外的雨渐渐变细。云层裂开一道缝，远海露出被星光照亮的航线。", fx = "stars" },
    { who = "林澄", text = "我还不能答应留下。但我可以答应，不再不告而别。" },
    { who = "周岚", text = "这已经足够了。我们有很多个明天可以慢慢谈。" },
    { who = "林澄", text = "录音怎么办？" },
    { who = "周岚", text = "封好，交给你保管。选择何时面对它的人应该是你，不是我，也不是过去。" },
    { who = "", text = "林澄把磁带放进贴身口袋。它不再是一道逼迫她回头的命令，而是一扇由她决定何时开启的门。" },
    { who = "周岚", text = "雨停了。海堤那边视野最好，流星雨还有二十分钟。" },
    { who = "林澄", text = "你连这个都算好了？" },
    { who = "周岚", text = "没有。我只是准备了两杯热咖啡、两条毯子，还有一张错误的八星星图。" },
    { who = "", text = "黎明前最深的蓝色覆盖海堤。两人并肩坐在尚未干透的长椅上。", scene = "dawn", fx = "stars", chapter = "终章 · 星约" },
    { who = "林澄", text = "第一颗。你看见了吗？" },
    { who = "周岚", text = "嗯。但我不许愿了。" },
    { who = "林澄", text = "为什么？" },
    { who = "周岚", text = "因为等愿望自己实现用了七年。这次我想亲手去做。", action = "close" },
    { who = "林澄", text = "那就从陪我去退掉明早的车票开始。" },
    { who = "", text = "第一缕晨光落在潮湿的栏杆上。两只手轻轻碰到一起，这一次谁也没有先躲开。", fx = "ending" }
];

function currentLines() {
    if (gal.route == "truth") return routeTruth;
    if (gal.route == "silence") return routeSilence;
    return story;
}

function currentLine() {
    local lines = currentLines();
    if (gal.step < 0 || gal.step >= lines.len()) return null;
    return lines[gal.step];
}

function makeAvatar(texture, slot) {
    local av = avatar.newImageAvatar();
    av.addLayer("portrait", texture, 0);
    av.setLayerSize("portrait", 520.0, 780.0);
    av.setLayerOffset("portrait", -260.0, 0.0);
    av.setPosition(0.0, 24.0);
    av.setLayer(10);
    av.setVisible(false);
    return av;
}

function setToast(text) {
    gal.toast = text;
    gal.toastLeft = 2.5;
}

function saveGame() {
    local payload = gal.step + "|" + gal.route + "|" + gal.trust + "|" + gal.mode;
    if (fs.writeText("tidal_frequency_slot1.sav", payload)) setToast("已保存到存档 1");
    else setToast("存档失败：保存目录不可写");
}

function loadGame() {
    local data = fs.readText("tidal_frequency_slot1.sav");
    if (data == "") { setToast("还没有存档"); return; }
    local p1 = data.find("|");
    local tail1 = p1 == null ? "" : data.slice(p1 + 1);
    local rel2 = tail1.find("|");
    local p2 = rel2 == null ? null : p1 + 1 + rel2;
    local tail2 = p2 == null ? "" : data.slice(p2 + 1);
    local rel3 = tail2.find("|");
    local p3 = rel3 == null ? null : p2 + 1 + rel3;
    if (p1 == null || p2 == null || p3 == null) { setToast("存档格式无效"); return; }
    gal.step = data.slice(0, p1).tointeger();
    gal.route = data.slice(p1 + 1, p2);
    gal.trust = data.slice(p2 + 1, p3).tointeger();
    gal.mode = data.slice(p3 + 1);
    if (gal.mode != "game") gal.mode = "game";
    gal.uiMode = "";
    showCurrentLine();
    setToast("已读取存档 1");
}

function configureStage() {
    dialogue.reset();
    dialogue.registerCharacter("lin", "林澄");
    dialogue.registerCharacter("zhou", "周岚");
    dialogue.registerCharacter("recording", "父亲的录音");
    dialogue.bindAvatar("lin", gal.lin);
    dialogue.bindAvatar("zhou", gal.zhou);
    dialogue.setSlotX("left", 0.27);
    dialogue.setSlotX("right", 0.73);
    dialogue.setTypeSpeed(34.0);
}

function showCurrentLine() {
    configureStage();
    local line = currentLine();
    if (line == null) return;
    if ("scene" in line && line.scene != gal.scene) {
        gal.previousScene = gal.scene;
        gal.scene = line.scene;
        gal.transition = 1.0;
        print("[galgame] scene=" + gal.scene + "\n");
    }
    if ("fx" in line) gal.fx = line.fx;
    gal.effectTime = 0.0;
    gal.entrance = ("action" in line && line.action == "enter") ? 1.0 : 0.0;
    if ("action" in line && line.action == "shake") gal.shake = 18.0;
    if ("chapter" in line) {
        gal.chapter = line.chapter;
        gal.chapterLeft = 3.0;
    }
    if (line.who == "林澄") {
        dialogue.show("lin", "left");
        if (gal.route != "") dialogue.show("zhou", "right");
        dialogue.say("lin", line.text);
    } else if (line.who == "周岚") {
        dialogue.show("lin", "left");
        dialogue.show("zhou", "right");
        dialogue.say("zhou", line.text);
    } else if (line.who == "父亲的录音") {
        dialogue.show("lin", "left");
        dialogue.show("zhou", "right");
        dialogue.say("recording", line.text);
    } else {
        if (gal.step > 1) { dialogue.show("lin", "left"); dialogue.show("zhou", "right"); }
        dialogue.narrate(line.text);
    }
    gal.lineRecorded = false;
}

function startGame() {
    gal.mode = "game"; gal.step = 0; gal.route = ""; gal.trust = 0; gal.ending = "";
    gal.ux.clearHistory();
    gal.scene = "station"; gal.previousScene = "station"; gal.fx = "rain";
    gal.time = 0.0; gal.transition = 1.0; gal.chapterLeft = 3.0;
    gal.uiMode = "";
    print("[galgame] route start: common\n");
    showCurrentLine();
}

function chooseRoute(route) {
    gal.route = route;
    gal.trust = route == "truth" ? 2 : 1;
    gal.step = 0;
    gal.mode = "game";
    gal.uiMode = "";
    print("[galgame] branch=" + route + "\n");
    showCurrentLine();
}

function finishRoute() {
    gal.ending = gal.route == "truth" ? "TRUE END · 归航" : "GOOD END · 星约";
    gal.mode = "ending";
    gal.uiMode = "";
    print("[galgame] ending=" + gal.ending + "\n");
}

function advanceStory() {
    if (dialogue.isTyping()) { dialogue.skipTyping(); return; }
    if (!dialogue.isWaitingAdvance()) return;
    dialogue.advance();
    local line = currentLine();
    if (line != null && !gal.lineRecorded) {
        gal.ux.record(gal.route + ":" + gal.step, line.who, line.text);
        gal.lineRecorded = true;
    }
    gal.step += 1;
    if (gal.route == "" && gal.step >= story.len()) {
        gal.mode = "choice"; gal.uiMode = ""; return;
    }
    if (gal.route != "" && gal.step >= currentLines().len()) { finishRoute(); return; }
    showCurrentLine();
}

function rebuildUI() {
    ui.beginBuild();
    if (gal.mode == "title") {
        ui.beginWindow("潮汐电台", "root");
        ui.text("TIDAL FREQUENCY", "subtitle");
        ui.separator("s");
        ui.text("一段关于旧电台、海风与迟到七年的约定", "blurb");
        ui.button("开始故事", "start");
        ui.button("读取存档 1", "load");
    } else if (gal.mode == "history") {
        ui.beginWindow("回想", "root");
        local count = gal.ux.getHistoryCount();
        local first = count > 9 ? count - 9 : 0;
        for (local i = first; i < count; ++i) {
            local speaker = gal.ux.getHistorySpeaker(i);
            ui.text((speaker == "" ? "旁白" : speaker) + "：" + gal.ux.getHistoryText(i), "h" + i);
        }
        ui.separator("s"); ui.button("返回", "back");
    } else if (gal.mode == "choice") {
        ui.beginWindow("你的选择", "root");
        ui.text("电台开始播放。你要和周岚一起听完七年前的录音吗？", "question");
        ui.button("[1] 一起听完，面对真相", "truth");
        ui.button("[2] 关掉电台，先说此刻", "silence");
    } else if (gal.mode == "ending") {
        ui.beginWindow(gal.ending, "root");
        ui.text(gal.route == "truth" ? "你们让沉默多年的真相重新被听见。" : "有些答案可以等到不再害怕的明天。", "endingText");
        ui.text("信赖度  " + gal.trust + " / 2", "trust");
        ui.separator("s"); ui.button("从头开始", "restart"); ui.button("回到标题", "title");
    } else {
        ui.beginWindow("潮汐电台", "root");
        ui.text("", "speaker");
        ui.text("", "line");
        ui.separator("s");
        ui.button("AUTO", "auto"); ui.sameLine("a");
        ui.button("SKIP", "skip"); ui.sameLine("b");
        ui.button("LOG", "history"); ui.sameLine("c");
        ui.button("SAVE", "save"); ui.sameLine("d");
        ui.button("LOAD", "load");
        ui.text("", "status");
    }
    ui.end();
    ui.mountBuildAs("galui"); ui.select("galui"); ui.setHostOverlay(true);
    ui.setHostOverlayAlpha(gal.mode == "game" ? 0.82 : 0.90);
    if (gal.mode == "game") {
        // Classic visual-novel composition: a wide dialogue box anchored to
        // the physical bottom edge, independent of its content height.
        ui.setHostAnchor(0.5, 1.0);
        ui.setHostPos(0.0, -18.0, 0.5, 1.0);
        ui.setHostPercent(0.94, 0.32);
    } else {
        ui.setHostAnchor(0.5, 0.5);
        ui.setHostPos(0.0, 0.0, 0.5, 0.5);
        ui.setHostSize(gal.mode == "history" ? 1120.0 : 820.0,
                       gal.mode == "history" ? 680.0 : 440.0);
    }
    gal.uiMode = gal.mode;
}

function handleClick(id) {
    if (id == "galui/start" || id == "galui/restart") startGame();
    else if (id == "galui/title") { gal.mode = "title"; gal.uiMode = ""; }
    else if (id == "galui/load") loadGame();
    else if (id == "galui/save") saveGame();
    else if (id == "galui/history") { gal.mode = "history"; gal.uiMode = ""; }
    else if (id == "galui/back") { gal.mode = "game"; gal.uiMode = ""; }
    else if (id == "galui/truth") chooseRoute("truth");
    else if (id == "galui/silence") chooseRoute("silence");
    else if (id == "galui/auto") {
        gal.ux.setAutoMode(!gal.ux.isAutoMode());
        setToast(gal.ux.isAutoMode() ? "自动播放：开" : "自动播放：关");
    } else if (id == "galui/skip") {
        local next = gal.ux.getSkipMode() == "all" ? "off" : "all";
        gal.ux.setSkipMode(next); setToast(next == "all" ? "快进：开" : "快进：关");
    }
}

function eve_init() {
    gfx.setBackgroundColor(0.02, 0.03, 0.08, 1.0);
    ui.setScale(1.35);
    fs.setIdentity("tidal-frequency", true); fs.setupWriteDirectory();
    gal.bg = gfx.newTextureFromFile("assets/station_twilight.png");
    gal.radioBg = gfx.newTextureFromFile("assets/lighthouse_radio.png");
    gal.dawnBg = gfx.newTextureFromFile("assets/seawall_dawn.png");
    gal.linTex = gfx.newTextureFromFile("assets/lin.png");
    gal.zhouTex = gfx.newTextureFromFile("assets/zhou.png");
    gal.lin = makeAvatar(gal.linTex, "left"); gal.zhou = makeAvatar(gal.zhouTex, "right");
    // With typing time plus this reading pause, a complete AUTO route runs
    // about ten minutes (69 displayed lines, excluding title/choice time).
    gal.ux = eve.DialogueUX(); gal.ux.setAutoDelay(7.8);
    // A temporary marker beside main.nut lets maintainers exercise a complete
    // route without changing normal player behaviour.
    gal.qaRoute = file_exists(".galgame-qa-truth") ? "truth" :
                  (file_exists(".galgame-qa-silence") ? "silence" : "");
    rebuildUI();
    if (gal.qaRoute != "") {
        print("[galgame] qa route=" + gal.qaRoute + "\n");
        startGame();
    }
}

function eve_reload() { gal.uiMode = ""; }

function eve_update(dt) {
    gal.time += dt;
    gal.effectTime += dt;
    if (gal.transition > 0.0) {
        gal.transition -= dt * 0.85;
        if (gal.transition < 0.0) gal.transition = 0.0;
    }
    if (gal.entrance > 0.0) {
        gal.entrance -= dt * 1.8;
        if (gal.entrance < 0.0) gal.entrance = 0.0;
    }
    if (gal.shake > 0.0) {
        gal.shake -= dt * 24.0;
        if (gal.shake < 0.0) gal.shake = 0.0;
    }
    if (gal.chapterLeft > 0.0) gal.chapterLeft -= dt;
    if (gal.uiMode != gal.mode) rebuildUI();
    local clickedUI = false;
    while (true) {
        local id = ui.consumeClick();
        if (id == "") break;
        clickedUI = true;
        handleClick(id);
    }
    local mouseNow = mouse.isDown(1);
    local touchNow = touch.getTouchCount() > 0;
    local pointerAdvance = (mouseNow && !gal.mouseWas) || (touchNow && !gal.touchWas);
    gal.mouseWas = mouseNow;
    gal.touchWas = touchNow;
    if (gal.mode == "choice") {
        if (gal.qaRoute != "") chooseRoute(gal.qaRoute);
        else {
            if (key_just_pressed("1")) chooseRoute("truth");
            if (key_just_pressed("2")) chooseRoute("silence");
        }
    } else if (gal.mode == "game") {
        dialogue.update(dt); avatar.update(dt);
        local line = currentLine();
        local activeLin = line != null && line.who == "林澄";
        local activeZhou = line != null && line.who == "周岚";
        local closePose = line != null && "action" in line && line.action == "close";
        local breathe = sin(gal.time * 1.8) * 3.0;
        local enterOffset = gal.entrance * gal.entrance * 150.0;
        gal.lin.setPosition(0.0, 24.0 + breathe + (activeLin ? 0.0 : 7.0));
        gal.zhou.setPosition(0.0, 24.0 - breathe + (activeZhou ? 0.0 : 7.0));
        gal.lin.setScale(activeLin ? 1.025 : 0.965, activeLin ? 1.025 : 0.965);
        gal.zhou.setScale(activeZhou ? 1.025 : 0.965, activeZhou ? 1.025 : 0.965);
        dialogue.setSlotX("left", closePose ? 0.36 : 0.27);
        dialogue.setSlotX("right", closePose ? 0.64 : 0.73);
        if (gal.entrance > 0.0) {
            if (activeLin) dialogue.setSlotX("left", 0.27 - enterOffset / config.width);
            if (activeZhou) dialogue.setSlotX("right", 0.73 + enterOffset / config.width);
        }
        dialogue.syncStage(config.width.tofloat(), config.height.tofloat());
        if (gal.qaRoute != "") {
            dialogue.skipTyping();
            if (dialogue.isWaitingAdvance()) advanceStory();
        }
        if (dialogue.isWaitingAdvance() && (gal.ux.getSkipMode() == "all" || gal.ux.updateAuto(dt, false))) advanceStory();
        if (key_just_pressed("Space") || key_just_pressed("Return")) advanceStory();
        if (pointerAdvance && !clickedUI) advanceStory();
        if (key_just_pressed("F5")) saveGame();
        if (key_just_pressed("F9")) loadGame();
        if (key_just_pressed("L")) { gal.mode = "history"; gal.uiMode = ""; }
        ui.select("galui");
        ui.setText("speaker", dialogue.getSpeakerName() == "" ? "旁白" : dialogue.getSpeakerName());
        ui.setText("line", dialogue.getVisibleText());
        local flags = (gal.ux.isAutoMode() ? "AUTO  " : "") + (gal.ux.getSkipMode() != "off" ? "SKIP  " : "");
        ui.setText("status", gal.chapter + "  ·  " + flags +
                   (gal.toastLeft > 0.0 ? gal.toast : "Space 推进 · F5 存档 · F9 读档 · L 回想"));
    }
    if (gal.toastLeft > 0.0) gal.toastLeft -= dt;
}

function sceneTexture(name) {
    if (name == "radio") return gal.radioBg;
    if (name == "dawn") return gal.dawnBg;
    return gal.bg;
}

function drawScene(name, alpha) {
    local sx = gal.shake > 0.0 ? sin(gal.time * 91.0) * gal.shake : 0.0;
    local sy = gal.shake > 0.0 ? cos(gal.time * 77.0) * gal.shake * 0.45 : 0.0;
    gfx.drawTexturedRect(sceneTexture(name), sx - 12.0, sy - 8.0,
                         config.width + 24.0, config.height + 16.0,
                         1.0, 1.0, 1.0, alpha);
}

function drawWeatherAndEffects() {
    if (gal.scene != "dawn" && (gal.fx == "rain" || gal.fx == "wind" || gal.fx == "lightning")) {
        for (local i = 0; i < 72; ++i) {
            local x = (i * 127.0 + gal.time * (gal.fx == "wind" ? 520.0 : 260.0)) % (config.width + 160.0) - 80.0;
            local y = (i * 83.0 + gal.time * 390.0) % config.height;
            gfx.drawSolidRect(x, y, gal.fx == "wind" ? 24.0 : 3.0, 18.0,
                              0.55, 0.72, 0.90, 0.20);
        }
    }
    if (gal.fx == "signal") {
        local pulse = 0.04 + (sin(gal.time * 7.0) + 1.0) * 0.025;
        for (local y = 0.0; y < config.height; y += 12.0)
            gfx.drawSolidRect(0.0, y, config.width.tofloat(), 2.0, 0.92, 0.62, 0.22, pulse);
    }
    if (gal.fx == "stars" || gal.fx == "ending") {
        for (local i = 0; i < 24; ++i) {
            local x = (i * 193.0 + gal.time * 24.0) % config.width;
            local y = 35.0 + (i * 71.0) % 360.0;
            local a = 0.25 + (sin(gal.time * 2.0 + i) + 1.0) * 0.25;
            gfx.drawSolidRect(x, y, 3.0, 3.0, 1.0, 0.88, 0.68, a);
        }
    }
    if (gal.fx == "lightning" && gal.effectTime < 0.42) {
        local a = (1.0 - gal.effectTime / 0.42) * (sin(gal.effectTime * 65.0) > 0.0 ? 0.72 : 0.18);
        gfx.drawSolidRect(0.0, 0.0, config.width.tofloat(), config.height.tofloat(), 0.72, 0.84, 1.0, a);
    }
    if (gal.fx == "flash" && gal.effectTime < 0.8)
        gfx.drawSolidRect(0.0, 0.0, config.width.tofloat(), config.height.tofloat(), 1.0, 0.78, 0.42,
                          (1.0 - gal.effectTime / 0.8) * 0.55);
}

function eve_render() {
    gfx.clear();
    if (gal.transition > 0.0) drawScene(gal.previousScene, 1.0);
    drawScene(gal.scene, 1.0 - gal.transition);
    drawWeatherAndEffects();
    if (gal.mode != "title") {
        avatar.render(gfx);
        gfx.drawSolidRect(0.0, 0.0, config.width.tofloat(), config.height.tofloat(), 0.03, 0.05, 0.12,
                          gal.mode == "game" ? 0.05 : 0.38);
    }
    if (gal.chapterLeft > 0.0 && gal.mode == "game") {
        local a = gal.chapterLeft > 2.3 ? (3.0 - gal.chapterLeft) / 0.7 : gal.chapterLeft / 0.7;
        if (a > 1.0) a = 1.0;
        gfx.drawSolidRect(0.0, config.height * 0.42, config.width.tofloat(), 96.0, 0.02, 0.03, 0.08, a * 0.78);
    }
    ui.beginFrameAndRender();
}
