// One surface definition feeds both the native collision geometry and the renderer.
const SKI_PI = 3.141592653589793;
function skiMin(a, b) { return a < b ? a : b; }
function skiMax(a, b) { return a > b ? a : b; }
function skiAbs(v) { return v < 0 ? -v : v; }
function skiAngle(v) {
    while (v > SKI_PI) v -= 2.0 * SKI_PI;
    while (v < -SKI_PI) v += 2.0 * SKI_PI;
    return v;
}

class SkiCourse {
    finish = 36000.0;
    pixelsPerMeter = 30.0;
    segmentWidth = 20.0;
    rampStart = 2160.0;
    rampSpacing = 2480.0;
    rampLength = 200.0;
    rampRise = 60.0;
    rampCount = 14;
    stages = null;

    constructor() { stages = ["FIRST TRACKS", "PINE RIDGE", "THE WHITE RUN"]; }

    function baseHeight(x) {
        return 340.0 + x * 0.32 + sin(x * 0.003) * 36.0 + sin(x * 0.008) * 8.0;
    }

    function rampAt(x) {
        local index = floor((x - rampStart) / rampSpacing).tointeger();
        if (index < 0 || index >= rampCount) return -1;
        return x <= rampStart + index * rampSpacing + rampLength ? index : -1;
    }

    function rampHeight(x, index) {
        local t = (x - rampStart - index * rampSpacing) / rampLength;
        return baseHeight(x) - rampRise * t * t;
    }

    function height(x) {
        local ramp = rampAt(x);
        return ramp >= 0 ? rampHeight(x, ramp) : baseHeight(x);
    }

    // The two sides of a lip deliberately have different heights at the same X.
    // Select the owning segment by its midpoint so no sloping bridge closes the drop.
    function segmentHeight(x, midpoint) {
        local ramp = rampAt(midpoint);
        return ramp >= 0 ? rampHeight(x, ramp) : baseHeight(x);
    }

    function slope(x) {
        local value = 0.32 + cos(x * 0.003) * 0.108 + cos(x * 0.008) * 0.064;
        local ramp = rampAt(x);
        if (ramp >= 0) {
            local t = (x - rampStart - ramp * rampSpacing) / rampLength;
            value -= 2.0 * rampRise * t / rampLength;
        }
        return value;
    }
}
