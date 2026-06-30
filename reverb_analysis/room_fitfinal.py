import numpy as np, subprocess, os, json, sys
sys.path.insert(0,'.'); import room_battery as rb
from numpy.fft import rfft,irfft,rfftfreq
W='/tmp/room_rebuild'; BIN=W+'/roomv2'; SR=48000
BANDS=[125,250,500,1000,2000,4000,8000]; TP=[10,25,50,90,150,250,400,650,1000,1500]
_M=None
def surface(m):
    global _M
    ff=rfftfreq(len(m),1/SR); X=rfft(m); k=int(.024*SR)
    gpk=np.convolve(m**2,np.ones(k)/k,'same'); peak=gpk.max()+1e-20
    if _M is None or _M[0]!=len(m):
        _M=(len(m),[((ff>=fc/np.sqrt(2))&(ff<fc*np.sqrt(2))) for fc in BANDS])
    idx=[int(t*.001*SR) for t in TP]; rows=[]
    for mask in _M[1]:
        b=irfft(X*mask,len(m)); e=np.convolve(b**2,np.ones(k)/k,'same'); d=10*np.log10(e/peak+1e-12)
        rows.append([max(d[i],-55) if i<len(d) else -55 for i in idx])
    return np.array(rows)
x=np.fromfile(W+'/wood.f32',dtype='<f4').reshape(-1,2)
def trim(ch): pk=abs(ch).max(); o=int(np.argmax(abs(ch)>0.01*pk)); return ch[o:o+int(2.0*SR)]
RMs=surface(trim((x[:,0]+x[:,1])/2)); FLOOR=float(np.mean((surface(trim(x[:,0]))-surface(trim(x[:,1])))**2))
RS=rb.spatial_full(*[trim(x[:,0]),trim(x[:,1])])
DEF=dict(SIZE=0.35,DECAY=0.46,DAMP=0.3,BW=0.65,SHELFG=0.8,SHELFHZ=4000,EARLYSEND=0.6,ESLP=8000,
         IDIF1=0.75,IDIF2=0.625,DDIF1=0.5,DDIF2=0.6,PRE=2.7,EXC=0,LATEG=0.18,LATEDECAY=5,LATEDAMP=12000,LATEHP=350,LATEPRE=45,LATELOOPHP=400)
GRID=dict(SIZE=[0.3,0.35,0.42],DECAY=[0.4,0.46,0.54],DAMP=[0.2,0.3,0.45],BW=[0.5,0.65,0.8,0.95],
          SHELFG=[0.6,0.75,0.9,1.0],SHELFHZ=[3000,4500,6500],EARLYSEND=[0.45,0.6,0.78],ESLP=[6000,8500,12000],
          DDIF1=[0.5,0.6],DDIF2=[0.5,0.6],IDIF2=[0.55,0.625],
          LATEG=[0.0,0.1,0.18,0.28,0.4],LATEDECAY=[2,3,4.5,7],LATEDAMP=[8000,12000,16000],
          LATEHP=[350,600,900,1300],LATEPRE=[20,45,80,120],LATELOOPHP=[150,400,800,1300])
def render(cfg):
    e=dict(os.environ); e.update({k:str(v) for k,v in cfg.items()})
    subprocess.run([BIN,W+'/imp.f32',W+'/ir.f32'],env=e,stderr=subprocess.DEVNULL,check=True)
    return rb.load_ir(W+'/ir.f32',dur=2.0)
def cost(cfg):
    L,R,m=render(cfg); sm=float(np.mean((surface(m)-RMs)**2)); s=rb.spatial_full(L,R)
    c=sm + 2.0*abs(s['side_mid_dB']-RS['side_mid_dB']) + 1.0*abs(s['iacc_bb']-RS['iacc_bb'])/0.1
    return c, sm, s
if __name__=='__main__':
    rounds=int(sys.argv[1]) if len(sys.argv)>1 else 4
    cur=dict(DEF); sj=f'{W}/room_fitfinal.json'
    if os.path.exists(sj): cur.update(json.load(open(sj)))
    bestc,sm,s=cost(cur); print('FLOOR=%.2f  start surface-dist %.2f (cost %.2f)'%(FLOOR,sm,bestc))
    for rd in range(rounds):
        for k in GRID:
            for v in GRID[k]:
                t=dict(cur); t[k]=v; c,_,_=cost(t)
                if c<bestc-1e-6: bestc=c; cur=t
        _,sm,_=cost(cur); print('round %d surface-dist %.2f'%(rd+1,sm))
    bestc,sm,s=cost(cur)
    print('FINAL surface-dist %.2f  (FLOOR %.2f)  side/mid %.2f(t%.2f) IACC %.2f(t%.2f)'%(sm,FLOOR,s['side_mid_dB'],RS['side_mid_dB'],s['iacc_bb'],RS['iacc_bb']))
    json.dump({k:cur[k] for k in DEF},open(sj,'w')); print('ENV:',' '.join('%s=%s'%(k,cur[k]) for k in DEF))
