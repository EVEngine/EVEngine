"""OpenRaster baseline I/O for the Azure master; no source-art regeneration.

The supported subset is full-canvas normal-alpha layers in isolated variant
groups. Unsupported edits are rejected before any export is published.
"""
from io import BytesIO
from pathlib import Path
import xml.etree.ElementTree as ET
import zipfile
import numpy as np
from PIL import Image, ImageDraw

SIZE = (768, 1536)
SLOTS = {'body':['base'], 'hands':['base'], 'face-base':['base'],
         'expression':['neutral','happy','shy','angry','sad','surprised'],
         'hair-back':['straight','waves','halfup'], 'hair-front':['straight','waves','halfup'],
         'shirt':['ivory','blue'], 'shirt-back':['ivory','blue'], 'shirt-sleeves':['ivory','blue'], 'jacket-back':['navy','ivory'], 'jacket':['navy','ivory'],
         'skirt':['navy','blue'], 'accessory':['star']}
OCCLUSIONS = {'navy':['shirt-sleeves']}


def png(image):
    out = BytesIO()
    image.save(out, format='PNG')
    return out.getvalue()


def combine(parts):
    result = Image.new('RGBA', SIZE)
    for image in parts:
        result = Image.alpha_composite(result, image)
    return result


def write_master(path, groups, anchors):
    image = ET.Element('image', {'version':'0.0.6','w':str(SIZE[0]),'h':str(SIZE[1]),'name':'Azure / '+path.stem})
    stack = ET.SubElement(image,'stack')
    merged = combine(part for g in groups if g['selected'] for part in g['parts'].values())
    with zipfile.ZipFile(path,'w') as archive:
        archive.writestr('mimetype','image/openraster',compress_type=zipfile.ZIP_STORED)
        guides=Image.new('RGBA',SIZE)
        draw=ImageDraw.Draw(guides)
        draw.line((384,0,384,1536),fill='#37babb',width=2)
        for y,label in [(100,'SCALP'),(300,'SHOULDER'),(550,'WAIST'),(700,'WRIST'),(1400,'GROUND')]:
            draw.line((0,y,768,y),fill='#37babb',width=1)
            draw.text((16,y+5),label,fill='#167778')
        for key,item in anchors.items():
            if key.startswith(path.stem+'.shirt.ivory'):
                for x,y in item['target']:
                    draw.ellipse((x-4,y-4,x+4,y+4),fill='#ec617e')
        gn=ET.SubElement(stack,'stack',{'name':'_guides','visibility':'hidden','isolation':'isolate'})
        ET.SubElement(gn,'layer',{'name':'Pose anchors - hide before export','src':'data/guides.png'})
        archive.writestr('data/guides.png',png(guides))
        for group in reversed(groups):
            node = ET.SubElement(stack,'stack',{'name':group['slot']+'.'+group['variant'],
                'visibility':'visible' if group['selected'] else 'hidden','opacity':'1.0',
                'composite-op':'svg:src-over','isolation':'isolate'})
            for name, part in reversed(list(group['parts'].items())):
                src = 'data/'+group['slot']+'-'+group['variant']+'-'+name+'.png'
                ET.SubElement(node,'layer',{'name':name,'src':src,'x':'0','y':'0',
                    'opacity':'1.0','visibility':'visible','composite-op':'svg:src-over'})
                archive.writestr(src,png(part),compress_type=zipfile.ZIP_STORED)
        archive.writestr('stack.xml',ET.tostring(image,encoding='utf-8',xml_declaration=True))
        archive.writestr('mergedimage.png',png(merged))
        thumb=merged.copy();thumb.thumbnail((256,256))
        archive.writestr('Thumbnails/thumbnail.png',png(thumb))


def read_master(path):
    with zipfile.ZipFile(path) as archive:
        if archive.read('mimetype') != b'image/openraster':
            raise ValueError('Not an OpenRaster archive: '+str(path))
        root = ET.fromstring(archive.read('stack.xml'))
        size = int(root.attrib['w']),int(root.attrib['h'])
        if size != SIZE:
            raise ValueError('Master canvas must remain 768 x 1536')
        groups=[]; seen=set()
        for node in reversed(list(root.find('stack'))):
            name=node.attrib.get('name','')
            if name.startswith('_'):
                if node.attrib.get('visibility') != 'hidden':
                    raise ValueError('Authoring guides must be hidden')
                continue
            if node.tag!='stack' or '.' not in name or name in seen:
                raise ValueError('Expected unique slot.variant group: '+name)
            seen.add(name)
            slot,variant=name.split('.',1)
            if slot not in SLOTS or variant not in SLOTS[slot]:
                raise ValueError('Unknown slot/variant: '+name)
            if node.attrib.get('composite-op','svg:src-over')!='svg:src-over' or float(node.attrib.get('opacity',1))!=1:
                raise ValueError('Variant groups must use normal blend and full opacity')
            parts={}
            for layer in reversed(list(node)):
                if layer.tag!='layer' or layer.attrib.get('composite-op','svg:src-over')!='svg:src-over':
                    raise ValueError('Only normal PNG painting layers are supported')
                if layer.attrib.get('visibility','visible')!='visible':
                    continue
                partname=layer.attrib.get('name','')
                if not partname or partname in parts:
                    raise ValueError('Layer names must be unique within a variant')
                src=layer.attrib['src']
                if not src.startswith('data/') or '..' in Path(src).parts:
                    raise ValueError('Unsafe layer source')
                part=Image.open(BytesIO(archive.read(src))).convert('RGBA')
                alpha=float(layer.attrib.get('opacity',1))
                if not 0<=alpha<=1:
                    raise ValueError('Invalid layer opacity')
                if alpha!=1:
                    part.putalpha(part.getchannel('A').point(lambda a:round(a*alpha)))
                full=Image.new('RGBA',SIZE)
                full.alpha_composite(part,(int(layer.attrib.get('x',0)),int(layer.attrib.get('y',0))))
                parts[partname]=full
            if not parts:
                raise ValueError('Empty variant: '+name)
            groups.append(dict(slot=slot,variant=variant,parts=parts,selected=node.attrib.get('visibility','visible')=='visible'))
        expected={s+'.'+v for s,vs in SLOTS.items() for v in vs}
        if seen!=expected:
            raise ValueError('Missing variants: '+str(expected-seen))
        return groups


def render(groups, recipe=None):
    parts=[]
    for group in groups:
        selected=group['selected'] if recipe is None else recipe.get(group['slot'])==group['variant']
        if selected:
            parts.extend(group['parts'].values())
    return combine(parts)


def recipe(hair='straight',expression='neutral',shirt='ivory',jacket='navy',skirt='navy',accessory=True):
    selected = {'body':'base','hands':'base','face-base':'base','expression':expression,
            'hair-back':hair,'hair-front':hair,'shirt':shirt,'shirt-back':shirt,
            'shirt-sleeves':shirt,'jacket':jacket,
            'jacket-back':jacket,'skirt':skirt,'accessory':'star' if accessory else None}
    for slot in OCCLUSIONS.get(jacket,[]):
        selected[slot]=None
    return selected
