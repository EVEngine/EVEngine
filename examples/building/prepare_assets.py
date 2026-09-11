"""Extract the example's Cainos tiles from a local licensed Unity package.
Usage: python prepare_assets.py "path/to/Pixel Art Top Down - Basic.unitypackage"
Requires Pillow. Pixel crops retain the original artwork; buildings compose those crops.
"""
import sys, tarfile, io, json
from pathlib import Path
from PIL import Image

def prepare(package):
    sheets = {}
    with tarfile.open(package) as archive:
        members = {m.name: m for m in archive.getmembers()}
        for m in archive.getmembers():
            if not m.name.endswith('/pathname'): continue
            path = archive.extractfile(m).read().decode().splitlines()[0].split('\0')[0]
            if not path.endswith('.png'): continue
            asset = m.name.rsplit('/', 1)[0] + '/asset'
            if asset in members:
                sheets[Path(path).name] = Image.open(io.BytesIO(archive.extractfile(members[asset]).read())).convert('RGBA')
    out = Path(__file__).parent / 'assets'
    out.mkdir(exist_ok=True)
    Image.new('RGBA',(1,1),(255,255,255,255)).save(out/'pixel.png')
    def crop(sheet, box): return sheets['TX '+sheet+'.png'].crop(box)
    for n in range(8):
        crop('Tileset Grass', (n%8*32, 0, n%8*32+32, 32)).save(out/f'grass{n}.png')
    for n, box in enumerate([(0,128,32,160),(32,128,64,160),(64,128,96,160),(0,160,32,192),(32,160,64,192),(64,160,96,192)]):
        crop('Tileset Grass', box).save(out/f'road{n}.png')
    crop('Props',(96,32,128,64)).save(out/'crate.png')
    crop('Props',(160,152,192,192)).save(out/'barrel.png')
    crop('Struct',(32,32,96,128)).save(out/'dock.png')
    # Flat-roof stone cottage: stone courtyard roof, masonry facade, wooden door.
    house = Image.new('RGBA',(64,64))
    house.alpha_composite(crop('Tileset Wall',(32,192,96,256)),(0,0))
    house.alpha_composite(crop('Tileset Stone Ground',(0,0,64,32)),(0,0))
    house.alpha_composite(crop('Props',(96,76,128,124)).resize((20,30),Image.Resampling.NEAREST),(22,34))
    house.save(out/'house.png')
    stall=Image.new('RGBA',(32,32))
    stall.alpha_composite(crop('Props',(288,16,352,64)).resize((32,24),Image.Resampling.NEAREST),(0,8))
    stall.save(out/'stall.png')
    (out/'provenance.json').write_text(json.dumps({'source':'Cainos / Pixel Art Top Down - Basic','processing':'Lossless atlas crops; house and stall assembled from source regions.','generator':'../prepare_assets.py'},indent=2))

if __name__=='__main__': prepare(sys.argv[1])
