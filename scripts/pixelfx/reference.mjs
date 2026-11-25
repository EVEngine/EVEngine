// Reference-only tool. Upstream JavaScript is never linked into the EVEngine runtime.
// Usage: node scripts/pixelfx/reference.mjs --upstream PATH --out build/pixelfx-reference [--id family/name]
import {execFileSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';

const args = process.argv.slice(2), flags = {};
for (let i = 0; i < args.length; i += 2) {
    if (!args[i]?.startsWith('--') || !args[i + 1])
        throw new Error('Expected --upstream PATH --out PATH [--id ID] [--options JSON]');
    flags[args[i].slice(2)] = args[i + 1];
}
if (!flags.upstream || !flags.out) throw new Error('--upstream and --out are required');
const supported = new Set(['upstream', 'out', 'id', 'options', 'actor', 'layer', 'canvas', 'times', 'frames']);
for (const key of Object.keys(flags))
    if (!supported.has(key)) throw new Error(`Unknown flag --${key}`);
if (flags.times && flags.frames) throw new Error('Use either --times or --frames');
const sampleCount = flags.frames ? Number(flags.frames) : 9;
if (!Number.isInteger(sampleCount) || sampleCount < 2 || sampleCount > 4097)
    throw new Error('Frame count must be in 2..4097');
const root = path.resolve(flags.upstream), out = path.resolve(flags.out);
const expected = 'a84159688af3ca37a4be05a0d422b94451b2e24e';
const revision = execFileSync('git', ['-C', root, 'rev-parse', 'HEAD'], {encoding: 'utf8'}).trim();
if (revision !== expected) throw new Error(`Reference must be pinned to ${expected}, got ${revision}`);
// Refuse modified tracked source even when HEAD still names the expected pin.
execFileSync('git', ['-C', root, 'diff', '--quiet', 'HEAD'], {encoding: 'utf8'});
const {effects, categories} = await import(pathToFileURL(path.join(root, 'src/fx/index.js')));
const core                  = await import(pathToFileURL(path.join(root, 'src/core/index.js')));
const              ids      = Object.keys(effects);
if (ids.length !== 244 || new Set(ids).size !== 244) throw new Error('Expected exactly 244 unique reference ids');
const catalog = ids.map(id => ({
                            id,
                            ...effects[id].meta,
                            implementation: effects[id].create ? 'simulation' : 'pure',
                            act: effects[id].act ?? null
                        }));
fs.mkdirSync(out, {recursive: true});
fs.writeFileSync(
    path.join(out, 'catalog.json'),
    JSON.stringify({schema: 'eve.pixelfx-reference/v1', revision, categories, effects: catalog}, null, 2) + '\n');
if (flags.id) {
    const mod = effects[flags.id];
    if (!mod) throw new Error(`Unknown id ${flags.id}`);
    const opts        = flags.options ? JSON.parse(flags.options) : {};
    let   actorSource = null;
    if (flags.actor) {
        const actor = JSON.parse(fs.readFileSync(flags.actor, 'utf8').replace(/^\uFEFF/, ''));
        actorSource = actor.source;
        if (actor.source === 'hero' || actor.source === 'dummy')
            opts.actor = core.stockActor(actor.source === 'dummy' ? 2 : 1);
        else if (actor.source === 'custom')
            opts.actor = {
                mask: {w: actor.width, h: actor.height, data: Uint8Array.from(actor.mask)},
                pixels: Uint32Array.from(actor.pixels),
                x: actor.x,
                y: actor.y,
                w: actor.width,
                h: actor.height,
                k: actor.scale,
                head: actor.head,
                chest: actor.chest,
                eyes: actor.eyes
            };
        else if (actor.source === 'none')
            opts.actor = null;
        else
            throw new Error('Unknown test actor source');
    }
    const layer = flags.layer ?? 'both';
    if (!['both', 'back', 'front'].includes(layer)) throw new Error('Unknown layer');
    const [w, h, ox, oy] = flags.canvas ? JSON.parse(flags.canvas) : [...mod.meta.size, ...mod.meta.origin];
    if (![w, h, ox, oy].every(Number.isInteger) || w < 1 || h < 1 || w > 4096 || h > 4096 || Math.abs(ox) > 4096 ||
        Math.abs(oy) > 4096)
        throw new Error('Invalid canvas');
    const frame = core.createFrame(w, h, -ox, -oy);
    // Explicit absence exercises the source kernel's optional-body path. The upstream
    // convenience wrapper otherwise replaces null with its metadata stock actor.
    // Keep the kernel untouched and suppress only that default in this reference adapter.
    const referenceMod = actorSource === 'none' ? {...mod, meta: {...mod.meta, actor: 0}} : mod;
    const fx           = core.createFx(referenceMod, opts);
    const times        = flags.times ?
        JSON.parse(flags.times) :
        Array.from({length: sampleCount}, (_, i) => Math.round(i * mod.meta.duration * 120 / (sampleCount - 1)) / 120);
    if (!Array.isArray(times) || times.length < 1 || times.length > 4097)
        throw new Error('Expected 1..4097 sample times');
    if (times.some((t, i) => !Number.isFinite(t) || t < 0 || (i > 0 && t < times[i - 1])))
        throw new Error('Sample times must be finite, nonnegative and ordered');
    let   tick   = 0;
    const frames = [];
    for (let i = 0; i < times.length; ++i) {
        const time = times[i], target = Math.round(time * 120);
        if (Math.abs(target / 120 - time) > 1e-8)
            throw new Error('Reference samples must fall on 1/120-second boundaries');
        while (tick < target) {
            fx.update(1 / 120);
            ++tick;
        }
        core.clearFrame(frame);
        if (mod.create)
            fx.draw(frame, layer);
        else {
            // Pure effects take absolute time directly. Avoid accumulating 120-Hz rounding
            // into a different evaluation time than the caller asked us to compare.
            const p = core.painterFor(frame), o = core.resolveOptions(mod.meta, opts);
            if (actorSource === 'none') o.actor = null;
            p.place(o.x ?? 0, o.y ?? 0, !!o.flip, o.angle ?? 0);
            p.lightK = o.light ?? 1;
            const tt = mod.meta.loop ? time % mod.meta.duration : Math.min(time, mod.meta.duration);
            if (layer !== 'front' && mod.drawBack) {
                p.layer = 'back';
                mod.drawBack(p, tt, o);
            }
            if (layer !== 'back' && mod.draw) {
                p.layer = 'front';
                mod.draw(p, tt, o);
            }
        }
        const base = `${flags.id.replaceAll('/', '_')}_${i}`;
        for (const name of ['color', 'light']) {
            const bytes = Buffer.alloc(frame[name].length * 4);
            frame[name].forEach((pixel, j) => bytes.writeUInt32LE(pixel >>> 0, j * 4));
            fs.writeFileSync(path.join(out, `${base}_${name}.bin`), bytes);
        }
        const bodyTime     = mod.meta.loop ? time % mod.meta.duration : Math.min(time, mod.meta.duration);
        const actorVisible = core.actorAlphaOf(mod, bodyTime, fx.options) !== 0;
        frames.push({time, tick, actorVisible, color: `${base}_color.bin`, light: `${base}_light.bin`});
    }
    fs.writeFileSync(path.join(out, 'frames.json'),
                     JSON.stringify({
                         schema: 'eve.pixelfx-reference-frames/v1',
                         revision,
                         id: flags.id,
                         options: opts,
                         w,
                         h,
                         origin: [ox, oy],
                         frames
                     },
                                    null, 2) +
                         '\n');
    console.log(`${flags.id}: ${frames.length} raw dual-plane frames at fixed 120 Hz`);
}
console.log(`Reference inventory: ${ids.length} effects, ${categories.length} categories, revision ${revision}`);
