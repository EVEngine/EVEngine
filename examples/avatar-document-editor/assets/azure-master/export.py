"""Validate edited ORA masters, then publish immutable runtime PNG exports.

Only this exporter reads authoring masters for the demo. The content-addressed
export completes before manifest.nut is atomically replaced, so a failed export
cannot leave the running demo referencing an incomplete set of layers.
"""
import hashlib
import itertools
import json
import os
from pathlib import Path
from PIL import Image, ImageDraw
import numpy as np
from master_io import SIZE, SLOTS, OCCLUSIONS, read_master, render, recipe, combine, png

ROOT=Path(__file__).resolve().parent


def quote(value):
    return json.dumps(value,ensure_ascii=False)


def export():
    masters={pose:read_master(ROOT/(pose+'.ora')) for pose in ['front','greeting']}
    fingerprint=hashlib.sha256()
    for pose in masters:
        fingerprint.update((ROOT/(pose+'.ora')).read_bytes())
    fingerprint.update(Path(__file__).read_bytes())
    fingerprint.update((ROOT/'master_io.py').read_bytes())
    revision=fingerprint.hexdigest()[:16]
    out=ROOT/'exports'/revision
    # All parsing and contract validation precedes writes or publication.
    report={'schema':'azure.master-verification','version':1,'revision':revision,
            'masterFormat':'OpenRaster 0.0.6','canvas':list(SIZE),'poses':{},
            'originalConceptPixelEquality':'not claimed; these are reconstructed editable masters'}
    for pose,groups in masters.items():
        pixels=render(groups,{'body':'base','hands':'base'})
        reference=Image.open(ROOT/(pose+'-body-reference.png')).convert('RGBA')
        # Body art may be deliberately edited later: this test guards the initial
        # reconstruction and must not silently accept body deletion as alignment.
        delta=np.abs(np.asarray(pixels,dtype=int)-np.asarray(reference,dtype=int))
        if delta.max()!=0:
            raise ValueError(pose+': complete-body preservation failed')
        report['poses'][pose]={'bodyPreservedPixelExactly':True,'variantGroups':len(groups),
                               'rasterLayers':sum(len(g['parts']) for g in groups)}
    out.mkdir(parents=True,exist_ok=True)
    manifest={}
    for pose,groups in masters.items():
        target=out/pose;target.mkdir(exist_ok=True)
        entries=[]
        for z,group in enumerate(groups):
            for part,image in group['parts'].items():
                name=group['slot']+'.'+group['variant']+'.'+part
                path=target/(name+'.png')
                path.write_bytes(png(image))
                entries.append(dict(name=name,slot=group['slot'],variant=group['variant'],z=z,
                                    file='assets/azure-master/exports/'+revision+'/'+pose+'/'+path.name))
        manifest[pose]=entries
        for look,selection in [('academy',recipe()),('formal',recipe(hair='halfup',shirt='blue',jacket='ivory',skirt='blue'))]:
            image=render(groups,selection)
            image.save(target/('complete-'+look+'.png'))
            # Read actual exported pieces, not the in-memory authoring image.
            rebuilt=combine(Image.open(ROOT.parents[1]/e['file']).convert('RGBA') for e in entries
                            if selection.get(e['slot'])==e['variant'])
            if not np.array_equal(np.asarray(image),np.asarray(rebuilt)):
                raise ValueError(pose+'/'+look+': exported layer reconstruction differs')
        render(groups).save(ROOT/(pose+'-preview.png'))
        # Eight wardrobe combinations, including both choices in every garment slot.
        matrix=Image.new('RGB',(4*256,2*544),'#e9edf4');draw=ImageDraw.Draw(matrix)
        for i,(shirt,jacket,skirt) in enumerate(itertools.product(SLOTS['shirt'],SLOTS['jacket'],SLOTS['skirt'])):
            image=render(groups,recipe(shirt=shirt,jacket=jacket,skirt=skirt))
            image=image.resize((256,512),Image.Resampling.LANCZOS)
            x=(i%4)*256;y=(i//4)*544
            matrix.paste(image,(x,y),image)
            draw.text((x+10,y+516),shirt+' / '+jacket+' / '+skirt,fill='#283d5e')
        matrix.save(ROOT/(pose+'-wardrobe.jpg'),quality=94)
        faces=Image.new('RGB',(6*220,330),'#e9edf4')
        for i,expression in enumerate(SLOTS['expression']):
            image=render(groups,recipe(expression=expression)).crop((285,80,487,360)).resize((220,305),Image.Resampling.LANCZOS)
            faces.paste(image,(i*220,0),image)
            ImageDraw.Draw(faces).text((i*220+12,310),expression,fill='#283d5e')
        faces.save(ROOT/(pose+'-expressions.jpg'),quality=96)
        report['poses'][pose]['defaultExportPixelEquality']=True
    manifest_json={'schema':'azure.runtime-layers','version':1,'unknownFields':'reject',
                   'revision':revision,'canvas':list(SIZE),'layers':manifest,
                   'occlusion':OCCLUSIONS}
    (out/'manifest.json').write_text(json.dumps(manifest_json,indent=2)+'\n',encoding='utf-8')
    lines=['// Generated exclusively from the ORA masters. Do not edit.',
           'azureMasterRevision <- '+quote(revision)+';', 'azureMasterLayers <- {']
    for pose,entries in manifest.items():
        lines.append('['+quote(pose)+'] = [')
        for e in entries:
            lines.append('  { '+', '.join(k+' = '+(str(v) if isinstance(v,int) else quote(v)) for k,v in e.items())+' },')
        lines.append('],')
    lines+=['};','azureMasterReferenceRoot <- '+quote('assets/azure-master/exports/'+revision+'/')+';']
    lines+=['azureMasterOcclusions <- {']
    for jacket,slots in OCCLUSIONS.items():
        lines.append('['+quote(jacket)+'] = ['+', '.join(quote(s) for s in slots)+'],')
    lines+=['};']
    temp=ROOT/'manifest.nut.tmp'
    temp.write_text('\n'.join(lines)+'\n',encoding='utf-8')
    os.replace(temp,ROOT/'manifest.nut')
    (ROOT/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))
    return report


if __name__=='__main__':
    export()
