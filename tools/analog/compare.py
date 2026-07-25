import numpy as np, subprocess, os
from scipy.signal import firls
E=dict(os.environ, VK_DRIVER_FILES="/usr/share/vulkan/icd.d/lvp_icd.json")
def boxc(L,n=3):
    h=np.ones(L)/L; o=h.copy()
    for _ in range(n-1): o=np.convolve(o,h)
    return o
CH=boxc(11)
def cv(x,h): return np.convolve(x,h,'same')
CFG={'NTSC':dict(spv='reference_chain_ntsc.spv',fsc=315e6/88,mul=15,adv=0.5,gain=0.5,pal=False,
      M=np.array([[.299,.587,.114],[.5959,-.2746,-.3213],[.2115,-.5227,.3112]]),
      lw=(6.0e6,7.5e6),lc=(4.2e6,6.0e6),lr=(3.0e6,4.2e6)),
     'PAL':dict(spv='reference_chain_pal.spv',fsc=4433618.75,mul=12,adv=0.75,gain=1.0,pal=True,
      M=np.vstack([[.299,.587,.114],
                   .492111*(np.array([0,0,1.])-np.array([.299,.587,.114])),
                   .877283*(np.array([1.,0,0])-np.array([.299,.587,.114]))]),
      lw=(6.0e6,7.5e6),lc=(5.0e6,6.8e6),lr=(3.0e6,4.5e6))}
BEAT={'NTSC':(3/175,0.5),'PAL':(0.020043470,0.248)}
def gpu(reg,img,div,x1,cable):
    c=CFG[reg]; L,PXW,_=img.shape
    s=np.zeros((L,PXW,4),np.float32); s[...,:3]=img
    p=subprocess.run(["./runner",str(PXW),str(L),str(div),str(x1),str(c['mul']and 1.0/c['mul']),
                      str(c['adv']),"0",str(int(c['pal'])),str(1 if cable==1 else 0),str(float(cable))],
                     input=s.tobytes(),capture_output=True,env=dict(E,SPV=c['spv']))
    if p.returncode: print(p.stderr.decode()[:400]); raise SystemExit(1)
    return np.frombuffer(p.stdout,np.float32).reshape(L,PXW,4)[...,:2]
def ref(reg,img,div,x1,cable):
    c=CFG[reg]; M=c['M']; Mi=np.linalg.inv(M); R=c['fsc']*c['mul']
    band = c['lw'] if cable==1 else (c['lr'] if cable==3 else c['lc'])
    L95=firls(95,[0,band[0],band[1],R/2],[1,1,0,0],fs=R)
    lines,px,_=img.shape; W=px*div
    hold=np.repeat(img,div,axis=1); yc=hold@M.T
    for l in range(lines):
        yc[l,:,0]=cv(yc[l,:,0],L95); yc[l,:,1]=cv(yc[l,:,1],CH); yc[l,:,2]=cv(yc[l,:,2],CH)
    n=np.arange(W)
    ph=np.stack([2*np.pi*np.mod(np.mod((x1+n)/c['mul'],1.)+np.mod(l*c['adv'],1.),1.) for l in range(lines)])
    sgn=np.array([[1.0 if (l%2==0 or not c['pal']) else -1.0] for l in range(lines)])
    C = yc[...,1]*np.sin(ph)+sgn*yc[...,2]*np.cos(ph) if c['pal'] else yc[...,1]*np.cos(ph)+yc[...,2]*np.sin(ph)
    if cable==3:
        br,bl=BEAT[reg]; mag=np.hypot(yc[...,1],yc[...,2])
        bph=np.stack([2*np.pi*np.mod(n*br+np.mod(l*bl,1.),1.) for l in range(lines)])
        C=C+0.18*mag*np.cos(bph)
    Y=yc[...,0]; comp=Y+C
    out=np.zeros((lines,W,3))
    for l in range(lines):
        if cable==1: y,est=Y[l],C[l]
        else:
            est=c['gain']*(comp[l]-0.5*(comp[min(l+1,lines-1)]+comp[max(l-1,0)]))
            y=comp[l]
        if c['pal']: a=cv(2*est*np.sin(ph[l]),CH); b=cv(sgn[l,0]*2*est*np.cos(ph[l]),CH)
        else:        a=cv(2*est*np.cos(ph[l]),CH); b=cv(2*est*np.sin(ph[l]),CH)
        if cable!=1:
            rec = a*np.sin(ph[l])+sgn[l,0]*b*np.cos(ph[l]) if c['pal'] else a*np.cos(ph[l])+b*np.sin(ph[l])
            y = comp[l]-rec
        out[l]=np.stack([y,a,b],-1)@Mi.T
    return out.reshape(lines,px,div,3).mean(2)[...,:2]
rng=np.random.default_rng(21); worst={}
print("full-chain regression: GPU vs reference model, current shaders\n")
print(" region  cable       div   x1     max abs      RMS")
for reg in ('NTSC','PAL'):
    for cable,cn in ((1,'S-Video  '),(2,'composite'),(3,'RF       ')):
        for px,div in ((220,8),(300,4),(240,5)):
            img=rng.random((16,px,3)); g0=int(np.ceil(115/div))+6
            x1 = 600.0 if reg=='NTSC' else 624.0
            a=gpu(reg,img,div,x1,cable); b=ref(reg,img,div,x1,cable)
            e=np.abs(a[4:12,g0:px-g0]-b[4:12,g0:px-g0])
            worst[reg]=max(worst.get(reg,0),e.max())
            if div==8 or (div==4 and cable==2):
                print(f" {reg:5s}  {cn}   {div:3d} {x1:5.0f}   {e.max():.3e}  {np.sqrt((e**2).mean()):.3e}")
print()
for k,v in worst.items(): print(f" worst across all {k} cases (3 cables x 3 modes): {v:.3e}")
