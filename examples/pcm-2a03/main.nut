dofile("synth.nut");
dofile("verification.nut");

local stream = null, voice = null, fontFace = null;
local waveform = [], previous = {}, age = 0.0, nextAuto = 0.35;
local automatic = true, selected = 0, sequence = 0, ended = false;
local names = ["JUMP", "COIN", "HURT"];
local colors = [[0.25,0.83,0.76], [1.0,0.75,0.25], [1.0,0.37,0.40]];
local completions = 0;

function checked(result) {
    if (!result.ok) throw result.status.summary;
    return result.value;
}
function pressed(key) {
    local down = keyboard.isDown(key);
    local was = key in previous ? previous[key] : false;
    previous[key] <- down;
    return down && !was;
}
function trigger(effect) {
    checked(stream.reset());
    selected = effect;
    voice = ChipVoice(effect, 1);
    waveform = [];
    ended = false;
    checked(stream.play());
    print("PCM_EFFECT_START " + names[effect] + " frames=" + voice.length + "\n");
}
function text(s, x, y, size = 1.0, c = null) {
    if (c == null) c = [0.85,0.88,0.91];
    gfx.drawText(fontFace, s, x.tofloat(), y.tofloat(), c[0],c[1],c[2],1.0,size.tofloat());
}
function rect(x,y,w,h,c) {
    gfx.drawSolidRect(x.tofloat(),y.tofloat(),w.tofloat(),h.tofloat(),c[0],c[1],c[2],1.0);
}

eve_init = function() {
    gfx.setBackgroundColor(0.035,0.045,0.065,1.0);
    fontFace = gfx.newFont(eve.Font().newFontDataFromFile("assets/DejaVuSans-Bold.ttf",24),
        " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/|:.-+[]");
    if (stream != null) stream.close();
    stream = checked(audio.newPcmStream(48000, 1));
    print("PCM_2A03_READY script-synthesis mono 48000Hz PCM16\n");
};
eve_reload <- function() {
    eve_init();
};
eve_update = function(dt) {
    age += dt;
    if (pressed("a")) automatic = !automatic;
    if (pressed("t")) verifySynthesis();
    if (pressed("1")) { automatic = false; trigger(0); }
    if (pressed("2")) { automatic = false; trigger(1); }
    if (pressed("3")) { automatic = false; trigger(2); }
    if (automatic && voice == null && age >= nextAuto) {
        trigger(sequence % 3); sequence++; nextAuto = age + 1.2;
    }
    if (voice != null && !ended) {
        // Keep about 43ms ahead; work is bounded to two 1024-frame blocks/update.
        // Synthesis time comes from the integer sample cursor, independent of dt.
        local info = stream.getInfo();
        local budget = 2;
        while (budget > 0 && info.availableBlocks > 0 && info.queuedFrames < 2048) {
            local block = voice.nextBlock(1024);
            if (block.len() == 0) {
                checked(stream.finishInput()); ended = true; break;
            }
            checked(stream.submit(block));
            foreach (value in block) waveform.append(value);
            info = stream.getInfo(); budget--;
        }
    }
    if (voice != null && ended && stream.getInfo().phase == "finished") {
        local info = stream.getInfo();
        if (info.playedFrames != voice.length) throw "PCM frame accounting mismatch";
        print("PCM_EFFECT_DONE " + names[selected] + " playedFrames=" + info.playedFrames + "\n");
        voice = null; completions++; nextAuto = age + 0.8;
        if (completions == 3) print("PCM_2A03_PLAYBACK_PASS jump coin hurt drained\n");
    }
};
eve_render = function() {
    gfx.clear();
    text("2A03 / SOUND LAB",40,30,1.4);
    text("SCRIPT SYNTHESIS / REALTIME PCM STREAM",42,80,0.65,[0.46,0.58,0.67]);
    for (local i = 0; i < 3; ++i) {
        local x = 40 + i * 300;
        rect(x,132,280,106,[0.075,0.095,0.13]);
        rect(x,132,280,4,colors[i]);
        text((i+1)+" / "+names[i],x+20,155,0.95,colors[i]);
        text(["PULSE / RISING SWEEP","PULSE / TWO NOTES","PULSE + 15-BIT NOISE"][i],x+20,199,0.49);
    }
    rect(40,270,880,205,[0.025,0.033,0.048]);
    rect(60,385,840,1,[0.16,0.20,0.25]);
    if (waveform.len() > 0) {
        for (local i = 0; i < 420; ++i) {
            local value = waveform[(i * waveform.len() / 420).tointeger()];
            rect(60+i*2,385-value*110,2,3,colors[selected]);
        }
    }
    local info = stream.getInfo();
    text(names[selected]+" / "+info.phase,60,286,0.68,colors[selected]);
    text("48000 HZ / MONO / PCM16 OUTPUT",60,443,0.55,[0.46,0.58,0.67]);
    text("1 JUMP  2 COIN  3 HURT  T VERIFY  A AUTO: "+(automatic ? "ON" : "OFF"),40,505,0.68);
    text("QUEUED "+info.queuedFrames+" FRAMES / PLAYED "+info.playedFrames+" / COMPLETE "+completions,40,548,0.57);
};
