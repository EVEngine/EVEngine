// Project-composed audio source editor. Native code owns the document,
// waveform, audition transport, transactions and undo/redo.

persist audioUi = {
    workspace = null, source = null, mouseDown = false,
    status = "Loading preview tone...",
    presenter = null,
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
    audioUi.status = "Silent clock preview · audio output disabled";
    audioUi.presenter = EditorWorkspacePresenter(ui, audioUi.workspace, audioUi.source);
    audioUi.presenter.layout(win, { left = 220.0, right = 280.0, bottom = 120.0 });
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
    ["audio.transport"]=panelTransport
};

function mountPanels() {
    audioUi.presenter.status = audioUi.status;
    audioUi.presenter.statusPanel = "audio.transport";
    audioUi.presenter.statusWidget = "transport-status";
    audioUi.presenter.mount(panelBuilders);

    audioUi.presenter.onClick = function(host, id) {
        if (host == "audio.sources" && id == "tone-clip") {
            audioUi.presenter.checked(audioUi.source.seekSeconds(0.0));
            audioUi.presenter.action("play-pause");
        }
    };
    requireResult(audioUi.source.setViewportWidth(audioUi.workspace.getRegionW("center") - 20.0), "Configure waveform viewport");
}

function updateWaveformPointer() {
    ui.select("audio.waveform");
    local hovered = ui.viewportHovered("audio-wave");
    local down = hovered && mouse.isDown(1);
    local x = ui.viewportMouseX("audio-wave");
    if (down && !audioUi.mouseDown) {
        local sought = audioUi.source.seekX(x);
        audioUi.presenter.status = sought.ok ? "Playhead from waveform" : sought.status.summary;
    }
    audioUi.mouseDown = down;
}

function updateLabels() {
    ui.select("audio.waveform");
    ui.setText("wave-status", format("%s  %.3f / %.3f s",
        audioUi.source.isPlaying() ? "PLAYING" : "PAUSED",
        audioUi.source.getPlayhead(), audioUi.source.getDuration()));
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
    buildWorkspace(); ui.setTheme("dark"); ui.setScale(0.75); ui.setNavKeyboard(true); mountPanels();
    requireResult(audioUi.source.stop(), "Start silent preview");
    print("audio-source-editor: duration=" + audioUi.source.getDuration() +
          "s buckets=" + audioUi.source.getBucketCount() + "\n");
};

eve_update = function(dt) {
    audioUi.presenter.update(keyboard, mouse.isDown(1), win.hasFocus()); updateWaveformPointer();
    local advanced = audioUi.source.update(dt);
    if (!advanced.ok) audioUi.presenter.checked(advanced);
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

eve_quit = function() { if (audioUi.presenter != null) audioUi.presenter.close(); };
