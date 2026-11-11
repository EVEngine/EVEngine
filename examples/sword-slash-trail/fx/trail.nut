// Blade-edge trail: sample sword root/tip each frame → fan ribbon mesh → textured draw.
// Mirrors stylize::TrailEmitter (CPU sampler + triangle ribbon) for EveScript demos.
// Providers: gfx. Main/render thread only. No callbacks, wall clocks, or retained weapon pointers.

local need = function(r, name) {
    if (!r.ok) throw "slash-trail " + name + ": " + r.status.summary;
    return r.value;
}
local bounded = function(v, lo, hi, name) {
    if ((typeof v != "float" && typeof v != "integer") || v != v || v < lo || v > hi)
        throw "slash-trail: invalid " + name;
    return v.tofloat();
}
local clamp = function(v, a, b) { return v < a ? a : (v > b ? b : v); }
local finite3 = function(p, name) {
    foreach (k in ["x", "y", "z"]) {
        if (!(k in p) || (typeof p[k] != "float" && typeof p[k] != "integer") || p[k] != p[k])
            throw "slash-trail: non-finite " + name + "." + k;
    }
    return {x = p.x.tofloat(), y = p.y.tofloat(), z = p.z.tofloat()};
}
local distMax = function(aRoot, aTip, bRoot, bTip) {
    local drx = aRoot.x - bRoot.x, dry = aRoot.y - bRoot.y, drz = aRoot.z - bRoot.z;
    local dtx = aTip.x - bTip.x, dty = aTip.y - bTip.y, dtz = aTip.z - bTip.z;
    local dr = sqrt(drx * drx + dry * dry + drz * drz);
    local dt = sqrt(dtx * dtx + dty * dty + dtz * dtz);
    return dr > dt ? dr : dt;
}
local cross = function(ax, ay, az, bx, by, bz) {
    return {x = ay * bz - az * by, y = az * bx - ax * bz, z = ax * by - ay * bx};
}
local normalize = function(v) {
    local len = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-6) return {x = 0.0, y = 1.0, z = 0.0};
    return {x = v.x / len, y = v.y / len, z = v.z / len};
}

local SlashTrail = class {
    _graphics = null;
    _shader = null;
    _material = null;
    _texture = null;
    _mesh = null;
    _object = null;
    _closedMaterial = null;
    _samples = null;
    _segment = 0;
    _forceBreak = true;
    _maxSamples = 48;
    _lifetime = 0.28;
    _minDistance = 0.015;
    _teleportDistance = 2.0;
    _closed = false;
    _color = null;
    _edge = null;
    _opacity = 1.0;
    _time = 0.0;
    _useTexColor = 1.0;

    constructor(graphics, texture, shaderPath, closedMaterial, settings) {
        _graphics = graphics;
        _closedMaterial = closedMaterial;
        _texture = texture;
        _samples = [];
        _color = [1.0, 0.92, 0.72];
        _edge = [0.55, 0.82, 1.0];
        if (typeof settings == "table") {
            if ("maxSamples" in settings) _maxSamples = settings.maxSamples.tointeger();
            if ("lifetime" in settings) _lifetime = settings.lifetime.tofloat();
            if ("minDistance" in settings) _minDistance = settings.minDistance.tofloat();
            if ("teleportDistance" in settings) _teleportDistance = settings.teleportDistance.tofloat();
            if ("color" in settings) _color = settings.color;
            if ("edge" in settings) _edge = settings.edge;
            if ("opacity" in settings) _opacity = settings.opacity.tofloat();
            if ("useTexColor" in settings) _useTexColor = settings.useTexColor.tofloat();
        }
        bounded(_maxSamples, 2, 256, "maxSamples");
        bounded(_lifetime, 0.02, 5.0, "lifetime");
        bounded(_minDistance, 0.0, 5.0, "minDistance");
        bounded(_teleportDistance, _minDistance > 0.0 ? _minDistance : 0.001, 50.0, "teleportDistance");
        bounded(_opacity, 0.0, 1.0, "opacity");
        bounded(_useTexColor, 0.0, 1.0, "useTexColor");

        // Placeholder ribbon kept alive so updateMeshVertices can grow/shrink each frame.
        local pos = [0.0, 0.0, 0.0, 0.0, 0.01, 0.0, 0.01, 0.0, 0.0, 0.01, 0.01, 0.0];
        local nrm = [0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0];
        local uv = [0.0, 0.0, 0.0, 1.0, 1.0, 0.0, 1.0, 1.0];
        local idx = [0, 1, 3, 0, 3, 2];
        _mesh = graphics.newMeshFromArrays(pos, nrm, uv, 4, idx, 6);
        if (_mesh == null) throw "slash-trail: mesh upload failed";

        _shader = need(graphics.loadMeshShaderSpv("", shaderPath), "load shader");
        foreach (k in ["time", "scroll", "red", "green", "blue", "edgeRed", "edgeGreen", "edgeBlue", "intensity", "useTexColor"])
            _shader.declareFloat(k);
        _shader.sendFloat("scroll", 0.35);
        foreach (i, k in ["red", "green", "blue"]) _shader.sendFloat(k, _color[i]);
        foreach (i, k in ["edgeRed", "edgeGreen", "edgeBlue"]) _shader.sendFloat(k, _edge[i]);
        _shader.sendFloat("intensity", 1.85);
        _shader.sendFloat("useTexColor", _useTexColor);

        _material = graphics.newMaterial();
        _material.setShader(_shader);
        _material.setShadingModel("custom");
        _material.setAlbedoTexture(texture);
        _material.setSurfaceMode("transparent");
        _material.setBlendMode("additive");
        _material.setDepthWrite(false);
        _material.setDoubleSided(true);
        _material.setCastShadow(false);
        _material.setReceiveShadow(false);
        _material.setReceiveLight(false);

        _object = eve.Renderable3D();
        _object.setMesh(_mesh);
        _object.setMaterial(_material);
        _object.setVisible(false);
    }

    function breakTrail() { _forceBreak = true; }
    function clear() {
        _samples = [];
        _segment = 0;
        _forceBreak = true;
        if (!_closed) _object.setVisible(false);
    }
    function sampleCount() { return _samples.len(); }
    function isVisible() { return !_closed && _object.getVisible(); }

    /** @brief Age samples with injected dt; cull expired edges. */
    function advance(dt) {
        bounded(dt, 0.0, 5.0, "dt");
        if (_closed) return;
        _time += dt;
        foreach (s in _samples) s.age += dt;
        while (_samples.len() > 0 && _samples[0].age >= _lifetime) _samples.remove(0);
        _rebuild();
    }

    /**
     * @brief Append one blade edge (root near guard, tip at point).
     * @return "added" | "started" | "skipped"
     */
    function append(root, tip) {
        if (_closed) throw "slash-trail: closed";
        local r = finite3(root, "root");
        local t = finite3(tip, "tip");
        local result = "added";
        if (_samples.len() > 0 && !_forceBreak) {
            local last = _samples[_samples.len() - 1];
            local distance = distMax(last.root, last.tip, r, t);
            if (distance < _minDistance) return "skipped";
            if (distance > _teleportDistance) {
                _segment += 1;
                result = "started";
            }
        } else {
            if (_samples.len() > 0) _segment += 1;
            result = "started";
        }
        _samples.push({root = r, tip = t, age = 0.0, segment = _segment});
        _forceBreak = false;
        while (_samples.len() > _maxSamples) _samples.remove(0);
        _rebuild();
        return result;
    }

    function setPalette(color, edge, opacity) {
        if (typeof color != "array" || color.len() != 3) throw "slash-trail: color rgb";
        if (typeof edge != "array" || edge.len() != 3) throw "slash-trail: edge rgb";
        foreach (i, v in color) _color[i] = bounded(v, 0.0, 1.0, "color");
        foreach (i, v in edge) _edge[i] = bounded(v, 0.0, 1.0, "edge");
        _opacity = bounded(opacity, 0.0, 1.0, "opacity");
        foreach (i, k in ["red", "green", "blue"]) _shader.sendFloat(k, _color[i]);
        foreach (i, k in ["edgeRed", "edgeGreen", "edgeBlue"]) _shader.sendFloat(k, _edge[i]);
    }

    /** @brief Swap ribbon albedo; useTexColor=1 samples PNG RGB, 0 uses procedural palette only. */
    function setTexture(texture, useTexColor = 1.0) {
        if (_closed) throw "slash-trail: closed";
        if (texture == null) throw "slash-trail: texture required";
        _texture = texture;
        _useTexColor = bounded(useTexColor, 0.0, 1.0, "useTexColor");
        _material.setAlbedoTexture(texture);
        _shader.sendFloat("useTexColor", _useTexColor);
    }

    function close() {
        if (_closed) return;
        _object.setVisible(false);
        _object.setMaterial(_closedMaterial);
        _closed = true;
    }

    function _rebuild() {
        if (_closed) return;
        if (_samples.len() < 2) {
            _object.setVisible(false);
            return;
        }
        local pos = [];
        local nrm = [];
        local uv = [];
        local indices = [];
        local segmentStart = 0;
        while (segmentStart < _samples.len()) {
            local segmentEnd = segmentStart + 1;
            while (segmentEnd < _samples.len() &&
                   _samples[segmentEnd].segment == _samples[segmentStart].segment)
                segmentEnd += 1;
            local segmentSize = segmentEnd - segmentStart;
            local vertBase = pos.len() / 3;
            for (local i = segmentStart; i < segmentEnd; ++i) {
                local s = _samples[i];
                local u = segmentSize > 1
                    ? (i - segmentStart).tofloat() / (segmentSize - 1).tofloat()
                    : 0.0;
                local fade = clamp(1.0 - s.age / _lifetime, 0.0, 1.0);
                pos.extend([s.root.x, s.root.y, s.root.z]);
                pos.extend([s.tip.x, s.tip.y, s.tip.z]);
                nrm.extend([0.0, 1.0, 0.0, 0.0, 1.0, 0.0]);
                // UV.x = arc progress (tail to head); UV.y = root(0) to tip(1).
                uv.extend([u, 0.0, u, 1.0]);
                // Dissolve old samples by pulling the root inward first; keep the tip path long
                // so the outer arc still reads as blade-point glow while the trail ages out.
                if (fade < 0.999) {
                    local mid = {
                        x = (s.root.x + s.tip.x) * 0.5,
                        y = (s.root.y + s.tip.y) * 0.5,
                        z = (s.root.z + s.tip.z) * 0.5
                    };
                    local rootKeep = 0.20 + 0.80 * fade;
                    local tipKeep = 0.70 + 0.30 * fade;
                    local vi = (pos.len() / 3) - 2;
                    pos[vi * 3] = mid.x + (s.root.x - mid.x) * rootKeep;
                    pos[vi * 3 + 1] = mid.y + (s.root.y - mid.y) * rootKeep;
                    pos[vi * 3 + 2] = mid.z + (s.root.z - mid.z) * rootKeep;
                    pos[vi * 3 + 3] = mid.x + (s.tip.x - mid.x) * tipKeep;
                    pos[vi * 3 + 4] = mid.y + (s.tip.y - mid.y) * tipKeep;
                    pos[vi * 3 + 5] = mid.z + (s.tip.z - mid.z) * tipKeep;
                }
                if (i > segmentStart) {
                    local prev = vertBase + (i - segmentStart - 1) * 2;
                    local cur = vertBase + (i - segmentStart) * 2;
                    indices.extend([prev, prev + 1, cur + 1, prev, cur + 1, cur]);
                }
            }
            segmentStart = segmentEnd;
        }

        // Face normals for lighting-independent custom shader (still useful for double-sided).
        for (local i = 0; i < nrm.len(); ++i) nrm[i] = 0.0;
        for (local t = 0; t + 2 < indices.len(); t += 3) {
            local ia = indices[t], ib = indices[t + 1], ic = indices[t + 2];
            local ax = pos[ib * 3] - pos[ia * 3], ay = pos[ib * 3 + 1] - pos[ia * 3 + 1],
                  az = pos[ib * 3 + 2] - pos[ia * 3 + 2];
            local bx = pos[ic * 3] - pos[ia * 3], by = pos[ic * 3 + 1] - pos[ia * 3 + 1],
                  bz = pos[ic * 3 + 2] - pos[ia * 3 + 2];
            local n = normalize(cross(ax, ay, az, bx, by, bz));
            foreach (idx in [ia, ib, ic]) {
                nrm[idx * 3] += n.x;
                nrm[idx * 3 + 1] += n.y;
                nrm[idx * 3 + 2] += n.z;
            }
        }
        local vcount = pos.len() / 3;
        for (local i = 0; i < vcount; ++i) {
            local n = normalize({x = nrm[i * 3], y = nrm[i * 3 + 1], z = nrm[i * 3 + 2]});
            nrm[i * 3] = n.x; nrm[i * 3 + 1] = n.y; nrm[i * 3 + 2] = n.z;
        }

        if (!_graphics.updateMeshVertices(_mesh, pos, nrm, uv, vcount, indices, indices.len()))
            throw "slash-trail: dynamic mesh update failed";

        _shader.sendFloat("time", _time);
        local headFade = 1.0;
        if (_samples.len() > 0) {
            local oldest = _samples[0].age;
            headFade = clamp(1.0 - oldest / _lifetime * 0.35, 0.45, 1.0);
        }
        _object.setTint(1.0, 1.0, 1.0, _opacity * headFade);
        _object.setVisible(true);
    }
}

local closedMaterial = null;

/**
 * @brief Prepare one trail emitter with uploaded mask texture and mesh shader.
 * @cost One mesh + shader + material; amortized across every slash rebuild.
 * @ownership Caller owns the emitter; native scene owns the Renderable3D.
 */
return {
    create = function(graphics, maskPath, shaderPath, settings = null) {
        if (closedMaterial == null) closedMaterial = graphics.newMaterial();
        local image = eve.Image().newImageDataFromFile(maskPath);
        if (image == null) throw "slash-trail: mask unavailable: " + maskPath;
        local texture = graphics.newTexture(image, true, false);
        if (texture == null) throw "slash-trail: texture upload failed";
        return SlashTrail(graphics, texture, shaderPath, closedMaterial, settings);
    },
    /** @brief Upload an image path to a Graphics texture for setTexture swaps. */
    loadTexture = function(graphics, maskPath) {
        local image = eve.Image().newImageDataFromFile(maskPath);
        if (image == null) throw "slash-trail: mask unavailable: " + maskPath;
        local texture = graphics.newTexture(image, true, false);
        if (texture == null) throw "slash-trail: texture upload failed";
        return texture;
    }
};
