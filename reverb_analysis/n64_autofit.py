import sys, os, subprocess, itertools
sys.path.insert(0,'.')
import numpy as np, reverb_battery as rb
SR=rb.sr
BIN="../plate_proto/render_proto64"; IMP=os.path.abspath("../plate_proto/impulse_2p5.f32")
REF_TRAJ=np.array([6358,5444,4199,3788.])
BANDS=[125,250,500,2000,4000,8000]; REF_TILT=np.array([-4.3,-2.0,-2.2,2.5,3.7,1.2])

def measure(m):
    o=rb.onset(m); m=m[o:]
    win,hop=512,128; w=np.hanning(win); f=np.fft.rfftfreq(win,1/SR); s=f>40; c=[];t=[]
    for i in range(0,int(0.6*SR)-win,hop):
        fr=np.abs(np.fft.rfft(m[i:i+win]*w)); c.append(np.sum(f[s]*fr[s])/(np.sum(fr[s])+1e-20)); t.append(i/SR)
    t=np.array(t); c=np.array(c)
    traj=np.array([c[min(np.searchsorted(t,x/1000),len(c)-1)] for x in (100,200,350,500)])
    seg=m[:int(2.0*SR)]; n=1<<int(np.ceil(np.log2(len(seg)))); P=np.abs(np.fft.rfft(seg,n))**2; ff=np.fft.rfftfreq(n,1/SR)
    o1=np.array([10*np.log10(np.sum(P[(ff>=cc/np.sqrt(2))&(ff<cc*np.sqrt(2))])+1e-20) for cc in [125,250,500,1000,2000,4000,8000]])
    tilt=o1-o1[3]; tilt=np.array([tilt[i] for i,cc in enumerate([125,250,500,1000,2000,4000,8000]) if cc in BANDS])
    return traj,tilt

def render(darkg,darkhz,vlvlv):
    out=f"/tmp/af_{darkg}_{darkhz}_{vlvlv}.f32"
    env={**os.environ,"RV_T60":"2.45","DARKHZ":str(darkhz),"DARKG":str(darkg),"VLV":"1","VLVLV":str(vlvlv),"HFM":"0"}
    subprocess.run([BIN,"plate",IMP,out],env=env,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    d=np.fromfile(out,dtype='<f4').reshape(-1,2); return (d[:,0]+d[:,1])/2

grid=list(itertools.product([0.85,0.90,0.94],[5000,7000],[1.4,2.0,2.6]))
print(f"{'DARKG':>6}{'DARKHZ':>7}{'VLVLV':>6} | {'traj(100/200/350/500)':>26} | {'tiltErr':>7}{'cenErr':>7}{'ERR':>7}")
best=None
for dg,dh,vl in grid:
    m=render(dg,dh,vl); traj,tilt=measure(m)
    cenErr=np.sqrt(np.mean(((traj-REF_TRAJ)/1000)**2)); tiltErr=np.sqrt(np.mean((tilt-REF_TILT)**2))
    err=cenErr+0.4*tiltErr
    print(f"{dg:6.2f}{dh:7.0f}{vl:6.1f} | {str([int(x) for x in traj]):>26} | {tiltErr:7.2f}{cenErr:7.3f}{err:7.3f}")
    if best is None or err<best[0]: best=(err,dg,dh,vl,traj,tilt)
print("\nBEST:",f"DARKG={best[1]} DARKHZ={best[2]} VLVLV={best[3]} err={best[0]:.3f} traj={[int(x) for x in best[4]]} tilt={[round(float(x),1) for x in best[5]]}")
print("REF: traj=[6358,5444,4199,3788] tilt(125,250,500,2k,4k,8k)=[-4.3,-2.0,-2.2,2.5,3.7,1.2]")
