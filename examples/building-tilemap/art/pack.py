"""Pack generated RGBA masters without painting over the original pixels."""
from pathlib import Path
from PIL import Image,ImageDraw
root=Path(__file__).parent
atlas=Image.new('RGBA',(384,160))
for name,offset,width in [('cottage',0,136),('tower',144,72)]:
    im=Image.open(root/(name+'-master.png')).convert('RGBA')
    box=im.getchannel('A').point(lambda a: 255 if a>24 else 0).getbbox()
    assert im.getextrema()[3][0]==0, 'Master must have transparent alpha'
    im=im.crop(box)
    size=(width,round(im.height*width/im.width))
    im=im.resize(size,Image.Resampling.LANCZOS)
    atlas.alpha_composite(im,(offset,0))
    print(name,box,size)

# A small code-drawn dock schematic keeps the existing water-only demo usable.
dock=Image.new('RGBA',(96,69));draw=ImageDraw.Draw(dock)
points=[(32,0),(95,46),(64,68),(0,23)]
draw.polygon(points,fill=(123,89,50,255),outline=(63,43,28,255),width=2)
for i in range(1,8):
    t=i/8
    draw.line([(32*(1-t),23*t),(95-31*t,46+22*t)],fill=(73,49,30,255),width=1)
dock.save(root/'dock.png')


atlas.alpha_composite(dock,(224,0))
atlas.alpha_composite(dock.transpose(Image.Transpose.FLIP_LEFT_RIGHT),(224,80))
atlas.save(root/'buildings.png')
