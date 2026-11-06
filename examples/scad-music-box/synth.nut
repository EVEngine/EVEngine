// Music-box tine synthesis for the PCM stream path.
// Soft sine + weak odd harmonics with exponential decay — no disk samples.

dofile("assets/tune.nut");

local PI = 3.14159265358979;
local SAMPLE_RATE = 24000;

class BoxVoice {
    hz = 440.0;
    phase = 0.0;
    age = 0.0;
    life = 1.0;
    amp = 0.28;
    rate = SAMPLE_RATE;
    alive = true;

    constructor(frequency, amplitude = 0.28) {
        hz = frequency;
        amp = amplitude;
        phase = 0.0;
        age = 0.0;
        life = 0.85 + (880.0 / frequency) * 0.25;
        rate = SAMPLE_RATE;
        alive = true;
    }

    function mixInto(out, frames) {
        if (!alive) return;
        local invRate = 1.0 / rate;
        local written = 0;
        for (local i = 0; i < frames; ++i) {
            local t = age + i * invRate;
            if (t >= life) break;
            local attack = t < 0.003 ? (t / 0.003) : 1.0;
            local env = exp(-t * (3.2 + hz * 0.002)) * attack;
            phase += hz * invRate;
            phase -= floor(phase);
            local ang = phase * 2.0 * PI;
            local sample = sin(ang) * 0.78 + sin(ang * 3.01) * 0.16 + sin(ang * 5.03) * 0.06;
            out[i] = out[i] + sample * amp * env;
            written = i + 1;
        }
        age += written * invRate;
        if (age >= life) alive = false;
    }
}

class BoxSynth {
    rate = SAMPLE_RATE;
    voices = null;
    lastCylinder = 359.999;
    muted = false;
    noteCount = 0;
    silence = null;

    constructor() {
        rate = SAMPLE_RATE;
        voices = [];
        lastCylinder = 359.999;
        muted = false;
        noteCount = 0;
        silence = [];
        for (local i = 0; i < 1024; ++i) silence.push(0.0);
    }

    function pluck(tooth, amplitude = 0.32) {
        if (muted) return;
        if (tooth < 0 || tooth >= boxToothHz.len()) return;
        if (voices.len() >= 8) voices.remove(0);
        voices.push(BoxVoice(boxToothHz[tooth], amplitude));
        noteCount += 1;
    }

    function resetPins() {
        lastCylinder = 359.999;
    }

    function advanceCylinder(degrees) {
        local curr = degrees;
        while (curr < 0.0) curr += 360.0;
        while (curr >= 360.0) curr -= 360.0;
        local prev = lastCylinder;
        local travel = curr - prev;
        if (travel < 0.0) travel += 360.0;
        if (travel > 0.0001 && travel < 180.0) {
            foreach (pin in boxTunePins) {
                local dist = pin.angle - prev;
                while (dist < 0.0) dist += 360.0;
                while (dist >= 360.0) dist -= 360.0;
                if (dist > 0.0 && dist <= travel) pluck(pin.tooth);
            }
        }
        lastCylinder = curr;
    }

    function renderBlock(frames) {
        if (voices.len() == 0 && frames == silence.len()) return silence;
        local out = [];
        for (local i = 0; i < frames; ++i) out.push(0.0);
        if (voices.len() == 0) return out;
        local alive = [];
        foreach (voice in voices) {
            voice.mixInto(out, frames);
            if (voice.alive) alive.push(voice);
        }
        voices = alive;
        for (local i = 0; i < frames; ++i) {
            local s = out[i];
            if (s > 0.95) s = 0.95;
            if (s < -0.95) s = -0.95;
            out[i] = s;
        }
        return out;
    }
}
