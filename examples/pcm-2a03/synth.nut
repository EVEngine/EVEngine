// All synthesis runs in script, ahead of playback, never in an audio callback.
// NTSC timer-quantized pulse pitch and a seeded 15-bit noise shift register.
// This is an original 2A03-style sound design, not a register/cycle emulator.
class ChipVoice {
    kind = 0;
    rate = 48000;
    cursor = 0;
    length = 0;
    phase = 0.0;
    noiseClock = 0.0;
    noise = 1;
    constructor(effect, seed = 1) {
        kind = effect;
        noise = seed & 32767;
        if (noise == 0) noise = 1;
        length = (rate * [0.24, 0.32, 0.42][kind]).tointeger();
    }
    function pulse(hz, duty) {
        local timer = floor(1789773.0 / (16.0 * hz) - 1.0);
        if (timer < 8.0) timer = 8.0;
        if (timer > 2047.0) timer = 2047.0;
        phase += 1789773.0 / (16.0 * (timer + 1.0) * rate);
        phase -= floor(phase);
        return phase < duty ? (1.0 - duty) : -duty;
    }
    function nextBlock(limit) {
        local out = [];
        local periods = [4,8,16,32,64,96,128,160,202,254,380,508,762,1016,2034,4068];
        for (local i = 0; i < limit && cursor < length; ++i, ++cursor) {
            local t = cursor.tofloat() / rate;
            local u = cursor.tofloat() / length;
            // Short attack/release and 4-bit envelope avoid discontinuous ends.
            local attack = t < 0.002 ? t / 0.002 : 1.0;
            local release = (1.0 - u) * 24.0;
            if (release > 1.0) release = 1.0;
            local envelope = floor((1.0 - u) * 15.0) / 15.0;
            local sample = 0.0;
            if (kind == 0) {
                // Rising pulse sweep: jump.
                sample = pulse(210.0 + 820.0 * u * u, 0.25) * 0.65;
            } else if (kind == 1) {
                // Two bright notes: pickup/coin.
                sample = pulse(t < 0.09 ? 987.77 : 1318.51, 0.125) * 0.75;
            } else {
                // Falling pulse plus deterministic metallic noise: hurt.
                local period = periods[8 + (u * 6).tointeger()];
                noiseClock += 1789773.0 / (period * rate);
                while (noiseClock >= 1.0) {
                    local feedback = (noise & 1) ^ ((noise >> 1) & 1);
                    noise = (noise >> 1) | (feedback << 14);
                    noiseClock -= 1.0;
                }
                sample = pulse(380.0 - 280.0 * u, 0.5) * 0.30
                    + ((noise & 1) == 0 ? 0.18 : -0.18);
            }
            out.append(sample * envelope * attack * release);
        }
        return out;
    }
}
