// Optional stock dialogue view. Games can replace it without changing Dialogue.
class DefaultDialogueUI {
    dlg = null;
    ux = null;
    uiModule = null;
    host = "defaultDialogue";
    mounted = false;
    lastRecordedLine = "";

    constructor(dialogueInstance, uxInstance, uiInstance, hostName = "defaultDialogue") {
        dlg = dialogueInstance;
        ux = uxInstance;
        uiModule = uiInstance;
        host = hostName;
    }

    function mount() {
        uiModule.beginBuild();
        uiModule.beginWindow("Dialogue", "root");
        uiModule.text("", "speaker");
        uiModule.text("", "line");
        uiModule.separator("separator");
        for (local i = 0; i < 8; ++i)
            uiModule.text("", "choice" + i);
        uiModule.end();
        uiModule.mountBuildAs(host);
        uiModule.select(host);
        uiModule.setHostOverlay(true);
        mounted = true;
    }

    function update(dt, voicePlaying = false) {
        if (!mounted) return false;
        uiModule.select(host);
        uiModule.setText("speaker", dlg.getSpeakerName());
        uiModule.setText("line", ux.plainText(dlg.getVisibleText()));
        local choiceCount = dlg.isWaitingChoice() ? dlg.getChoiceCount() : 0;
        for (local i = 0; i < 8; ++i) {
            local label = i < choiceCount ? ((i + 1) + ". " + dlg.getChoiceLabel(i)) : "";
            uiModule.setText("choice" + i, label);
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
        if (mounted) uiModule.beginFrameAndRender();
    }
}

function make_default_dialogue_ui(dialogueInstance, uxInstance, uiInstance,
                                  hostName = "defaultDialogue") {
    return DefaultDialogueUI(dialogueInstance, uxInstance, uiInstance, hostName);
}
