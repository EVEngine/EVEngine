// Project-composed audio source editor. Native code owns the document,
// waveform, audition transport, transactions and undo/redo.

persist audioUi = {
    workspace = null, source = null, mouseDown = false,
    status = "Loading preview tone...",
    spaceWas = false, undoWas = false, redoWas = false,
    frame = 0, screenshotSaved = false,
};

function requireResult(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result.value;
}

function buildWorkspace() {
    audioUi.workspace = editor.newWorkspace("preview.audio", "Preview Tone Editor");
    audioUi.source = requireResult(eve.AudioEditorModule().create("asset.preview.tone"),
                                   "Create audio source editor");
    requireResult(audioUi.source.configureWorkspace(audioUi.workspace), "Compose workspace");
    local live = audioUi.source.attachLiveAudition();
    audioUi.status = live.ok ? "Live audition attached" : "Clock audition (no live audio device)";
    audioUi.workspace.setRegionSize("left", 220.0);
    audioUi.workspace.setRegionSize("right", 280.0);
    audioUi.workspace.setRegionSize("bottom", 120.0);
    audioUi.workspace.layout(config.width.tofloat(), config.height.tofloat());
}

function panelSources() {
    ui.text("Sources", "sources-title");
    ui.listItem("Preview sine (440+660)", "tone-clip");
    ui.text("", "source-selection");
    ui.separator("sources-sep");
    ui.textWrapped("Native PCM + waveform envelopes. Inspector edits go through audio.source.property.set.v1.",
                   190.0, "sources-help");
}

function panelWaveform() {
    ui.text("Click waveform to seek · loop markers in gold", "wave-help");
    ui.text("", "wave-status");
    ui.viewport("audio-wave", audioUi.workspace.getRegionW("center") - 20.0,
                audioUi.workspace.getRegionH("center") - 72.0);
}

function panelInspector() {
    ui.text("Source Inspector", "inspector-title");
    ui.text("", "clip-uri");
    ui.text("", "revision");
    ui.separator("inspector-sep-1");
    ui.slider("Volume", audioUi.source.getFloat("play.volume"), 0.0, 2.0, "volume");
    ui.slider("Pitch", audioUi.source.getFloat("play.pitch"), 0.25, 2.0, "pitch");
    ui.slider("Loop start", audioUi.source.getFloat("play.loop-start"), 0.0,
              audioUi.source.getDuration(), "loop-start");
    ui.slider("Loop end", audioUi.source.getFloat("play.loop-end"), 0.0,
              audioUi.source.getDuration(), "loop-end");
    ui.beginRow("inspector-history", 8.0); ui.button("Undo", "undo"); ui.button("Redo", "redo"); ui.end();
    ui.textWrapped("Volume and pitch publish to the live Source without unbinding audition.",
                   245.0, "inspector-help");
}

function panelTransport() {
    ui.beginRow("transport", 8.0);
    ui.button("Play / Pause", "play-pause");
    ui.button("Stop", "stop");
    ui.button("Undo", "undo");
    ui.button("Redo", "redo");
    ui.end();
    ui.text("", "transport-status");
}

panelBuilders <- {
    ["audio.sources"]=panelSources,
    ["audio.waveform"]=panelWaveform,
    ["audio.inspector"]=panelInspector,
    ["audio.transport"]=panelTransport
};

function mountPanels() {
    for (local i = 0; i < audioUi.workspace.getPanelCount(); ++i) {
        local id = audioUi.workspace.getPanelId(i);
        if (!(id in panelBuilders)) continue;
        ui.beginBuild(); ui.beginWindow(audioUi.workspace.getPanelTitle(i), "root");
        panelBuilders[id](); ui.end(); ui.mountBuildAs(id); ui.select(id);
        local region = audioUi.workspace.getPanelRegion(i);
        ui.setHostPos(audioUi.workspace.getRegionX(region), audioUi.workspace.getRegionY(region), 0.0, 0.0);
        ui.setHostSize(audioUi.workspace.getRegionW(region), audioUi.workspace.getRegionH(region));
        ui.setHostOverlay(false);
    }
    requireResult(audioUi.source.setViewportWidth(audioUi.workspace.getRegionW("center") - 20.0),
                  "Configure waveform viewport");
}

function applyHistory(command) {
    local result = command == "undo" ? audioUi.source.undo() : audioUi.source.redo();
    audioUi.status = result.ok ? command + " applied" : command + ": " + result.status.summary;
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
            if (audioUi.source.isPlaying()) {
                audioUi.source.pause(); audioUi.status = "Audition paused";
            } else {
                local played = audioUi.source.play();
                audioUi.status = played.ok ? "Audition playing" : played.status.summary;
            }
        } else if (id == "stop") {
            audioUi.source.stop(); audioUi.status = "Audition stopped";
        } else if (id == "undo" || id == "redo") {
            applyHistory(id);
        } else if (host == "audio.sources" && id == "tone-clip") {
            requireResult(audioUi.source.seekSeconds(0.0), "Select preview clip");
            audioUi.source.play();
            audioUi.status = "Selected preview sine";
        }
        click = ui.consumeClick();
    }

    local changed = ui.consumeChange();
    while (changed != "") {
        local event = eventParts(changed); local host = event[0]; local id = event[1];
        if (host == "audio.inspector") {
            ui.select("audio.inspector");
            local result = { ok = true, status = { summary = "" } };
            if (id == "volume") result = audioUi.source.setFloat("play.volume", ui.getValue("volume"));
            else if (id == "pitch") result = audioUi.source.setFloat("play.pitch", ui.getValue("pitch"));
            else if (id == "loop-start") result = audioUi.source.setFloat("play.loop-start", ui.getValue("loop-start"));
            else if (id == "loop-end") result = audioUi.source.setFloat("play.loop-end", ui.getValue("loop-end"));
            audioUi.status = result.ok ? id + " committed · revision " + audioUi.source.getRevision()
                                       : result.status.summary;
        }
        changed = ui.consumeChange();
    }
}

function updateWaveformPointer() {
    ui.select("audio.waveform");
    local hovered = ui.viewportHovered("audio-wave");
    local down = hovered && mouse.isDown(1);
    local x = ui.viewportMouseX("audio-wave");
    if (down && !audioUi.mouseDown) {
        local sought = audioUi.source.seekX(x);
        audioUi.status = sought.ok ? "Playhead from waveform" : sought.status.summary;
    }
    audioUi.mouseDown = down;
}

function updateKeyboardShortcuts() {
    local space = keyboard.isDown("space") || keyboard.isDown("Space");
    if (space && !audioUi.spaceWas) {
        if (audioUi.source.isPlaying()) audioUi.source.pause(); else audioUi.source.play();
        audioUi.status = "Space toggled audition";
    }
    audioUi.spaceWas = space;
    local control = keyboard.isDown("lctrl") || keyboard.isDown("rctrl") || keyboard.isDown("ctrl");
    local undo = control && (keyboard.isDown("z") || keyboard.isDown("Z"));
    local redo = control && (keyboard.isDown("y") || keyboard.isDown("Y"));
    if (undo && !audioUi.undoWas) applyHistory("undo");
    if (redo && !audioUi.redoWas) applyHistory("redo");
    audioUi.undoWas = undo; audioUi.redoWas = redo;
}

function updateLabels() {
    ui.select("audio.waveform");
    ui.setText("wave-status", format("%s  %.3f / %.3f s",
        audioUi.source.isPlaying() ? "PLAYING" : "PAUSED",
        audioUi.source.getPlayhead(), audioUi.source.getDuration()));
    ui.select("audio.inspector");
    ui.setText("clip-uri", audioUi.source.getString("clip.asset"));
    ui.setText("revision", "Revision " + audioUi.source.getRevision());
    ui.setValue("volume", audioUi.source.getFloat("play.volume"));
    ui.setValue("pitch", audioUi.source.getFloat("play.pitch"));
    ui.setValue("loop-start", audioUi.source.getFloat("play.loop-start"));
    ui.setValue("loop-end", audioUi.source.getFloat("play.loop-end"));
    ui.setEnabled("undo", audioUi.source.canUndo());
    ui.setEnabled("redo", audioUi.source.canRedo());
    ui.select("audio.sources");
    ui.setText("source-selection", "Active: Preview sine");
    ui.select("audio.transport");
    ui.setText("transport-status", audioUi.status);
    ui.setEnabled("undo", audioUi.source.canUndo());
    ui.setEnabled("redo", audioUi.source.canRedo());
}

function drawWaveform() {
    ui.select("audio.waveform");
    local canvas = ui.viewportCanvas("audio-wave");
    if (canvas == null) return;
    gfx.setCanvas(canvas); gfx.clear();
    local width = audioUi.source.getLayoutWidth();
    local height = audioUi.workspace.getRegionH("center") - 72.0;
    gfx.drawSolidRect(0.0, 0.0, width, height, 0.04, 0.05, 0.07, 1.0);
    local mid = height * 0.5;
    local count = audioUi.source.getBucketCount();
    if (count > 0) {
        local bucketW = width / count.tofloat();
        for (local i = 0; i < count; ++i) {
            local lo = audioUi.source.getBucketMin(i);
            local hi = audioUi.source.getBucketMax(i);
            local y0 = mid - hi * mid;
            local y1 = mid - lo * mid;
            gfx.drawSolidRect(i.tofloat() * bucketW, y0, bucketW, y1 - y0 + 1.0, 0.35, 0.72, 0.62, 0.95);
        }
    }
    gfx.drawSolidRect(audioUi.source.getLoopStartX(), 0.0, 2.0, height, 1.0, 0.82, 0.32, 1.0);
    gfx.drawSolidRect(audioUi.source.getLoopEndX(), 0.0, 2.0, height, 1.0, 0.82, 0.32, 1.0);
    gfx.drawSolidRect(audioUi.source.getPlayheadX(), 0.0, 2.0, height, 0.98, 0.28, 0.24, 1.0);
    gfx.setCanvas(null);
}

eve_init = function() {
    gfx.setBackgroundColor(0.09, 0.11, 0.14, 1.0);
    buildWorkspace(); ui.setTheme("dark"); ui.setNavKeyboard(true); mountPanels();
    audioUi.source.play();
    print("audio-source-editor: duration=" + audioUi.source.getDuration() +
          "s buckets=" + audioUi.source.getBucketCount() + "\n");
};

eve_update = function(dt) {
    handleUiEvents(); updateWaveformPointer(); updateKeyboardShortcuts();
    local advanced = audioUi.source.update(dt);
    if (!advanced.ok) audioUi.status = advanced.status.summary;
    updateLabels();
};

eve_render = function() {
    gfx.clear();
    drawWaveform();
    ui.beginFrameAndRender();
    audioUi.frame += 1;
    if (!audioUi.screenshotSaved && audioUi.frame > 90 && gfx.saveFramePng("audio-source-editor.png")) {
        audioUi.screenshotSaved = true;
        print("audio-source-editor: saved audio-source-editor.png\n");
    }
};
