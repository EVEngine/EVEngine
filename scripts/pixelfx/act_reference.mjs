// Pinned createAct control and raster observer; body and target composition are explicit flags.
import {execFileSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';

const f = {};
for (let i = 2; i < process.argv.length; i += 2) {
    const key = process.argv[i];
    if (!['--actor', '--contract', '--upstream', '--id', '--input', '--out', '--seed', '--warm-id', '--warm-steps', '--targets', '--body',
          '--flip', '--x', '--y', '--width', '--height', '--origin-x', '--origin-y']
             .includes(key) ||
        i + 1 >= process.argv.length)
        throw new Error('Unknown flag or missing value');
    f[key.slice(2)] = process.argv[i + 1];
}
if (f.seed !== undefined && (!/^\d+$/.test(f.seed) || Number(f.seed) > 4294967295))
    throw new Error('Seed must be a uint32 integer');
const root = path.resolve(f.upstream), out = path.resolve(f.out), pin = 'a84159688af3ca37a4be05a0d422b94451b2e24e';
if (execFileSync('git', ['-C', root, 'rev-parse', 'HEAD'], {encoding: 'utf8'}).trim() !== pin)
    throw new Error('Unpinned reference');
execFileSync('git', ['-C', root, 'diff', '--quiet', 'HEAD'], {encoding: 'utf8'});
const {effects} = await                         import(pathToFileURL(path.join(root, 'src/fx/index.js')));
const {createAct, actTiming, actBounds} = await import(pathToFileURL(path.join(root, 'src/core/act.js')));
const {createFrame, clearFrame}         = await         import(pathToFileURL(path.join(root, 'src/core/fx.js')));
const input                             = JSON.parse(fs.readFileSync(f.input, 'utf8').replace(/^\uFEFF/, ''));
if (input.schema !== 'eve.pixelfx.act-probe-input' || input.version !== 1) throw new Error('Bad input schema/version');
if (Object.keys(input).some(
        k => !['schema', 'version', 'steps', 'frames', 'stops', 'initialColor', 'initialLight'].includes(k)))
    throw new Error('Unknown input field');
if (!Array.isArray(input.steps) || input.steps.length > 4096 ||
    input.steps.some(n => !Number.isFinite(n) || n < 0 || n > 1))
    throw new Error('Invalid steps');
for (const name of ['frames', 'stops'])
    if (!Array.isArray(input[name]) || input[name].length > 4097 ||
        input[name].some(n => !Number.isInteger(n) || n < 0 || n > input.steps.length))
        throw new Error('Invalid sample indices');
const initialColor = input.initialColor ?? 0, initialLight = input.initialLight ?? 0;
for (const value of [initialColor, initialLight])
    if (!Number.isInteger(value) || value < 0 || value > 4294967295) throw new Error('Invalid initial plane color');
const mod = effects[f.id];
if (!mod) throw new Error('Unknown effect');
for (const name of ['targets', 'body', 'contract', 'flip'])
    if (f[name] !== undefined && f[name] !== '0' && f[name] !== '1') throw new Error('Expected boolean 0 or 1');
for (const name of ['x', 'y'])
    if (f[name] !== undefined &&
        (!/^[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?$/.test(f[name]) || !Number.isFinite(Number(f[name])) ||
         Math.abs(Number(f[name])) > 4096))
        throw new Error('Placement outside bounds');
for (const name of ['width', 'height', 'origin-x', 'origin-y'])
    if (f[name] !== undefined &&
        (!/^[+-]?\d+$/.test(f[name]) || !Number.isInteger(Number(f[name])) ||
         Number(f[name]) < (name === 'width' || name === 'height' ? 1 : -4096) || Number(f[name]) > 4096))
        throw new Error('Canvas outside bounds');
const o = {
    x: Number(f.x ?? 0),
    y: Number(f.y ?? 0),
    flip: f.flip === '1'
};
if (f.seed !== undefined) o.seed = Number(f.seed);
if (f.actor !== undefined) {
    const a = JSON.parse(fs.readFileSync(f.actor, 'utf8').replace(/^\uFEFF/, ''));
    const {stockActor} = await import(pathToFileURL(path.join(root, 'src/core/actor.js')));
    if (a.source === 'hero' || a.source === 'dummy') o.actor = stockActor(a.source === 'dummy' ? 2 : 1);
    else if (a.source === 'custom') o.actor = {
        mask: {w: a.width, h: a.height, data: Uint8Array.from(a.mask)},
        pixels: Uint32Array.from(a.pixels), x: a.x, y: a.y, w: a.width, h: a.height,
        k: a.scale, head: a.head, chest: a.chest, eyes: a.eyes
    };
    else if (a.source === 'none') throw new Error('Pristine Act resolves a missing actor to stock; native None has no equivalent source actor input');
    else throw new Error('Unknown actor source');
}
const actor = createAct(mod, o),
      frame = createFrame(Number(f.width ?? mod.meta.size[0]), Number(f.height ?? mod.meta.size[1]),
                          -Number(f['origin-x'] ?? mod.meta.origin[0]), -Number(f['origin-y'] ?? mod.meta.origin[1]));
if ((f['warm-id'] === undefined) !== (f['warm-steps'] === undefined) ||
    (f['warm-id'] !== undefined &&
     (!/^\d+$/.test(f['warm-steps']) || Number(f['warm-steps']) > 4096 || f.body !== '1')))
    throw new Error('Warm requires bounded steps and body 1');
if (f['warm-id'] !== undefined) {
    const warmMod = effects[f['warm-id']];
    if (!warmMod) throw new Error('Unknown warm effect');
    const warm = createAct(warmMod, o);
    for (let i = 0; i < Number(f['warm-steps']); i++) warm.update(1 / 60);
    const warmFrame = createFrame(frame.w, frame.h, frame.ox, frame.oy);
    warm.draw(warmFrame, {body: true, target: f.targets === '1'});
}
const requested = new Set(input.frames), stops = new Set(input.stops), states = [], frames = [];
let   events = [], wall = 0;
actor.onHit = h => events.push({sourceTime: h.t, actTime: actor.t, effectTime: actor.fx.t});
fs.mkdirSync(out, {recursive: true});
if (f.contract === '1')
    fs.writeFileSync(
        path.join(out, 'contract.json'),
        JSON.stringify(
            {schema: 'eve.pixelfx.act-contract/v1', id: f.id, timing: actTiming(mod), bounds: actBounds(mod)}) +
            '\n');
function observe(index) {
    if (stops.has(index)) actor.stop();
    const pose = actor.pose;
    states.push({
        index,
        wallTime: wall,
        actTime: actor.t,
        effectTime: actor.fx.t,
        frozen: actor.frozen,
        done: actor.done(),
        shake: actor.shake,
        targets: actor.targets.map(g => ({
                                       x: g.x,
                                       theta: g.theta,
                                       omega: g.omega,
                                       flashTime: g.flashT,
                                       skin: g.skin,
                                       shiver: g.shiver,
                                       clock: g.clock,
                                       randomState: g.R.s,
                                       straw: g.straw.map(p => ({...p}))
                                   })),
        pose,
        presence: Object.keys(pose).filter(k => pose[k] !== undefined),
        events
    });
    if (requested.has(index)) {
        clearFrame(frame);
        frame.color.fill(initialColor);
        frame.light.fill(initialLight);
        actor.draw(frame, {body: f.body === '1', target: f.targets === '1'});
        const stem = `frame_${index}`;
        for (const [name, values] of [['color', frame.color], ['light', frame.light]]) {
            const bytes = Buffer.alloc(values.length * 4);
            for (let j = 0; j < values.length; j++) bytes.writeUInt32LE(values[j] >>> 0, j * 4);
            fs.writeFileSync(path.join(out, `${stem}_${name}.bin`), bytes);
        }
        frames.push({time: wall, color: stem + '_color.bin', light: stem + '_light.bin'});
    }
}
observe(0);
for (let i = 0; i < input.steps.length; i++) {
    const dt = input.steps[i];
    if (!Number.isFinite(dt) || dt < 0 || dt > 1) throw new Error('Bad delta');
    events = [];
    actor.update(dt);
    wall += dt;
    observe(i + 1);
}
fs.writeFileSync(path.join(out, 'states.json'),
                 JSON.stringify({schema: 'eve.pixelfx.act-clock-probe/v1', revision: pin, id: f.id, states}) + '\n');
fs.writeFileSync(
    path.join(out, 'frames.json'),
    JSON.stringify({schema: 'eve.pixelfx-reference-frames/v1', id: f.id, w: frame.w, h: frame.h, frames}) + '\n');
console.log(`${f.id}: ${states.length} source Act snapshots; ${frames.length} effect-layer frames`);
