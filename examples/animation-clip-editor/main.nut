// Project-composed animation clip editor. Native code owns the document,
// pose preview, skeleton overlay, dope-sheet layout, transactions and undo/redo.

persist animUi = {
    workspace = null, clip = null, mouseDown = false,
    status = "Loading preview clip...",
    spaceWas = false, undoWas = false, redoWas = false,
    frame = 0, screenshotSaved = false,
};

function requireResult(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result.value;
}

function buildWorkspace() {
    animUi.workspace = editor.newWorkspace("preview.animation", "Preview Walk Editor");
    animUi.clip = requireResult(eve.AnimationEditorModule().create("asset.preview.walk"),
                                "Create animation clip editor");
    requireResult(animUi.clip.configureWorkspace(animUi.workspace), "Compose workspace");
    animUi.status = "Seeded two-bone walk clip";
    animUi.workspace.setRegionSize("left", 220.0);
    animUi.workspace.setRegionSize("right", 280.0);
    animUi.workspace.setRegionSize("bottom", 180.0);
    animUi.workspace.layout(config.width.tofloat(), config.height.tofloat());
}

function panelSkeleton() {
    ui.text("Skeleton", "skeleton-title");
    ui.listItem("Hips", "bone-Hips");
    ui.listItem("Spine", "bone-Spine");
    ui.text("", "bone-selection");
    ui.separator("skeleton-sep");
    ui.textWrapped("Selecting a bone highlights its overlay axes and dope-sheet row.",
                   190.0, "skeleton-help");
}

function panelPreview() {
    ui.text("Pose follows the playhead. Overlay primitives are CPU-side.", "preview-help");
    ui.text("", "preview-status");
    ui.viewport("anim-pose", animUi.workspace.getRegionW("center") - 20.0,
                animUi.workspace.getRegionH("center") - 72.0);
}

function panelInspector() {
    ui.text("Clip Inspector", "inspector-title");
    ui.text("", "selected-bone");
    ui.text("", "revision");
    ui.separator("inspector-sep-1");
    ui.slider("Duration", animUi.clip.getDuration(), 0.25, 8.0, "duration");
    ui.slider("Sample rate", animUi.clip.getSampleRate(), 1.0, 120.0, "sample-rate");
    ui.checkbox("Loop", animUi.clip.getLoop(), "loop");
    ui.slider("Mask", animUi.clip.getSelectedMaskWeight(), 0.0, 1.0, "mask");
    ui.beginRow("inspector-history", 8.0); ui.button("Undo", "undo"); ui.button("Redo", "redo"); ui.end();
    ui.textWrapped("Mask weight dims the selected bone in the overlay on the next sample.",
                   245.0, "inspector-help");
}

function panelTimeline() {
    ui.beginRow("transport", 8.0);
    ui.button("Play / Pause", "play-pause");
    ui.button("Stop", "stop");
    ui.button("Undo", "undo");
    ui.button("Redo", "redo");
    ui.end();
    ui.text("", "timeline-status");
    ui.viewport("anim-dopesheet", animUi.workspace.getRegionW("bottom") - 20.0,
                animUi.workspace.getRegionH("bottom") - 88.0);
}

panelBuilders <- {
    ["animation.skeleton"]=panelSkeleton,
    ["animation.preview"]=panelPreview,
    ["animation.inspector"]=panelInspector,
    ["animation.timeline"]=panelTimeline
};

function mountPanels() {
    for (local i = 0; i < animUi.workspace.getPanelCount(); ++i) {
        local id = animUi.workspace.getPanelId(i);
        if (!(id in panelBuilders)) continue;
        ui.beginBuild(); ui.beginWindow(animUi.workspace.getPanelTitle(i), "root");
        panelBuilders[id](); ui.end(); ui.mountBuildAs(id); ui.select(id);
        local region = animUi.workspace.getPanelRegion(i);
        ui.setHostPos(animUi.workspace.getRegionX(region), animUi.workspace.getRegionY(region), 0.0, 0.0);
        ui.setHostSize(animUi.workspace.getRegionW(region), animUi.workspace.getRegionH(region));
        ui.setHostOverlay(false);
    }
    requireResult(animUi.clip.setViewport(animUi.workspace.getRegionW("bottom") - 20.0, 36.0, 120.0),
                  "Configure dope-sheet viewport");
}

function applyHistory(command) {
    local result = command == "undo" ? animUi.clip.undo() : animUi.clip.redo();
    animUi.status = result.ok ? command + " applied" : command + ": " + result.status.summary;
}

function eventParts(path) {
    local slash = path.find("/");
    return slash == null ? ["", path] : [path.slice(0, slash), path.slice(slash + 1)];
}

function handleUiEvents() {
    local click = ui.consumeClick();
    while (click != "") {
        local event = eventParts(click); local host = event[0]; local id = event[1];
        if (id == "play-pause") {
            if (animUi.clip.isPlaying()) {
                animUi.clip.pause(); animUi.status = "Preview paused";
            } else {
                animUi.clip.play(); animUi.status = "Preview playing";
            }
        } else if (id == "stop") {
            animUi.clip.stop(); animUi.status = "Preview stopped";
        } else if (id == "undo" || id == "redo") {
            applyHistory(id);
        } else if (host == "animation.skeleton") {
            local bone = id == "bone-Hips" ? "Hips" : id == "bone-Spine" ? "Spine" : "";
            if (bone != "") {
                requireResult(animUi.clip.selectBone(bone), "Select bone");
                animUi.status = "Selected " + bone;
            }
        }
        click = ui.consumeClick();
    }

    local changed = ui.consumeChange();
    while (changed != "") {
        local event = eventParts(changed); local host = event[0]; local id = event[1];
        if (host == "animation.inspector") {
            ui.select("animation.inspector");
            local result = { ok = true, status = { summary = "" } };
            if (id == "duration") result = animUi.clip.setDuration(ui.getValue("duration"));
            else if (id == "sample-rate") result = animUi.clip.setSampleRate(ui.getValue("sample-rate"));
            else if (id == "loop") result = animUi.clip.setLoop(ui.getChecked("loop"));
            else if (id == "mask") result = animUi.clip.setMaskWeight(ui.getValue("mask"));
            animUi.status = result.ok ? id + " committed · revision " + animUi.clip.getRevision()
                                      : result.status.summary;
        }
        changed = ui.consumeChange();
    }
}

function updateTimelinePointer() {
    ui.select("animation.timeline");
    local hovered = ui.viewportHovered("anim-dopesheet");
    local down = hovered && mouse.isDown(1);
    local x = ui.viewportMouseX("anim-dopesheet");
    local y = ui.viewportMouseY("anim-dopesheet");
    if (down && !animUi.mouseDown) {
        local hit = animUi.clip.pointerDown(x, y);
        animUi.status = hit.ok ? "Timeline " + animUi.clip.getSelectedBone() : hit.status.summary;
    }
    animUi.mouseDown = down;
}

function updateKeyboardShortcuts() {
    local space = keyboard.isDown("space") || keyboard.isDown("Space");
    if (space && !animUi.spaceWas) {
        if (animUi.clip.isPlaying()) animUi.clip.pause(); else animUi.clip.play();
        animUi.status = "Space toggled preview";
    }
    animUi.spaceWas = space;
    local control = keyboard.isDown("lctrl") || keyboard.isDown("rctrl") || keyboard.isDown("ctrl");
    local undo = control && (keyboard.isDown("z") || keyboard.isDown("Z"));
    local redo = control && (keyboard.isDown("y") || keyboard.isDown("Y"));
    if (undo && !animUi.undoWas) applyHistory("undo");
    if (redo && !animUi.redoWas) applyHistory("redo");
    animUi.undoWas = undo; animUi.redoWas = redo;
}

function updateLabels() {
    ui.select("animation.preview");
    ui.setText("preview-status", format("%s  %.3f / %.3f s  %s",
        animUi.clip.isPlaying() ? "PLAYING" : "PAUSED",
        animUi.clip.getPlayhead(), animUi.clip.getDuration(),
        animUi.clip.getSelectedBone()));
    ui.select("animation.inspector");
    ui.setText("selected-bone", "Bone " + animUi.clip.getSelectedBone());
    ui.setText("revision", "Revision " + animUi.clip.getRevision());
    ui.setValue("duration", animUi.clip.getDuration());
    ui.setValue("sample-rate", animUi.clip.getSampleRate());
    ui.setChecked("loop", animUi.clip.getLoop());
    ui.setValue("mask", animUi.clip.getSelectedMaskWeight());
    ui.setEnabled("undo", animUi.clip.canUndo());
    ui.setEnabled("redo", animUi.clip.canRedo());
    ui.select("animation.skeleton");
    ui.setText("bone-selection", "Active: " + animUi.clip.getSelectedBone());
    ui.select("animation.timeline");
    ui.setText("timeline-status", animUi.status);
    ui.setEnabled("undo", animUi.clip.canUndo());
    ui.setEnabled("redo", animUi.clip.canRedo());
}

function drawPose() {
    ui.select("animation.preview");
    local canvas = ui.viewportCanvas("anim-pose");
    if (canvas == null) return;
    gfx.setCanvas(canvas); gfx.clear();
    local width = animUi.workspace.getRegionW("center") - 20.0;
    local height = animUi.workspace.getRegionH("center") - 72.0;
    gfx.drawSolidRect(0.0, 0.0, width, height, 0.04, 0.05, 0.07, 1.0);
    local originX = width * 0.22;
    local originY = height * 0.72;
    local scale = 90.0;
    local count = animUi.clip.getPrimitiveCount();
    for (local i = 0; i < count; ++i) {
        local kind = animUi.clip.getPrimitiveKind(i);
        local x = originX + animUi.clip.getPrimitiveX(i) * scale;
        local y = originY - animUi.clip.getPrimitiveY(i) * scale;
        local r = animUi.clip.getPrimitiveR(i);
        local g = animUi.clip.getPrimitiveG(i);
        local b = animUi.clip.getPrimitiveB(i);
        if (kind == "bone-line") {
            local len = animUi.clip.getPrimitiveLength(i) * scale;
            local dx = animUi.clip.getPrimitiveDirX(i);
            local dy = -animUi.clip.getPrimitiveDirY(i);
            local steps = 12;
            for (local s = 0; s < steps; ++s) {
                local t = s.tofloat() / steps.tofloat();
                gfx.drawSolidRect(x + dx * len * t, y + dy * len * t, 3.0, 3.0, r, g, b, 1.0);
            }
        } else if (kind == "joint") {
            local radius = 7.0;
            gfx.drawSolidRect(x - radius, y - radius, radius * 2.0, radius * 2.0, r, g, b, 1.0);
        } else if (kind == "axis") {
            local len = animUi.clip.getPrimitiveLength(i) * scale;
            local dx = animUi.clip.getPrimitiveDirX(i);
            local dy = -animUi.clip.getPrimitiveDirY(i);
            gfx.drawSolidRect(x, y, dx * len, 2.0, r, g, b, 1.0);
        }
    }
    gfx.setCanvas(null);
}

function drawDopeSheet() {
    ui.select("animation.timeline");
    local canvas = ui.viewportCanvas("anim-dopesheet");
    if (canvas == null) return;
    gfx.setCanvas(canvas); gfx.clear();
    local width = animUi.clip.getLayoutWidth();
    local height = animUi.clip.getLayoutHeight();
    gfx.drawSolidRect(0.0, 0.0, width, height, 0.05, 0.06, 0.08, 1.0);
    local tracks = animUi.clip.getTrackCount();
    local rowH = tracks > 0 ? (height - 28.0) / tracks.tofloat() : 36.0;
    for (local i = 0; i < tracks; ++i) {
        local y = i.tofloat() * rowH;
        local shade = animUi.clip.getTrackSelected(i) ? 0.16 : 0.08;
        gfx.drawSolidRect(0.0, y, width, rowH, shade, shade + 0.02, shade + 0.04, 1.0);
        gfx.drawSolidRect(119.0, y, 1.0, rowH, 0.34, 0.37, 0.45, 1.0);
    }
    local keys = animUi.clip.getKeyCount();
    for (local i = 0; i < keys; ++i) {
        local selected = animUi.clip.getKeySelected(i);
        local r = selected ? 1.0 : 0.85;
        local g = selected ? 0.78 : 0.72;
        local b = selected ? 0.2 : 0.38;
        gfx.drawSolidRect(animUi.clip.getKeyX(i) - 3.0, animUi.clip.getKeyY(i) - 3.0, 7.0, 7.0, r, g, b, 1.0);
    }
    local events = animUi.clip.getEventCount();
    for (local i = 0; i < events; ++i)
        gfx.drawSolidRect(animUi.clip.getEventX(i), height - 18.0, 3.0, 14.0, 0.95, 0.7, 0.25, 1.0);
    gfx.drawSolidRect(animUi.clip.getPlayheadX(), 0.0, 2.0, height, 0.98, 0.28, 0.24, 1.0);
    gfx.setCanvas(null);
}

eve_init = function() {
    gfx.setBackgroundColor(0.09, 0.11, 0.14, 1.0);
    buildWorkspace(); ui.setTheme("dark"); ui.setNavKeyboard(true); mountPanels();
    animUi.clip.play();
    print("animation-clip-editor: duration=" + animUi.clip.getDuration() +
          "s tracks=" + animUi.clip.getTrackCount() + " primitives=" + animUi.clip.getPrimitiveCount() + "\n");
};

eve_update = function(dt) {
    handleUiEvents(); updateTimelinePointer(); updateKeyboardShortcuts();
    local advanced = animUi.clip.update(dt);
    if (!advanced.ok) animUi.status = advanced.status.summary;
    updateLabels();
};

eve_render = function() {
    gfx.clear();
    drawPose();
    drawDopeSheet();
    ui.beginFrameAndRender();
    animUi.frame += 1;
    if (!animUi.screenshotSaved && animUi.frame > 90 && gfx.saveFramePng("animation-clip-editor.png")) {
        animUi.screenshotSaved = true;
        print("animation-clip-editor: saved animation-clip-editor.png\n");
    }
};
