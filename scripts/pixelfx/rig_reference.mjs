// Reference-only: executes public APIs from the pinned upstream rig implementation.
import {execFileSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';

const flags = {};
for (let i = 2; i < process.argv.length; i += 2) flags[process.argv[i].slice(2)] = process.argv[i + 1];
if (!flags.upstream || !flags.pose || !flags.out) throw new Error('Expected --upstream PATH --pose FILE --out FILE');
const root = path.resolve(flags.upstream), pin = 'a84159688af3ca37a4be05a0d422b94451b2e24e';
if (execFileSync('git', ['-C', root, 'rev-parse', 'HEAD'], {encoding: 'utf8'}).trim() !== pin)
    throw new Error('Unpinned rig source');
// Refuse modified tracked source even when HEAD still names the expected pin.
execFileSync('git', ['-C', root, 'diff', '--quiet', 'HEAD'], {encoding: 'utf8'});
const {posed, posedParts, skeleton, shortfall} = await import(pathToFileURL(path.join(root, 'src/core/rig.js')));
const doc                                      = JSON.parse(fs.readFileSync(flags.pose, 'utf8').replace(/^\uFEFF/, ''));
if (doc.schema !== 'eve.pixelfx.pose' || doc.version !== 1) throw new Error('Expected pose v1 fixture');
const q = {...doc.fields};
if (q.weapon === 'none') delete q.weapon;
if (q.offWeapon === 'none') delete q.offWeapon;
const r = posed(q), s = skeleton(q);
const parts = posedParts(q).map(
    p => ({name: p.name, part: p.part, xs: [...p.xs], ys: [...p.ys], color: [...p.color], cx: p.cx, cy: p.cy, z: p.z}));
const out = {
    parts,
    schema: 'eve.pixelfx.rig-probe/v1',
    revision: pin,
    joints: s.joints,
    shortfall: shortfall(q),
    xs: [...r.xs],
    ys: [...r.ys],
    color: [...r.color],
    src: [...r.src],
    ink: [...r.ink],
    bounds: [r.x0, r.y0, r.x1, r.y1],
    z: r.z
};
fs.writeFileSync(flags.out, JSON.stringify(out) + '\n');
console.log(`Upstream rig: ${r.n} sparse pixels`);
