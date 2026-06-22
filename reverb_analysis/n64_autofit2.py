import sys, os, subprocess, itertools
sys.path.insert(0,'.')
import numpy as np, reverb_battery as rb
SR=rb.sr
BIN="../plate_proto/render_proto64"; IMP=os.path.abspath("../plate_proto/impulse_2p5.f32")
REF_TILT=np.array([-4.3,-2.0,-2.2,2.5,3.7,1.2]); BANDS=[125,250,500,2000,4000,8000]
def fine25(m,t0=.2,t1=1.,nfft=1<<17):
    seg=m[int(t0*SR):int(t1*SR)]; P=np.abs(np.fft.rfft(seg*np.hanning(len(seg)),nfft)); f=np.fft.rfftfreq(nfft,1/SR)
    b=(f>=2000)&(f<=5000); fb=f[b]; PdB=20*np.log10(P[b]+1e-20); df=fb[1]-fb[0]; k=max(3,int(80/df)|1)
    res=PdB-np.convolve(PdB,np.ones(k)/k,mode='same')
    return res.max()-res.min(), sum(1 for i in range(1,len(res)-1) if res[i]>3 and res[i]>res[i-1] and res[i]>=res[i+1])
def measure(m):
    o=rb.onset(m); m2=m[o:]; win,hop=512,128; w=np.hanning(win); f=np.fft.rfftfreq(win,1/SR); s=f>40; c=[];t=[]
    for i in range(0,int(.6*SR)-win,hop):
        fr=np.abs(np.fft.rfft(m2[i:i+win]*w)); c.append(np.sum(f[s]*fr[s])/(np.sum(fr[s])+1e-20)); t.append(i/SR)
    t=np.array(t);c=np.array(c); traj=[int(c[min(np.searchsorted(t,x/1000),len(c)-1)]) for x in (100,200,350,500)]
    seg=m2[:int(2*SR)]; n=1<<int(np.ceil(np.log2(len(seg)))); P=np.abs(np.fft.rfft(seg,n))**2; ff=np.fft.rfftfreq(n,1/SR)
    o1=np.array([10*np.log10(np.sum(P[(ff>=cc/np.sqrt(2))&(ff<cc*np.sqrt(2))])+1e-20) for cc in [125,250,500,1000,2000,4000,8000]]); tl=o1-o1[3]
    tilt=[tl[i] for i,cc in enumerate([125,250,500,1000,2000,4000,8000]) if cc in BANDS]
    te=np.sqrt(np.mean((np.array(tilt)-REF_TILT)**2)); cr,pk=fine25(m); return traj,tilt,te,cr,pk
def render(damp,vl):
    out=f"/tmp/af2_{damp}_{vl}.f32"; env={**os.environ,"RV_T60":"2.45","RV_DAMP":str(damp),"DARKHZ":"7000","DARKG":"0.90","VLV":"1","VLVLV":str(vl),"HFM":"0"}
    subprocess.run([BIN,"plate",IMP,out],env=env,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    d=np.fromfile(out,dtype='<f4').reshape(-1,2); return (d[:,0]+d[:,1])/2
print("DARKG0.90 DARKHZ7000.  ref traj=[6358,5444,4199,3788] tilt2k/4k/8k=2.5/3.7/1.2  N32 crest56.5 pks406")
print(f"{'DAMP':>6}{'VLVLV':>6} | {'traj':>26} | {'2k':>5}{'4k':>5}{'8k':>5}{'tErr':>6} | {'crest':>6}{'pks':>5}")
for damp,vl in itertools.product([6000,11000,18000,26000],[1.4,2.2]):
    traj,tilt,te,cr,pk=measure(render(damp,vl))
    print(f"{damp:6.0f}{vl:6.1f} | {str(traj):>26} | {tilt[3]:5.1f}{tilt[4]:5.1f}{tilt[5]:5.1f}{te:6.2f} | {cr:6.1f}{pk:5d}")
