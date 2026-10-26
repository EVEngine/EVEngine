// Project-composed animation clip editor. Native code owns the document,
// pose preview, skeleton overlay, dope-sheet layout, transactions and undo/redo.

persist animUi = {
    workspace = null, clip = null, mouseDown = false,
    status = "Loading preview clip...",
    presenter = null,
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
    animUi.presenter = EditorWorkspacePresenter(ui, animUi.workspace, animUi.clip);
    animUi.presenter.layout(win, { left = 220.0, right = 340.0, bottom = 230.0 });
}

function panelSkeleton() {
    ui.text("Skeleton", "skeleton-title");
    for (local i = 0; i < animUi.clip.getBoneCount(); ++i) {
        local bone = animUi.clip.getBoneName(i);
        local parent = animUi.clip.getBoneParent(i);
        ui.listItem((parent == "" ? "" : "  ") + bone, "bone-" + bone);
    }
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

function panelTimeline() {
    ui.beginToolbar("transport");
    ui.iconButton("play", "", "play-pause"); ui.iconButton("stop", "", "stop");
    ui.iconButton("plus", "Set Key", "set-key"); ui.iconButton("minus", "", "delete-key");
    ui.iconButton("undo", "", "undo"); ui.iconButton("redo", "", "redo"); ui.end();
    ui.text("", "timeline-status");
    ui.viewport("anim-dopesheet", animUi.workspace.getRegionW("bottom") - 20.0,
                animUi.workspace.getRegionH("bottom") - 88.0);
}

panelBuilders <- {
    ["animation.skeleton"]=panelSkeleton,
    ["animation.preview"]=panelPreview,
    ["animation.timeline"]=panelTimeline
};

function mountPanels() {
    animUi.presenter.status = animUi.status;
    animUi.presenter.statusPanel = "animation.timeline";
    animUi.presenter.statusWidget = "timeline-status";
    animUi.presenter.mount(panelBuilders);

    animUi.presenter.actions["set-key"] <- true;
    animUi.presenter.actions["delete-key"] <- true;
    animUi.presenter.onClick = function(host, id) {
        if (host == "animation.skeleton" && id.find("bone-") == 0)
            animUi.presenter.checked(animUi.clip.selectBone(id.slice(5)));
    };
    requireResult(animUi.clip.setViewport(animUi.workspace.getRegionW("bottom") - 20.0, 36.0, 120.0), "Configure dope-sheet viewport");
}

function updateTimelinePointer() {
    ui.select("animation.timeline");
    local hovered = ui.viewportHovered("anim-dopesheet");
    local down = hovered && mouse.isDown(1);
    local x = ui.viewportMouseX("anim-dopesheet");
    local y = ui.viewportMouseY("anim-dopesheet");
    if (down && !animUi.mouseDown) {
        local hit = animUi.clip.pointerDown(x, y);
        animUi.presenter.status = hit.ok ? "Timeline " + animUi.clip.getSelectedBone() : hit.status.summary;
    }
    animUi.mouseDown = down;
}

function updateLabels() {
    ui.select("animation.preview");
    ui.setText("preview-status", format("%s  %.3f / %.3f s  %s",
        animUi.clip.isPlaying() ? "PLAYING" : "PAUSED",
        animUi.clip.getPlayhead(), animUi.clip.getDuration(), animUi.clip.getSelectedBone()));
    ui.select("animation.skeleton");
    ui.setText("bone-selection", "Active: " + animUi.clip.getSelectedBone());
    ui.select("animation.timeline");
    ui.setEnabled("delete-key", animUi.clip.hasSelectedKey());
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
    buildWorkspace(); ui.setTheme("dark"); ui.setScale(0.75);
    ui.setNavKeyboard(true); mountPanels();
    animUi.clip.play();
    print("animation-clip-editor: duration=" + animUi.clip.getDuration() +
          "s tracks=" + animUi.clip.getTrackCount() + " primitives=" + animUi.clip.getPrimitiveCount() + "\n");
};

eve_update = function(dt) {
    animUi.presenter.update(keyboard, mouse.isDown(1), win.hasFocus()); updateTimelinePointer();
    local advanced = animUi.clip.update(dt);
    if (!advanced.ok) animUi.presenter.checked(advanced);
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

eve_quit = function() { if (animUi.presenter != null) animUi.presenter.close(); };
