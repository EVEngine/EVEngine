// Seeded geometry from EZ-Tree (MIT), adapted for EVEngine glTF import.
// npm install && npm run build; no npm or JavaScript runtime is needed by the game.
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { Tree } from './vendor/ez-tree/tree.js';
const root=path.dirname(fileURLToPath(import.meta.url));
const out=path.join(root,'../assets/garden');
for(let variant=0;variant<3;variant++) {
 const preset=JSON.parse(fs.readFileSync(path.join(root,'presets',variant===2?'aspen_medium.json':'oak_medium.json'),'utf8'));
 preset.seed=902+variant*137; preset.leaves.count=26; preset.leaves.size*=1.35;
 const tree=new Tree(); tree.loadFromJson(preset);
 let height=0;
 for(const mesh of [tree.branchesMesh,tree.leavesMesh]) {
  const p=mesh.geometry.attributes.position;
  for(let i=0;i<p.count;i++) height=Math.max(height,p.getY(i));
 }
 const scale=(7.0+variant*0.6)/height;
 const buffers=[],views=[],accessors=[],meshes=[]; let byteOffset=0;
 function stream(array,type,components,target,min,max) {
  const data=Buffer.from(array.buffer,array.byteOffset,array.byteLength);
  const view=views.length;views.push({buffer:0,byteOffset,byteLength:data.length,target});
  buffers.push(data);byteOffset+=data.length;
  const index=accessors.length;const entry={bufferView:view,componentType:type,count:array.length/components,type:components===1?'SCALAR':`VEC${components}`};
  if(min) {entry.min=min;entry.max=max;} accessors.push(entry);return index;
 }
 for(const [i,mesh] of [tree.branchesMesh,tree.leavesMesh].entries()) {
  const g=mesh.geometry;const positions=new Float32Array(g.attributes.position.array);let min=[Infinity,Infinity,Infinity],max=[-Infinity,-Infinity,-Infinity];
  positions.forEach((v,j)=>{positions[j]=v*scale;min[j%3]=Math.min(min[j%3],positions[j]);max[j%3]=Math.max(max[j%3],positions[j]);});
  const p=stream(positions,5126,3,34962,min,max);
  const n=stream(new Float32Array(g.attributes.normal.array),5126,3,34962);
  const uv=stream(new Float32Array(g.attributes.uv.array),5126,2,34962);
  const indices=stream(new Uint32Array(g.index.array),5125,1,34963);
  meshes.push({name:i===0?'bark':'leaves',primitives:[{attributes:{POSITION:p,NORMAL:n,TEXCOORD_0:uv},indices,material:i}]});
 }
 const doc={asset:{version:'2.0',generator:'EZ-Tree dcf309bd / modern-balcony exporter'},scene:0,scenes:[{nodes:[0,1]}],nodes:[{mesh:0},{mesh:1}],meshes,
  materials:[{name:'bark',pbrMetallicRoughness:{baseColorFactor:[0.36,0.27,0.17,1],metallicFactor:0,roughnessFactor:1}},{name:'leaves',doubleSided:true,alphaMode:'MASK',alphaCutoff:0.35,pbrMetallicRoughness:{baseColorFactor:[1,1,1,1],metallicFactor:0,roughnessFactor:1}}],
  buffers:[{uri:`tree-${variant}.bin`,byteLength:byteOffset}],bufferViews:views,accessors};
 fs.writeFileSync(path.join(out,`tree-${variant}.gltf`),JSON.stringify(doc));
 fs.writeFileSync(path.join(out,`tree-${variant}.bin`),Buffer.concat(buffers));
 console.log(`tree ${variant}: ${(byteOffset/1024).toFixed(0)} KiB, grounded at y=0`);
}
