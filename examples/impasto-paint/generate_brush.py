from pathlib import Path
import numpy as np
from PIL import Image
rng=np.random.default_rng(63821);n=1024
normal=np.zeros((n,n,2),np.float32);tone=np.full((n,n),.5,np.float32);mask=np.zeros((n,n),np.float32)
for layer,count in [(0,340),(1,700)]:
 for _ in range(count):
  cx,cy=rng.integers(0,n,2);w=rng.uniform(95,240) if layer==0 else rng.uniform(20,85);h=rng.uniform(16,52) if layer==0 else rng.uniform(5,19)
  a=rng.normal(0,.23);r=int(w*.65+h);yy,xx=np.mgrid[-r:r,-r:r];u=(xx*np.cos(a)+yy*np.sin(a))/w;v=(-xx*np.sin(a)+yy*np.cos(a))/h
  tooth=.035*np.sin(v*65+rng.uniform(0,6))+.025*np.sin(v*132)
  edge=np.minimum(.5-np.abs(u)+tooth,.47-np.abs(v)+.025*np.sin(u*62))
  alpha=np.clip(edge*22,0,1);alpha*=np.clip(.8+.24*np.sin(v*170+rng.uniform(0,6)),0,1)
  ys=(yy+cy)%n;xs=(xx+cx)%n
  if layer==0:
   angle=rng.uniform(0,np.pi*2);strength=rng.uniform(.3,.92);vec=np.array([np.cos(angle),np.sin(angle)])*strength
   normal[ys,xs]=normal[ys,xs]*(1-alpha[:,:,None])+vec*alpha[:,:,None]
   tone[ys,xs]=tone[ys,xs]*(1-alpha)+rng.uniform(.08,.95)*alpha
  else:mask[ys,xs]=mask[ys,xs]*(1-alpha)+rng.uniform(.0,1.)*alpha
packed=np.dstack([normal*.5+.5,tone,mask]);out=Path(__file__).resolve().parent/'assets';out.mkdir(exist_ok=True)
Image.fromarray(np.uint8(np.clip(packed,0,1)*255),'RGBA').save(out/'brush-packed.png')
print('packed brush generated, seed 63821')
