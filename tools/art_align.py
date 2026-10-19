"""Finds where each supplied hat colour layer sits relative to its base sprite by matching the supplied preview composites.
Writes art/hats/alignment.json. usage: python3 tools/art_align.py (from the repo root)"""
import numpy as np, json
from PIL import Image
import os; os.chdir('art/hats')
cn={'boundy':'bouncy-hat','sharkfin':'shark-fin-hat','party':'party-hat','top':'top-hat','pirate':'pirate-hat','nurse':'nurse-hat','happy':'happy-hat','parasol':'parasol-hat'}
rng=np.random.default_rng(1)
out={}
for n,c in cn.items():
    B=Image.open(n+'.png').convert('RGBA'); C=Image.open('color-layer/%s-color.png'%c).convert('RGBA'); P=Image.open('color-layer/preview/%s-composite.png'%c).convert('RGBA')
    Pa=np.array(P).astype(int); Ca=np.array(C).astype(int)
    # A: colour layer offset inside the composite
    ys,xs=np.where(Ca[:,:,3]==255); idx=rng.choice(len(ys),min(600,len(ys)),replace=False); ys,xs=ys[idx],xs[idx]; cols=Ca[ys,xs,:3]
    best=(-1,0,0)
    for oy in range(0,P.height-C.height+1):
        for ox in range(0,P.width-C.width+1):
            m=(np.abs(Pa[ys+oy,xs+ox,:3]-cols).sum(1)<12).sum()
            if m>best[0]: best=(m,ox,oy)
    _,cx,cy=best
    # B: base scale / offset from silhouette outside the colour layer
    mask=np.ones((P.height,P.width),bool); mask[cy:cy+C.height,cx:cx+C.width]=Ca[:,:,3]==0
    Pal=(Pa[:,:,3]>40)&mask
    bestb=(1e18,1,0,0)
    for s in np.arange(1.5,3.6,0.04):
        bw,bh=int(round(B.width*s)),int(round(B.height*s))
        if bw>P.width or bh>P.height: continue
        Bs=np.array(B.resize((bw,bh),Image.Resampling.LANCZOS))[:,:,3]>40
        for oy in range(0,P.height-bh+1,2):
            for ox in range(0,P.width-bw+1,2):
                reg=mask[oy:oy+bh,ox:ox+bw]
                sc=((Pal[oy:oy+bh,ox:ox+bw]!=Bs)&reg).sum()+ (Pal.sum()-(Pal[oy:oy+bh,ox:ox+bw]).sum())
                if sc<bestb[0]: bestb=(sc,s,ox,oy)
    _,s0,bx0,by0=bestb
    best2=(1e18,s0,bx0,by0)
    for s in np.arange(s0-0.06,s0+0.0601,0.01):
        bw,bh=int(round(B.width*s)),int(round(B.height*s))
        if bw>P.width or bh>P.height: continue
        Bs=np.array(B.resize((bw,bh),Image.Resampling.LANCZOS))[:,:,3]>40
        for oy in range(max(0,by0-4),min(P.height-bh,by0+4)+1):
            for ox in range(max(0,bx0-4),min(P.width-bw,bx0+4)+1):
                reg=mask[oy:oy+bh,ox:ox+bw]
                sc=((Pal[oy:oy+bh,ox:ox+bw]!=Bs)&reg).sum()+(Pal.sum()-(Pal[oy:oy+bh,ox:ox+bw]).sum())
                if sc<best2[0]: best2=(sc,s,ox,oy)
    _,s,bx,by=best2
    out[n]=dict(cx=int(cx),cy=int(cy),s=float(s),bx=int(bx),by=int(by),match=int(best[0]),npts=len(ys))
    print(n,out[n],flush=True)
json.dump(out,open('alignment.json','w'),indent=1)
