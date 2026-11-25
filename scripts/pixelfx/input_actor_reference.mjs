// Independent input-image presentation oracle. Effect simulation and poses are pristine upstream;
// this explicit adapter is not the upstream stock-body draw path. No source files are modified.
// Dust/ghost formulas adapt core/act.js (MIT, Patch and rehan-remade); see repository notices.
import {execFileSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';

const flags = {};
const names = new Set(
    ['upstream', 'id', 'input', 'actor', 'out', 'width', 'height', 'origin-x', 'origin-y', 'x', 'y', 'flip', 'seed']);
for (let i = 2; i < process.argv.length; i += 2) {
    const key = process.argv[i];
    if (!key.startsWith('--') || !names.has(key.slice(2)) || i + 1 >= process.argv.length ||
        Object.hasOwn(flags, key.slice(2)))
        throw Error('Unknown, duplicate or incomplete option');
    flags[key.slice(2)] = process.argv[i + 1];
}
for (const key of ['upstream', 'id', 'input', 'actor', 'out'])
    if (!flags[key]) throw Error('Missing required option');
if (flags.seed !== undefined && (!/^\d+$/.test(flags.seed) || Number(flags.seed) > 4294967295))
    throw Error('Invalid seed');
const pin = 'a84159688af3ca37a4be05a0d422b94451b2e24e', root = path.resolve(flags.upstream);
if (execFileSync('git', ['-C', root, 'rev-parse', 'HEAD'], {encoding: 'utf8'}).trim() !== pin)
    throw Error('Unpinned source');
execFileSync('git', ['-C', root, 'diff', '--quiet', 'HEAD']);
const                                   load = p => import(pathToFileURL(path.join(root, p)));
const {effects}                                   = await                 load('src/fx/index.js');
const {createAct}                                 = await               load('src/core/act.js');
const {createFrame, painterFor}                   = await load('src/core/fx.js');
const {actorAlphaOf}                              = await            load('src/core/actor.js');
const                                   mod       = effects[flags.id];
if (!mod?.act) throw Error('Requires authored Act');
const read = p => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, ''));
const input = read(flags.input), a = read(flags.actor), W = Number(flags.width ?? 600), H = Number(flags.height ?? 320);
const ox = Number(flags['origin-x'] ?? 0), oy = Number(flags['origin-y'] ?? 0), flip = flags.flip === '1';
const x = Number(flags.x ?? 300), y = Number(flags.y ?? 160), X = ox + Math.round(x), Y = oy + Math.round(y);
const actor = {
    mask: {w: a.width, h: a.height, data: Uint8Array.from(a.mask)},
    pixels: Uint32Array.from(a.pixels),
    x: a.x,
    y: a.y,
    w: a.width,
    h: a.height,
    k: a.scale,
    head: a.head,
    chest: a.chest,
    eyes: a.eyes
};
if (a.schema !== 'eve.pixelfx.actor' || a.version !== 1 || a.source !== 'custom' || !Number.isInteger(a.width) ||
    !Number.isInteger(a.height) || a.width < 1 || a.width > 256 || a.height < 1 || a.height > 256 ||
    a.mask.length !== a.width * a.height || a.pixels.length !== a.width * a.height)
    throw Error('Invalid custom actor');
if (input.schema !== 'eve.pixelfx.act-probe-input' || input.version !== 1 || !Array.isArray(input.steps) ||
    input.steps.length > 4096 || input.steps.some(dt => !Number.isFinite(dt) || dt < 0 || dt > 1) ||
    !Array.isArray(input.frames) || input.frames.some(n => !Number.isInteger(n) || n < 0 || n > input.steps.length) ||
    !Array.isArray(input.stops) || input.stops.some(n => !Number.isInteger(n) || n < 0 || n > input.steps.length))
    throw Error('Invalid observation input');
if (![W, H, ox, oy].every(Number.isInteger) || W < 1 || H < 1 || W > 4096 || H > 4096 || Math.abs(ox) > 4096 ||
    Math.abs(oy) > 4096 || !Number.isFinite(x) || !Number.isFinite(y) || Math.abs(x) > 4096 || Math.abs(y) > 4096 ||
    flags.flip !== undefined && !['0', '1'].includes(flags.flip))
    throw Error('Invalid canvas/placement');
if (Object.keys(input).some(
        k => !['schema', 'version', 'steps', 'frames', 'stops', 'initialColor', 'initialLight'].includes(k)))
    throw Error('Unknown observation field');
if (input.frames.length > 4097 || input.stops.length > 4097) throw Error('Too many observations');
if (![1, 2].includes(a.scale) || a.mask.some(v => v !== 0 && v !== 1) ||
    a.pixels.some(v => !Number.isInteger(v) || v < 0 || v > 4294967295))
    throw Error('Invalid actor cells');
for (const value of [input.initialColor ?? 0, input.initialLight ?? 0])
    if (!Number.isInteger(value) || value < 0 || value > 4294967295) throw Error('Invalid initial plane');
const act  = createAct(mod, {actor, x, y, flip, ...(flags.seed === undefined ? {} : {seed: Number(flags.seed)})});
const make = () => createFrame(W, H, -ox, -oy), wanted = new Set(input.frames), stops = new Set(input.stops),
      frames                   = [];
const {hex}                    = await load('src/core/color.js');
const {Rng}                    = await load('src/core/math.js');
const               dustRandom = new Rng(Number(flags.seed ?? mod.meta.options?.seed ?? 1) + 9), specks = [],
                    dust       = mod.act.dust ?? [];
const               dustColors = ['#efe7dc', '#c9bfb8', '#8f8693', '#5a5468'].map(hex);
let                 nextDust = 0, dustLoop = 0;
const               palette    = mod.meta.options?.palette;
const               ghostColor = hex(mod.act.ghost ?? palette?.[Math.floor(palette.length * .7)] ?? '#8fb4ff');
const               ghosts = [], ghostCounts = [];
let                 ghostAccumulator = 0;
const               out              = path.resolve(flags.out);
fs.mkdirSync(out, {recursive: true});
let      wall  = 0;
const    clamp = v => Math.min(1, Math.max(0, v));
const    index = (px, py) => px >= 0 && py >= 0 && px < W && py < H && Number.isInteger(py * W + px) ? py * W + px : -1;
function point(i, q) {
    const lx = Math.floor(a.x) + i % a.width, ly = Math.floor(a.y) + Math.floor(i / a.width), k = clamp((-11 - ly) / 7);
    return [
        lx + Math.round((q.lean ?? 0) * k + Math.round(q.x ?? 0)),
        ly + Math.round((q.crouch ?? 0) * k + Math.round(q.y ?? 0))
    ];
}
function shadow(frame, q) {
    const rx = Math.round(q.x ?? 0), ry = Math.round(q.y ?? 0),
          half = 10 + (Math.abs(q.ffx - q.fbx) >> 1) - Math.min(8, Math.abs(ry) >> 1);
    for (let dy = 0; dy < 2; dy++)
        for (let dx = -half + dy * 2; dx <= half - dy * 2; dx++) {
            const j = index(X + (flip ? -rx : rx) + dx, Y + dy);
            if (j >= 0) frame.color[j] = (frame.color[j] & 0xffffff) | (Math.max(frame.color[j] >>> 24, 110) << 24);
        }
}
function drawGhosts(frame) {
    for (const g of ghosts)
        for (let i = 0; i < actor.pixels.length; i++) {
            const alpha = (actor.pixels[i] >>> 24) / 255;
            if (!alpha) continue;
            const [px, py] = point(i, g.q), j = index(X + (flip ? -px - 1 : px), Y + py);
            if (j < 0) continue;
            const k = .5 * (1 - g.age / .16) * alpha, d = frame.light[j];
            let   value = 0xff000000;
            for (const shift of [0, 8, 16])
                value |= Math.min(255, ((d >>> shift) & 255) + ((ghostColor >>> shift) & 255) * k) << shift;
            frame.light[j] = value;
        }
}
function writeFrame(frame, tick) {
    for (const p of specks) {
        const c = p.c || dustColors[Math.min(3, Math.floor(p.age / p.life * 4))], lx = Math.round(p.x),
              sy = Y + Math.round(p.y);
        for (let n = 0; n < (p.big ? 2 : 1); n++) {
            const j = index(flip ? X - lx - n - 1 : X + lx + n, sy);
            if (j >= 0) frame.color[j] = c;
        }
    }

    for (const [name, values] of [['color', frame.color], ['light', frame.light]]) {
        const b = Buffer.alloc(values.length * 4);
        for (let i = 0; i < values.length; i++) b.writeUInt32LE(values[i] >>> 0, i * 4);
        fs.writeFileSync(path.join(out, `frame_${tick}_${name}.bin`), b);
    }
    frames.push({time: wall, color: `frame_${tick}_color.bin`, light: `frame_${tick}_light.bin`});
    ghostCounts.push({tick, count: ghosts.length});
}
function render(tick) {
    const frame = make();
    frame.color.fill(input.initialColor ?? 0);
    frame.light.fill(input.initialLight ?? 0);
    const A = make(), B = make(), q = act.pose, rx = Math.round(q.x ?? 0), ry = Math.round(q.y ?? 0),
          active = act.t >= 0;
    if (!mod.act.onBody) {
        if (active) act.fx.draw(frame, 'back');
        shadow(frame, q);
        drawGhosts(frame);
        for (let i = 0; i < actor.pixels.length; i++)
            if (actor.pixels[i] >>> 24) {
                const [px, py] = point(i, q), j = index(X + (flip ? -px - 1 : px), Y + py);
                if (j >= 0) {
                    frame.color[j] = actor.pixels[i];
                    if (ghosts.length) frame.light[j] = 0;
                }
            }
        if (active) act.fx.draw(frame, 'front');
        writeFrame(frame, tick);
        return;
    }
    if (active) act.fx.draw(A, 'back');
    B.color.set(A.color);
    B.light.set(A.light);
    const tt =
        mod.meta.loop ? (act.t < 0 ? 0 : act.t % mod.meta.duration) : Math.min(mod.meta.duration, Math.max(0, act.t));
    const visible = !active || actorAlphaOf(mod, tt, act.fx.options) > 0;
    const painter = painterFor(B).place(x, y, flip);
    if (visible)
        for (let row = 0; row < a.height; row++)
            for (let col = 0; col < a.width; col++) {
                const c = actor.pixels[row * a.width + col];
                if (c) painter.dot(a.x + col, a.y + row, c);
            }
    if (active) act.fx.draw(B, 'front');
    shadow(frame, q);
    const    follow = mod.act.follow ?? 1, lean = (q.lean ?? 0) * follow, cr = (q.crouch ?? 0) * follow;
    function copy(light, sx, sy, c) {
        const ly = sy - Y, k = clamp((-11 - ly) / 7), f = clamp((-1 - ly) / 6), dx = Math.round(lean * k + rx * f),
              dy = Math.round(cr * k + ry * f), j = index(sx + (flip ? -dx : dx), sy + dy);
        if (j < 0) return;
        const dest = light ? frame.light : frame.color, d = dest[j];
        if (light) {
            let value = 0xff000000;
            for (const shift of [0, 8, 16])
                value |= Math.min(255, ((d >>> shift) & 255) + ((c >>> shift) & 255)) << shift;
            dest[j] = value;
        } else if (c >>> 24 === 255 || !d)
            dest[j] = c;
        else {
            const alpha = (c >>> 24) / 255;
            let   value = Math.max(c >>> 24, d >>> 24) << 24;
            for (const shift of [0, 8, 16])
                value |= Math.round(((c >>> shift) & 255) * alpha + ((d >>> shift) & 255) * (1 - alpha)) << shift;
            dest[j] = value;
        }
    }
    for (let sy = 0; sy < H; sy++)
        for (let sx = 0; sx < W; sx++) {
            const c = A.color[sy * W + sx];
            if (c) copy(false, sx, sy, c);
        }
    for (let i = 0; i < actor.pixels.length; i++) {
        if (!(actor.pixels[i] >>> 24)) continue;
        const lx = Math.floor(a.x) + i % a.width, ly = Math.floor(a.y) + Math.floor(i / a.width),
              source = index(X + (flip ? -lx - 1 : lx), Y + ly);
        const c      = source >= 0 ? B.color[source] : (visible ? actor.pixels[i] : 0);
        if (!c) continue;
        const k = clamp((-11 - ly) / 7), px = lx + Math.round((q.lean ?? 0) * k + rx),
              py   = ly + Math.round((q.crouch ?? 0) * k + ry);
        const dest = index(X + (flip ? -px - 1 : px), Y + py);
        if (dest >= 0) frame.color[dest] = c;
    }
    for (let sy = 0; sy < H; sy++)
        for (let sx = 0; sx < W; sx++) {
            const j = sy * W + sx, col = (flip ? X - sx - 1 : sx - X) - Math.floor(a.x), row = sy - Y - Math.floor(a.y),
                  i        = row * a.width + col;
            const standing = col >= 0 && row >= 0 && col < a.width && row < a.height && Number.isInteger(i) &&
                (actor.pixels[i] >>> 24) !== 0;
            if (B.color[j] && B.color[j] !== A.color[j] && !standing) copy(false, sx, sy, B.color[j]);
            if (B.light[j] & 0xffffff) copy(true, sx, sy, B.light[j]);
        }
    writeFrame(frame, tick);
}
if (stops.has(0)) act.stop();
if (wanted.has(0)) render(0);
for (let i = 0; i < input.steps.length; i++) {
    const frozen = act.frozen, dt = input.steps[i];
    act.update(dt);
    if (!frozen) {
        if (mod.meta.loop && act.t >= mod.meta.duration) {
            const loop = Math.floor(act.t / mod.meta.duration);
            if (loop !== dustLoop) {
                dustLoop = loop;
                nextDust = 0;
            }
        }
        const tt = mod.meta.loop && act.t >= mod.meta.duration ? act.t % mod.meta.duration : act.t;
        while (nextDust < dust.length && dust[nextDust].t <= tt) {
            const d = dust[nextDust++];
            for (let n = 0; n < (d.n ?? 6); n++) {
                const dir = d.dir ?? (dustRandom.next() < .5 ? -1 : 1);
                specks.push({
                    x: d.x + dustRandom.range(-3, 3),
                    y: -1 - dustRandom.range(0, 2),
                    vx: dir * dustRandom.range(22, 90),
                    vy: dustRandom.range(-34, -6),
                    age: 0,
                    life: dustRandom.range(.16, .4),
                    big: dustRandom.next() < .45,
                    c: d.c ? hex(d.c) : 0
                });
            }
        }
        for (const p of specks) {
            p.age += dt;
            p.vx *= 1 - 5 * dt;
            p.vy += 110 * dt;
            p.x += p.vx * dt;
            p.y = Math.min(-1, p.y + p.vy * dt);
        }
        for (let n = specks.length - 1; n >= 0; n--)
            if (specks[n].age >= specks[n].life) specks.splice(n, 1);
        ghostAccumulator += dt;
        if (act.pose.ghost && ghostAccumulator >= 1 / 30) {
            ghostAccumulator = 0;
            ghosts.push({q: structuredClone(act.pose), age: 0});
            if (ghosts.length > 7) ghosts.shift();
        }
        for (const g of ghosts) g.age += dt;
        while (ghosts.length && ghosts[0].age > .16) ghosts.shift();
    }
    wall += dt;
    if (stops.has(i + 1)) act.stop();
    if (wanted.has(i + 1)) render(i + 1);
}
fs.writeFileSync(path.join(out, 'frames.json'),
                 JSON.stringify({schema: 'eve.pixelfx-reference-frames/v1', id: flags.id, w: W, h: H, frames}));
fs.writeFileSync(path.join(out, 'oracle.json'), JSON.stringify({
    schema: 'eve.pixelfx.input-actor-oracle/v1',
    revision: pin,
    scope: 'Independent input-image presentation adapter using pristine Act clocks/poses/effect kernels; no targets',
    frames: frames.length,
    ghostCounts
}));
