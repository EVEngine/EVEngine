// Optional stock dialogue view. Games can replace it without changing Dialogue.
class DefaultDialogueUI {
    dlg = null;
    ux = null;
    uiModule = null;
    host = "defaultDialogue";
    mounted = false;
    lastRecordedLine = "";
    presentation = null;
    builtChoices = -1;
    stockMounted = false;

    /** @brief Replace presentation options, then mount to rebuild. Dialogue/UX remain authoritative.
     * Options are copied. Callbacks run synchronously on the game thread, outside locks;
     * do not recursively mount/update/render. The game owns UI, graphics and callback resources.
     */
    function setPresentation(options) {
        presentation = clone options;
    }

    function option(key, fallback) {
        return presentation != null && key in presentation ? presentation[key] : fallback;
    }

    /** @brief Owning frame projection; custom renderers may change it without changing Dialogue. */
    function snapshot() {
        local choices = [];
        if (dlg.isWaitingChoice())
            for (local i = 0; i < dlg.getChoiceCount(); ++i)
                choices.append({ index = i, id = dlg.getChoiceId(i), label = dlg.getChoiceLabel(i) });
        return { speaker = dlg.getSpeakerName(), speakerId = dlg.getSpeakerId(),
                 text = ux.plainText(dlg.getVisibleText()), typing = dlg.isTyping(),
                 waitingAdvance = dlg.isWaitingAdvance(), idle = dlg.isIdle(), choices = choices };
    }

    constructor(dialogueInstance, uxInstance, uiInstance, hostName = "defaultDialogue") {
        dlg = dialogueInstance;
        ux = uxInstance;
        uiModule = uiInstance;
        host = hostName;
    }

    function mount() {
        if (option("draw", null) != null) {
            if (stockMounted) {
                uiModule.select(host);
                uiModule.setHostVisible(false);
            }
            mounted = true;
            return;
        }
        if (uiModule == null) throw "DefaultDialogueUI: stock presentation requires UI";
        uiModule.beginBuild();
        // UI hosts need a window root; overlay flags remove its desktop chrome.
        uiModule.beginWindow("Dialogue", "hostRoot");
        local skin = option("skin", "");
        if (skin != "") {
            if (!uiModule.beginNinePatch(skin, "root", option("width", 720.0), option("height", 180.0)))
                throw "DefaultDialogueUI: invalid nine-patch: " + skin;
        } else {
            uiModule.beginGroup("root");
        }
        uiModule.text("", "speaker");
        uiModule.textWrapped("", option("width", 720.0) - 64.0, "line");
        uiModule.separator("separator");
        builtChoices = dlg.isWaitingChoice() ? dlg.getChoiceCount() : 0;
        for (local i = 0; i < builtChoices; ++i)
            uiModule.button("", "choice" + i);
        uiModule.textWrapped(option("hint", ""), option("width", 720.0) - 64.0, "hint");
        uiModule.end();
        uiModule.end();
        uiModule.mountBuildAs(host);
        stockMounted = true;
        uiModule.select(host);
        uiModule.setHostOverlay(true);
        uiModule.setHostOverlayAlpha(0.0);
        uiModule.setHostMovable(false);
        uiModule.setHostVisible(true);
        uiModule.setHostPos(option("x", 24.0), option("y", 24.0), 0.0, 0.0);
        uiModule.setColor("speaker", 0.94, 0.76, 0.42, 1.0);
        mounted = true;
    }

    function update(dt, voicePlaying = false) {
        if (!mounted) return false;
        local choiceCount = dlg.isWaitingChoice() ? dlg.getChoiceCount() : 0;
        if (option("draw", null) == null) {
            if (builtChoices != choiceCount) mount();
            uiModule.select(host);
            local layout = option("layout", null);
            if (layout != null) {
                local position = layout(snapshot());
                uiModule.setHostPos(position.x, position.y, 0.0, 0.0);
            }
            uiModule.setText("speaker", dlg.getSpeakerName());
            uiModule.setText("line", ux.plainText(dlg.getVisibleText()));
            for (local i = 0; i < choiceCount; ++i)
                uiModule.setText("choice" + i, (i + 1) + ". " + dlg.getChoiceLabel(i));
        }
        if (dlg.isWaitingAdvance()) {
            local lineId = dlg.getCurrentLineId();
            if (lineId == "") lineId = "runtime:" + dlg.getSpeakerId() + ":" + dlg.getFullText();
            if (lineId != lastRecordedLine) {
                ux.record(lineId, dlg.getSpeakerName(), dlg.getFullText());
                lastRecordedLine = lineId;
                ux.resetAutoTimer();
            }
            if (ux.shouldSkip(lineId) || ux.updateAuto(dt, voicePlaying)) {
                dlg.advance();
                return true;
            }
        }
        return false;
    }

    function render() {
        if (!mounted) return;
        local draw = option("draw", null);
        if (draw != null) draw(snapshot());
        else {
            local background = option("drawBackground", null);
            if (background != null) background(snapshot());
            uiModule.beginFrameAndRender();
        }
    }

    /** @brief Consume a stock choice click. Game validates/selects and resumes its runner. */
    function consumeChoice() {
        if (!mounted || option("draw", null) != null || uiModule == null) return -1;
        uiModule.select(host);
        local clicked = uiModule.consumeClick();
        for (local i = 0; i < builtChoices; ++i)
            if (clicked == host + "/choice" + i) return i;
        return -1;
    }

    /** @brief Hide this host and release callbacks; caller-owned UI/resources remain alive. */
    function unmount() {
        if (stockMounted) {
            uiModule.select(host);
            uiModule.setHostVisible(false);
        }
        mounted = false;
        presentation = null;
    }
}

function make_default_dialogue_ui(dialogueInstance, uxInstance, uiInstance,
                                  hostName = "defaultDialogue") {
    return DefaultDialogueUI(dialogueInstance, uxInstance, uiInstance, hostName);
}
