// Standalone: sq test/dialogue_presentation.nut (from repository root).
dofile("src/scripts/default_dialogue_ui.nut");

class PresentationDialogue {
    choiceCount = 12;
    waiting = false;
    advances = 0;
    function isWaitingChoice() { return choiceCount > 0; }
    function getChoiceCount() { return choiceCount; }
    function getChoiceId(i) { return "route" + i; }
    function getChoiceLabel(i) { return "Choice " + i; }
    function getSpeakerName() { return "Alice"; }
    function getSpeakerId() { return "alice"; }
    function getVisibleText() { return "hello"; }
    function isTyping() { return true; }
    function isWaitingAdvance() { return waiting; }
    function getCurrentLineId() { return "test.line"; }
    function getFullText() { return "hello"; }
    function advance() { advances++; waiting = false; }
    function isIdle() { return false; }
}
class PresentationUI {
    buttons = 0;
    mounts = 0;
    visible = false;
    valid = true;
    clicked = "";
    skin = "";
    x = 0.0;
    function beginBuild() { buttons = 0; }
    function beginWindow(title, id) {}
    function beginGroup(id) {}
    function beginNinePatch(path, id, w, h) { skin = path; return valid; }
    function text(s, id) {}
    function textWrapped(s, w, id) { assert(w > 0); }
    function separator(id) {}
    function button(s, id) { buttons++; }
    function end() {}
    function mountBuildAs(host) { mounts++; }
    function select(host) {}
    function setHostOverlay(v) {}
    function setHostOverlayAlpha(v) {}
    function setHostMovable(v) {}
    function setHostVisible(v) { visible = v; }
    function setHostPos(nx, y, px, py) { x = nx; }
    function setColor(id, r, g, b, a) {}
    function setText(id, s) {}
    function beginFrameAndRender() {}
    function consumeClick() { local c = clicked; clicked = ""; return c; }
}
local dlg = PresentationDialogue();
local ux = { plainText = function(s) { return s; } };
local ui = PresentationUI();
local view = make_default_dialogue_ui(dlg, ux, ui);
view.setPresentation({skin = "moonlight.9.png"});
view.mount();
view.update(0.016);
assert(ui.buttons == 12); // Every choice is reachable, including index > 7.
ui.clicked = "defaultDialogue/choice11";
assert(view.consumeChoice() == 11);
assert(view.consumeChoice() == -1);
ui.clicked = "otherHost/choice0";
assert(view.consumeChoice() == -1);
local frame = view.snapshot();
frame.choices.clear();
assert(view.snapshot().choices.len() == 12);
local calls = 0;
view.setPresentation({draw = function(f) { calls++; assert(f.text == "hello"); }});
view.mount();
view.update(0.016);
view.render();
assert(calls == 1 && !ui.visible && dlg.choiceCount == 12);
view.setPresentation({skin = "letter.9.png"});
view.mount();
assert(ui.visible && ui.skin == "letter.9.png" && dlg.choiceCount == 12);
dlg.choiceCount = 2;
view.update(0.016);
assert(ui.buttons == 2);
local mounts = ui.mounts;
ui.valid = false;
local rejected = false;
try { view.mount(); } catch (e) { rejected = true; }
assert(rejected && ui.mounts == mounts); // Bad asset never replaces the mounted tree.
local custom = make_default_dialogue_ui(dlg, ux, null);
custom.setPresentation({draw = function(f) { calls++; }});
custom.mount();
custom.update(0.016);
custom.render();
assert(calls == 2); // A complete script renderer does not require UI.
custom.setPresentation({draw = function(f) { throw "paint failure"; }});
rejected = false;
try { custom.render(); } catch (e) { rejected = true; }
assert(rejected); // Callback failures remain observable.
ui.valid = true;
view.setPresentation({layout = function(f) { return {x = 123.0, y = 22.0}; }});
view.mount();
view.update(0.016);
assert(ui.x == 123.0);
view.unmount();
assert(!ui.visible && view.consumeChoice() == -1);
local records = 0;
local autoUX = {
    plainText = function(s) { return s; },
    record = function(id, who, text) { records++; },
    resetAutoTimer = function() {},
    shouldSkip = function(id) { return false; },
    updateAuto = function(dt, playing) { return !playing; }
};
local autoView = make_default_dialogue_ui(dlg, autoUX, null);
autoView.setPresentation({draw = function(f) {}});
autoView.mount();
dlg.choiceCount = 0;
dlg.waiting = true;
assert(!autoView.update(0.016, true));
assert(records == 1 && dlg.advances == 0);
autoView.setPresentation({draw = function(f) {}});
autoView.mount();
assert(autoView.update(0.016, false));
assert(records == 1 && dlg.advances == 1); // Switching never resets read history/timer.
print("PASS dialogue presentation contracts\n");
