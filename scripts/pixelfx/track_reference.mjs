// Reference-only poseAt/skeleton evaluation; executes the actual pinned effect track.
import {execFileSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';

const f = {};
for (let i = 2; i < process.argv.length; i += 2) f[process.argv[i].slice(2)] = process.argv[i + 1];
if (!f.upstream || !f.effect || !f.times || !f.out)
    throw new Error('Expected --upstream PATH --effect ID --times FILE --out FILE');
const root = path.resolve(f.upstream), pin = 'a84159688af3ca37a4be05a0d422b94451b2e24e';
if (execFileSync('git', ['-C', root, 'rev-parse', 'HEAD'], {encoding: 'utf8'}).trim() !== pin)
    throw new Error('Unpinned track reference');
// Refuse modified tracked source even when HEAD still names the expected pin.
execFileSync('git', ['-C', root, 'diff', '--quiet', 'HEAD'], {encoding: 'utf8'});
const {effects}             = await             import(pathToFileURL(path.join(root, 'src/fx/index.js')));
const {poseAt}              = await              import(pathToFileURL(path.join(root, 'src/core/act.js')));
const {skeleton, shortfall} = await import(pathToFileURL(path.join(root, 'src/core/rig.js')));
const                               track = effects[f.effect]?.act?.poses;
if (!track) throw new Error('Effect has no authored track');
const times  = JSON.parse(fs.readFileSync(f.times, 'utf8').replace(/^\uFEFF/, ''));
const frames = times.map(time => {
    const pose = poseAt(track, time);
    return {
        time,
        pose,
        presence: Object.keys(pose).filter(k => pose[k] !== undefined),
        undefinedProperties: Object.keys(pose).filter(k => pose[k] === undefined),
        joints: skeleton(pose).joints,
        shortfall: shortfall(pose)
    };
});
fs.writeFileSync(
    f.out, JSON.stringify({schema: 'eve.pixelfx.track-probe/v1', revision: pin, effect: f.effect, frames}) + '\n');
console.log(`${f.effect}: ${frames.length} upstream track samples`);
