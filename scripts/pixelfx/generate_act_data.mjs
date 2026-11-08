// Authoring-only metadata import; no JavaScript is used by the native runtime.
import {execFileSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';

const flags = {};
for (let i = 2; i < process.argv.length; i += 2) flags[process.argv[i].slice(2)] = process.argv[i + 1];
if (!flags.upstream || !flags.out) throw new Error('Expected --upstream PATH --out FILE');
const upstream = path.resolve(flags.upstream), pin = 'a84159688af3ca37a4be05a0d422b94451b2e24e';
if (execFileSync('git', ['-C', upstream, 'rev-parse', 'HEAD'], {encoding: 'utf8'}).trim() !== pin)
    throw new Error('Unpinned source');
execFileSync('git', ['-C', upstream, 'diff', '--quiet', 'HEAD'], {encoding: 'utf8'});
const {effects}              = await import(pathToFileURL(path.join(upstream, 'src/fx/index.js')));
const                   root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..'),
                        dir = path.join(root, 'src/modules/stylize/pixelfx'), kinds = new Map();
for (const name of fs.readdirSync(dir).filter(n => n.endsWith('.cpp')).sort()) {
    const s = fs.readFileSync(path.join(dir, name), 'utf8');
    for (const m of s.matchAll(/PixelFxKind::(\w+),\s*"([^" ]+\/[^" ]+)"/g)) {
        if (kinds.has(m[2])) throw new Error(`Duplicate compiled effect ${m[2]}`);
        kinds.set(m[2], m[1]);
    }
}
if (Object.keys(effects).length !== 244 || kinds.size !== 244)
    throw new Error('Expected 244 source and native definitions');
let text = `// Authored Act data adapted from patch-gg/pixelfx, MIT (c) 2026 Patch.\n// Pinned source ${
    pin}; regenerate with scripts/pixelfx/generate_act_data.mjs.\n#include "stylize/pixelfx/ActData.h"\nnamespace eve::stylize::pixelfx {\nstd::string_view actSourceData(PixelFxKind kind) {\n    switch(kind) {\n`;
let count = 0;
for (const [id, fx] of Object.entries(effects)) {
    if (!kinds.has(id)) throw new Error(`Missing native kind ${id}`);
    if (!fx.act) continue;
    const json = JSON.stringify(fx.act);
    if (json.includes(')act"')) throw new Error('Raw literal terminator in source metadata');
    text += `        case PixelFxKind::${kinds.get(id)}: return R"act(${json})act";\n`;
    count++;
}
text += '        default: return "{}";\n    }\n}\n}  // namespace eve::stylize::pixelfx\n';
fs.writeFileSync(flags.out, text);
console.log(`Imported ${count} authored Acts for 244 compiled effects`);
