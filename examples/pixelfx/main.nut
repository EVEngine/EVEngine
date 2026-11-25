import { make_effect, custom_actor } from "./variants.nut";

// Two persistent image/texture surfaces; only the selected pair owns effect programs.
local CW = 640, CH = 400, TICK = 1.0 / 60.0;
local imageOwner = null, fontOwner = null, fontData = null, font = null, styleOwner = null;
local session = null, ids = [], matches = [], metadata = {}, entries = [], surfaces = [];
local selected = "", matchIndex = 0, query = "", searchEditing = false;
local paused = false, rate = 1.0, targets = true, flip = false, bodyChoice = 0;
local buffers = 0, backdrop = 0, paramIndex = 0, pointAxis = 0;
local viewAngle = 0.0, viewCos = 1.0, viewSin = 0.0;
local editingText = false, editText = "", status = "", cropW = 320, cropH = 240, originX = 160, originY = 200;

function need(result) {
    if (!result.ok) throw "PixelFX operation rejected: " + result.code;
    return result.value;
}
function rejection(result) {
    status = result.diagnostics.len() > 0 ? result.diagnostics[0].message : "Value rejected; previous preview kept.";
    print("PixelFX parameter rejected / " + status + "\n");
}
function text(value, x, y, scale = 0.8, r = 0.85, g = 0.90, b = 0.83) {
    gfx.drawText(font, value, x.tofloat(), y.tofloat(), r, g, b, 1.0, scale);
}
function rectangle(x, y, w, h, r, g, b) {
    gfx.drawSolidRect(x.tofloat(), y.tofloat(), w.tofloat(), h.tofloat(), r, g, b, 1.0);
}
function f2(value) { return format("%.2f", value.tofloat()); }
function playback(effect) {
    local candidate = need(session.prepare(effect));
    local act = need(candidate.publish());
    return {fx = effect, act = act, timing = need(act.getTiming()), box = need(need(act.prepareBounds()).getValue()),
        config = need(effect.getConfiguration()), parameters = need(effect.getParameters()).parameters,
        ticks = 0, accumulator = 0.0, stopTick = null, repeat = true, doneTicks = 0, dirty = true, snapshot = need(act.getSnapshot())};
}
function layout() {
    local x0 = entries[0].box.x0, x1 = entries[0].box.x1, y0 = entries[0].box.y0, y1 = entries[0].box.y1;
    foreach (entry in entries) {
        if (entry.box.x0 < x0) x0 = entry.box.x0; if (entry.box.x1 > x1) x1 = entry.box.x1;
        if (entry.box.y0 < y0) y0 = entry.box.y0; if (entry.box.y1 > y1) y1 = entry.box.y1;
    }
    cropW = (x1 - x0 + 24).tointeger(); cropH = (y1 - y0 + 24).tointeger();
    if (cropW > CW || cropH > CH) throw "PixelFX source bounds exceed the gallery surface";
    originX = flip ? 12 - (-x1) : 12 - x0; originY = 12 - y0;
    fit_views();
    foreach (entry in entries) entry.dirty = true;
}
function fit_views() {
    local radians = viewAngle * 0.017453292519943295;
    viewCos = cos(radians); viewSin = sin(radians);
    local c = viewCos < 0 ? -viewCos : viewCos, n = viewSin < 0 ? -viewSin : viewSin;
    local width = c * cropW + n * cropH, height = n * cropW + c * cropH;
    local scale = 400.0 / width; if (340.0 / height < scale) scale = 340.0 / height; if (scale > 4.0) scale = 4.0;
    foreach (index, surface in surfaces) {
        surface.scale = scale;
        surface.baseX = 248.0 + index * 432.0 + (416.0 - cropW * scale) / 2.0;
        surface.baseY = 164.0 + (360.0 - cropH * scale) / 2.0;
        surface.quad.setViewport(0, 0, cropW, cropH);
        foreach (sprite in surface.sprites) {
            sprite.setSize(cropW.tofloat(), cropH.tofloat()); sprite.setScale(scale, scale); sprite.setRotation(viewAngle);
        }
    }
}
function rotate_view(delta) {
    viewAngle += delta;
    if (viewAngle > 180) viewAngle -= 360;
    if (viewAngle <= -180) viewAngle += 360;
    fit_views();
}

function select_effect(index) {
    if (matches.len() == 0) {status = "No matching effects. Edit the filter with Tab."; return;}
    index = (index % matches.len() + matches.len()) % matches.len();
    local id = matches[index];
    local next = [playback(make_effect(id, false)), playback(make_effect(id, true))];
    entries = next; selected = id; matchIndex = index; paramIndex = 0; pointAxis = 0;
    layout(); status = "";
    print("PixelFX Act selected: " + selected + " / active programs=2 / catalogue=" + ids.len() + "\n");
}
function filter_effects() {
    matches.clear(); foreach (id in ids) if (query.len() == 0 || id.find(query) != null) matches.push(id);
    if (matches.len() > 0) select_effect(0); else status = "No matching effects.";
}
function restart_entry(entry) {
    local next = playback(entry.fx); return next;
}
function tick_entry(entry) {
    local r = entry.act.step(TICK); if (!r.ok) throw "PixelFX Act update failed";
    entry.ticks++; if (entry.stopTick != null && entry.stopTick == entry.ticks) entry.act.stop();
    entry.snapshot = need(entry.act.getSnapshot()); entry.dirty = true;
}
function seek_entry(entry, tick) {
    if (tick < 0) tick = 0;
    if (tick > 7200) {status = "Frame replay is bounded to 120 seconds."; return;}
    local history = [];
    if (entry.stopTick == 0) history.push({op = "stop"});
    for (local i = 0; i < tick; ++i) {
        history.push({op = "step", delta = TICK});
        if (entry.stopTick != null && i + 1 == entry.stopTick) history.push({op = "stop"});
    }
    local prepared = entry.act.prepareReplay(history);
    if (!prepared.ok) {rejection(prepared); return;}
    local candidate = need(prepared.value.publish());
    entry.act = candidate; entry.ticks = tick; entry.accumulator = 0.0; entry.doneTicks = 0;
    entry.snapshot = need(candidate.getSnapshot()); entry.dirty = true;
}
function step_frames(count) {
    paused = true;
    foreach (entry in entries) {
        if (count < 0) seek_entry(entry, entry.ticks + count);
        else for (local i = 0; i < count; ++i) tick_entry(entry);
    }
    print("PixelFX Act frame: " + selected + " / tick=" + entries[0].ticks + "\n");
}
function parameter() {
    local p = entries[1].parameters; if (paramIndex >= p.len()) paramIndex = 0; return p[paramIndex];
}
function apply_value(value) {
    local p = parameter(), config = need(entries[1].fx.getConfiguration()); config.options[p.name] = value;
    local fx = eve.PixelFx(selected), prepared = fx.prepareConfiguration(config);
    if (!prepared.ok) {rejection(prepared); return;}
    local published = fx.commitConfiguration(prepared.value); if (!published.ok) {rejection(published); return;}
    local next = playback(fx); entries[1] = next; layout(); status = "Updated " + p.name + "; right preview restarted.";
    print("PixelFX Act parameter: " + selected + " / " + p.name + "\n");
}
function adjust_parameter(direction) {
    local p = parameter(), value = entries[1].config.options[p.name];
    if (p.type == "boolean") apply_value(!value);
    else if (p.type == "number" || p.type == "integer") {
        local step = p.type == "integer" ? 1 : (p.maximum - p.minimum) / 100.0;
        if (step <= 0.0) return;
        value += direction * step; if (value < p.minimum) value = p.minimum; if (value > p.maximum) value = p.maximum;
        if ("exclusiveMinimum" in p && p.exclusiveMinimum && value <= p.minimum) value = p.minimum + step;
        apply_value(p.type == "integer" ? value.tointeger() : value);
    } else if (p.type == "color") apply_value((value & 0xffffff00) | (((value & 255) + direction * 16) & 255));
    else if (p.type == "palette") {
        if (value.len() > 1) {if (direction > 0) value.push(value.remove(0)); else value.insert(0, value.pop());}
        else if (value.len() == 1) value[0] = (value[0] & 0xffffff00) | (((value[0] & 255) + direction * 16) & 255);
        apply_value(value);
    } else if (p.type == "point2") {
        value[pointAxis] += direction; if (value[pointAxis] < p.minimum) value[pointAxis] = p.minimum;
        if (value[pointAxis] > p.maximum) value[pointAxis] = p.maximum; apply_value(value);
    } else if (p.type == "actor") {
        local kinds = ["dummy", "hero", "custom", "none"], index = kinds.find(value.source);
        if (index == null) index = 0; index = (index + direction + 4) % 4;
        apply_value(kinds[index] == "custom" ? custom_actor() : {schema = "eve.pixelfx.actor", version = 1, source = kinds[index]});
    } else if (p.type == "string") {editingText = true; editText = value; status = "Edit text, then press Enter.";}
}
function value_text() {
    local p = parameter(), v = entries[1].config.options[p.name];
    if (p.type == "actor") return v.source;
    if (p.type == "palette") return v.len() + " colors";
    if (p.type == "point2") return "[" + f2(v[0]) + ", " + f2(v[1]) + "] axis " + pointAxis;
    if (p.type == "number") return f2(v);
    if (p.type == "boolean") return v ? "on" : "off";
    return v.tostring();
}
function listen_keys() {
    asyncOnEvent("keypressed").then(function(raw) {
        local key = raw.tolower(); if (key == "slash") key = "/";
        if (editingText) {
            if (key == "escape") editingText = false;
            else if (key == "return" || key == "enter") {editingText = false; apply_value(editText);}
            else if (key == "backspace" && editText.len() > 0) editText = editText.slice(0, editText.len() - 1);
            else if (key == "space") editText += " ";
            else if (key.len() == 1 && editText.len() < 128) editText += key;
        } else if (key == "tab" || (!searchEditing && key == "/")) {searchEditing = !searchEditing; if (searchEditing) {query = ""; filter_effects();}}
        else if (searchEditing) {
            if (key == "return" || key == "enter" || key == "escape") searchEditing = false;
            else if (key == "up") select_effect(matchIndex - 1);
            else if (key == "down") select_effect(matchIndex + 1);
            else if (key == "backspace" && query.len() > 0) {query = query.slice(0, query.len() - 1); filter_effects();}
            else if (key.len() == 1 && "abcdefghijklmnopqrstuvwxyz0123456789-/_".find(key) != null && query.len() < 64) {query += key; filter_effects();}
        } else if (key == "up") select_effect(matchIndex - 1);
        else if (key == "down") select_effect(matchIndex + 1);
        else if (key == "space") paused = !paused;
        else if (key == "r") {for (local i = 0; i < entries.len(); ++i) entries[i] = restart_entry(entries[i]); layout();}
        else if (key == "s") foreach (entry in entries) {if (entry.stopTick == null || entry.ticks < entry.stopTick) entry.stopTick = entry.ticks; entry.act.stop(); entry.repeat = false; entry.dirty = true;}
        else if (key == "," || key == "comma") step_frames(-1);
        else if (key == "." || key == "period") step_frames(1);
        else if (key == "pageup") step_frames(-30);
        else if (key == "pagedown") step_frames(30);
        else if (key == "b") {bodyChoice = (bodyChoice + 1) % 4; foreach (e in entries) e.dirty = true;}
        else if (key == "t") {targets = !targets; foreach (e in entries) e.dirty = true;}
        else if (key == "f") {flip = !flip; layout();}
        else if (key == "q") rotate_view(-15.0);
        else if (key == "e") rotate_view(15.0);
        else if (key == "z") {viewAngle = 0.0; fit_views();}
        else if (key == "v") buffers = (buffers + 1) % 3;
        else if (key == "g") backdrop = (backdrop + 1) % 3;
        else if (key == "[" || key == "leftbracket") paramIndex = (paramIndex - 1 + entries[1].parameters.len()) % entries[1].parameters.len();
        else if (key == "]" || key == "rightbracket") paramIndex = (paramIndex + 1) % entries[1].parameters.len();
        else if (key == "+" || key == "=" || key == "equals") adjust_parameter(1);
        else if (key == "-" || key == "minus") adjust_parameter(-1);
        else if (key == "p") pointAxis = 1 - pointAxis;
        else if (key == "return" || key == "enter") {if (parameter().type == "string") {editingText = true; editText = entries[1].config.options[parameter().name];}}
        else if (key == "1") rate = 0.25;
        else if (key == "2") rate = 0.5;
        else if (key == "3") rate = 1.0;
        else if (key == "4") rate = 2.0;
        listen_keys();
    });
}
function surface(index) {
    local c = imageOwner.newEmptyImageData(CW, CH, "RGBA8"), l = imageOwner.newEmptyImageData(CW, CH, "RGBA8");
    local bgc = imageOwner.newEmptyImageData(CW, CH, "RGBA8"), bgl = imageOwner.newEmptyImageData(CW, CH, "RGBA8");
    local textures = [gfx.newTexture(c, false, false), gfx.newTexture(l, false, false)], sprites = [], quad = gfx.newQuad(0, 0, CW, CH);
    foreach (layer, tex in textures) {
        gfx.setTextureSampler(tex, "nearest", "none", 1.0, 0.0);
        local sprite = gfx.newSprite2D(); sprite.setTexture(tex); sprite.setQuad(quad); sprite.setAnchor(0.5, 0.5);
        sprite.setReceiveLight(false); sprite.setLayer(layer); sprite.setBlend(layer == 0 ? "alpha" : "additive"); sprites.push(sprite);
    }
    return {color = c, light = l, bgColor = bgc, bgLight = bgl, textures = textures, sprites = sprites, quad = quad,
        baseX = 0.0, baseY = 0.0, scale = 1.0};
}
eve_init = function() {
    styleOwner = eve.Stylize(); imageOwner = eve.Image(); fontOwner = eve.Font();
    fontData = fontOwner.newFontDataFromFile("assets/DejaVuSans-Bold.ttf", 20);
    font = gfx.newFont(fontData, " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.,;-/_:[]()=+|!?<>");
    session = eve.PixelFxActSession(); local catalogue = need(session.getCatalogue());
    foreach (info in catalogue.effects) {ids.push(info.id); metadata[info.id] <- info;}
    ids.sort(); matches = clone ids; surfaces = [surface(0), surface(1)];
    local first = matches.find("moves/arrow-shot"); select_effect(first == null ? 0 : first);
    gfx.setBackgroundColor(0.09, 0.16, 0.23, 1.0); listen_keys();
    print("PixelFX Act gallery ready / catalogue=" + ids.len() + " / active programs=2 / fixed surfaces=2\n");
};
eve_update = function(dt) {
    foreach (index, entry in entries) {
        if (!paused) {
            entry.accumulator += dt * rate * entry.config.options.playbackSpeed;
            while (entry.accumulator >= TICK) {
                tick_entry(entry); entry.accumulator -= TICK;
                if (entry.snapshot.done && entry.repeat && !metadata[ selected ].loop) {
                    entry.doneTicks++; if (entry.doneTicks > 48) {local remaining = entry.accumulator; entries[index] = restart_entry(entry); entry = entries[index]; entry.accumulator = remaining;}
                }
            }
        }
        local s = surfaces[index];
        if (entry.dirty) {
            local actor = "actor" in entry.config.options ? entry.config.options.actor : null;
            local mode = bodyChoice == 3 ? "none" : bodyChoice == 1 ? "source" : bodyChoice == 2 ? "actor" :
                actor != null && actor.source == "custom" ? "actor" : entry.snapshot.hasAct ? "source" : actor == null || actor.source == "none" ? "none" : "actor";
            local frame = need(entry.act.prepareFrame(s.bgColor, s.bgLight, {width = cropW, height = cropH,
                x = originX.tofloat(), y = originY.tofloat(), flip = flip, targets = targets, body = mode}));
            local copied = frame.copyTo(s.color, s.light); if (!copied.ok) throw "PixelFX frame presentation failed";
            foreach (layer, tex in s.textures) gfx.updateTextureFromImageData(tex, layer == 0 ? s.color : s.light);
            entry.dirty = false;
        }
        foreach (layer, sprite in s.sprites) {
            sprite.setVisible(buffers == 0 || buffers == layer + 1);
            sprite.setPosition(s.baseX + (entry.snapshot.shake[0] * viewCos - entry.snapshot.shake[1] * viewSin) * s.scale,
                s.baseY + (entry.snapshot.shake[0] * viewSin + entry.snapshot.shake[1] * viewCos) * s.scale);
        }
    }
};
function timeline(entry, x, y) {
    local info = metadata[selected], windup = entry.timing == null ? 0.0 : entry.timing.windup;
    local recovery = entry.timing == null ? 0.0 : entry.timing.recovery, total = windup + info.duration + recovery;
    if (total <= 0) total = 1.0;
    rectangle(x, y, 400, 7, 0.12, 0.23, 0.30);
    if (windup > 0) rectangle(x, y, 400.0 * windup / total, 7, 0.37, 0.49, 0.57);
    if (entry.timing != null) foreach (h in entry.timing.hits) rectangle(x + 400.0 * (windup + h.t) / total, y - 3, 2, 13, 0.91, 0.50, 0.48);
    local phase = entry.snapshot.actTime; if (info.loop && phase >= info.duration) phase = phase % info.duration;
    local fraction = (phase + windup) / total; if (fraction < 0) fraction = 0; if (fraction > 1) fraction = 1;
    rectangle(x + 400.0 * fraction, y - 4, 3, 15, 1.0, 0.73, 0.53);
    text("ACT " + f2(entry.snapshot.actTime) + "  FX " + f2(entry.snapshot.effectTime) +
        (entry.snapshot.frozen ? "  HIT STOP" : entry.snapshot.done ? "  COMPLETE" : ""), x, y + 18, 0.68);
}
eve_render = function() {
    gfx.clear(); rectangle(0, 0, 1120, 90, 0.07, 0.12, 0.19);
    rectangle(12, 106, 220, 490, 0.07, 0.13, 0.20);
    foreach (i, s in surfaces) {
        local x = 240 + i * 432; rectangle(x, 146, 432, 396, 0.12, 0.23, 0.30);
        if (backdrop == 1) rectangle(x + 8, 154, 416, 380, 0.80, 0.84, 0.75);
        else if (backdrop == 2) {rectangle(x + 8, 154, 416, 380, 0.19, 0.35, 0.39);rectangle(x + 8, 438, 416, 96, 0.10, 0.23, 0.27);}
        else for (local row = 0; row < 19; ++row) for (local col = 0; col < 21; ++col)
            if ((row + col) % 2 == 0) rectangle(x + 8 + col * 19.8, 154 + row * 20, 19.8, 20, 0.14, 0.26, 0.33);
    }
    gfx.renderSprites();
    text("PIXELFX / ACT WORKBENCH", 20, 17, 1.05, 0.85, 0.91, 0.82);
    text("244 PROCEDURAL EFFECTS    " + (paused ? "PAUSED" : "PLAYING") + "    " + f2(rate) + "x", 20, 53, 0.7, 0.42, 0.78, 0.83);
    text("BODY " + ["AUTO", "RIG", "SPRITE", "FX ONLY"][bodyChoice] + "   TARGETS " + (targets ? "ON" : "OFF") +
        "   " + ["COMPOSITE", "COLOR", "LIGHT"][buffers] + (flip ? "   MIRRORED" : "") + "   VIEW " + viewAngle.tointeger(), 570, 53, 0.62, 0.42, 0.78, 0.83);
    text(selected, 248, 108, 0.9, 1.0, 0.73, 0.53); text("DEFAULT", 248, 132, 0.6); text("PARAMETER VARIANT", 680, 132, 0.6);
    text("FILTER " + query + (searchEditing ? "_" : ""), 22, 119, 0.65, 0.42, 0.78, 0.83);
    text((matches.len() > 0 ? matchIndex + 1 : 0) + " / " + matches.len(), 22, 147, 0.7);
    local first = matchIndex - 7; if (first < 0) first = 0;
    for (local n = first; n < matches.len() && n < first + 15; ++n) {
        local y = 180 + (n - first) * 26;
        if (n == matchIndex) rectangle(18, y - 3, 208, 24, 0.23, 0.37, 0.43);
        local label = matches[n]; if (label.len() > 27) label = label.slice(0, 26) + ">";
        text(label, 24, y, 0.58, n == matchIndex ? 1.0 : 0.68, n == matchIndex ? 0.73 : 0.80, n == matchIndex ? 0.53 : 0.81);
    }
    if (entries.len() == 2) {
        timeline(entries[0], 248, 553); timeline(entries[1], 680, 553);
        local label = editingText ? "TEXT " + editText + "_" : "PARAMETER " + parameter().name + " = " + value_text();
        if (label.len() > 100) label = label.slice(0, 100); text(label, 20, 615, 0.76, 1.0, 0.73, 0.53);
    }
    if (status.len() > 0) text(status.slice(0, status.len() > 120 ? 120 : status.len()), 20, 593, 0.58, 0.91, 0.50, 0.48);
    text("Tab filter | Up/Down select | Space pause | R restart | S fade | , . frame | PgUp/PgDn 30 frames | 1-4 speed | Q/E rotate | Z reset", 20, 651, 0.62);
    text("B body | T targets | F mirror | V color/light | G background | [ ] parameter | +/- adjust | P point axis | Enter text", 20, 679, 0.60);
};
