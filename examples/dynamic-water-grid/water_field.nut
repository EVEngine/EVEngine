// CPU-authoritative conservative water field.
// Rendering consumes a quantized copy; gameplay always reads these float arrays.

function minValue(a, b) { return a < b ? a : b; }

function clampValue(v, lo, hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

class WaterField {
    width = 0;
    height = 0;
    amount = null;
    bed = null;
    delta = null;
    flowX = null;
    flowY = null;
    wet = null;
    maxVisualDepth = 1.0;
    wetThreshold = 0.035;
    flowRate = 2.4;

    constructor(w, h) {
        width = w;
        height = h;
        local count = w * h;
        amount = array(count, 0.0);
        bed = array(count, 0.0);
        delta = array(count, 0.0);
        flowX = array(count, 0.0);
        flowY = array(count, 0.0);
        wet = array(count, false);
    }

    function index(x, y) { return y * width + x; }

    function inside(x, y) {
        return x >= 0 && y >= 0 && x < width && y < height;
    }

    function clamp01(v) {
        if (v < 0.0) return 0.0;
        if (v > 1.0) return 1.0;
        return v;
    }

    function clear() {
        for (local i = 0; i < amount.len(); ++i) {
            amount[i] = 0.0;
            delta[i] = 0.0;
            flowX[i] = 0.0;
            flowY[i] = 0.0;
            wet[i] = false;
        }
    }

    function channelCenter(x) {
        return height * 0.48 + sin(x * 0.58) * 1.55 + sin(x * 0.19) * 0.65;
    }

    function resetRiver() {
        clear();
        for (local y = 0; y < height; ++y) {
            for (local x = 0; x < width; ++x) {
                local i = index(x, y);
                local distance = abs(y - channelCenter(x));
                // High banks and a gently descending river bed.
                bed[i] = 0.78 + distance * 0.075 + x * 0.005;
                if (distance < 2.35)
                    bed[i] = 0.12 + distance * 0.055 + x * 0.004;
                if (distance < 1.35)
                    amount[i] = 0.42 + (1.35 - distance) * 0.16;
            }
        }
        refreshWetState();
    }

    function getAmount(x, y) {
        if (!inside(x, y)) return 0.0;
        return amount[index(x, y)];
    }

    function getBed(x, y) {
        if (!inside(x, y)) return 1000.0;
        return bed[index(x, y)];
    }

    function getSurface(x, y) {
        if (!inside(x, y)) return 1000.0;
        local i = index(x, y);
        return bed[i] + amount[i];
    }

    function addAmount(x, y, value) {
        if (!inside(x, y)) return;
        local i = index(x, y);
        amount[i] = clampValue(amount[i] + value, 0.0, 1.75);
    }

    function normalizedDepth(x, y) {
        return clamp01(getAmount(x, y) / maxVisualDepth);
    }

    function isWet(x, y) {
        return inside(x, y) && amount[index(x, y)] > wetThreshold;
    }

    function refreshWetState() {
        local changed = false;
        for (local i = 0; i < amount.len(); ++i) {
            local nowWet = amount[i] > wetThreshold;
            if (nowWet != wet[i]) changed = true;
            wet[i] = nowWet;
        }
        return changed;
    }

    function transferPair(ax, ay, bx, by, dirX, dirY, dt) {
        local a = index(ax, ay);
        local b = index(bx, by);
        local difference = (bed[a] + amount[a]) - (bed[b] + amount[b]);
        if (abs(difference) < 0.0001) return;

        if (difference > 0.0) {
            // At most 22% per edge: four neighbours cannot overspend the cell.
            local q = minValue(difference * flowRate * dt, amount[a] * 0.22);
            delta[a] -= q;
            delta[b] += q;
            flowX[a] += dirX * q;
            flowY[a] += dirY * q;
            flowX[b] += dirX * q;
            flowY[b] += dirY * q;
        } else {
            local q = minValue(-difference * flowRate * dt, amount[b] * 0.22);
            delta[b] -= q;
            delta[a] += q;
            flowX[a] -= dirX * q;
            flowY[a] -= dirY * q;
            flowX[b] -= dirX * q;
            flowY[b] -= dirY * q;
        }
    }

    function step(dt) {
        for (local i = 0; i < amount.len(); ++i) {
            delta[i] = 0.0;
            flowX[i] = 0.0;
            flowY[i] = 0.0;
        }

        // Process every undirected edge once. All writes go to delta so the
        // result does not depend on traversal direction.
        for (local y = 0; y < height; ++y) {
            for (local x = 0; x < width; ++x) {
                if (x + 1 < width) transferPair(x, y, x + 1, y, 1.0, 0.0, dt);
                if (y + 1 < height) transferPair(x, y, x, y + 1, 0.0, 1.0, dt);
            }
        }

        for (local i = 0; i < amount.len(); ++i)
            amount[i] = clampValue(amount[i] + delta[i], 0.0, 1.75);

        return refreshWetState();
    }

    function totalAmount() {
        local total = 0.0;
        foreach (value in amount) total += value;
        return total;
    }
}
