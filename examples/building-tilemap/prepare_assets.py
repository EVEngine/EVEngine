"""Pack locally supplied C14 diamond PNGs for the building-tilemap example.
Usage: python prepare_assets.py "path/to/C14 folder"
Requires Pillow. Originals remain untouched; source mapping is written to assets.
"""
from pathlib import Path
import json, sys
from PIL import Image, ImageDraw


def prepare(source):
    source = Path(source)
    files = sorted(source.glob('*.png'), key=lambda p: int(p.stem))
    if not files:
        raise ValueError('No numbered C14 PNG files found')
    out = Path(__file__).parent / 'assets'
    out.mkdir(exist_ok=True)
    width, height, columns = 64, 46, 10
    variant_sources = [11,12,13,2,3,37,69,72,73,7,8,10,14,108,114,116]
    total=len(files)+len(variant_sources)*4
    atlas = Image.new('RGBA', (columns * width, ((total+columns-1)//columns)*height))
    entries = []
    for index, path in enumerate(files):
        with Image.open(path) as source_image:
            image = source_image.convert('RGBA')
            bounds = image.getbbox()
            if bounds is None:
                raise ValueError(f'Empty source: {path}')
            tile = image.crop(bounds).resize((width,height),Image.Resampling.LANCZOS)
            atlas.paste(tile, ((index%columns)*width, (index//columns)*height))
            entries.append({'gid':index+1,'source':path.name,'sourceSize':list(image.size),'crop':list(bounds)})
    variants={}
    for source_id in variant_sources:
        with Image.open(source/f'{source_id}.png') as im:
            im=im.convert('RGBA');tile=im.crop(im.getbbox()).resize((width,height),Image.Resampling.LANCZOS)
        variants[source_id]=[]
        for flip in [None,Image.Transpose.FLIP_LEFT_RIGHT,Image.Transpose.FLIP_TOP_BOTTOM,Image.Transpose.ROTATE_180]:
            index=len(entries)
            atlas.paste(tile if flip is None else tile.transpose(flip),((index%columns)*width,(index//columns)*height))
            variants[source_id].append(index+1)
            entries.append({'gid':index+1,'source':f'{source_id}.png','mirror':str(flip)})
    atlas.save(out/'c14-atlas.png')
    # Geometric selection overlay, aligned to the exact projection vertices.
    outline = Image.new('RGBA',(width,height))
    ImageDraw.Draw(outline).polygon([(32,0),(63,23),(32,45),(0,23)],fill=(255,255,255,24),outline=(255,255,255,255),width=2)
    outline.save(out/'cursor.png')
    (out/'provenance.json').write_text(json.dumps({'source':'User supplied C14 45-degree ground collection','tileSize':[width,height],'entries':entries},indent=2))
    # Actual file 66 is absent; never assume filename+1 is the packed GID.
    mapping={int(p.stem):i+1 for i,p in enumerate(files)}
    (out/'catalog.nut').write_text('c14Gids <- {'+','.join('[%d]=%d'%pair for pair in mapping.items())+'};\n'+'c14Variants <- {'+','.join('[%d]=[%s]'%(key,','.join(map(str,values))) for key,values in variants.items())+'};\n')

if __name__=='__main__':
    prepare(sys.argv[1])
