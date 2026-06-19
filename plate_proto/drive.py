import numpy as np,subprocess,os
from wavutil import read_wav
sr=48000
def loadf(p): a=np.fromfile(p,dtype='<f4').reshape(-1,2); return (a[:,0]+a[:,1])/2
def loadw(p): a,_=read_wav(p); return (a[:,0]+a[:,1])/2
def spec(x,c,frac):
    on=np.argmax(np.abs(x)>0.01*np.max(np.abs(x))); x=x[on:on+int(sr*5)]
    X=np.abs(np.fft.rfft(x*np.hanning(len(x))))**2; f=np.fft.rfftfreq(len(x),1/sr); o=np.zeros(len(c))
    for i,cc in enumerate(c):
        lo,hi=cc/2**(1/(2*frac)),cc*2**(1/(2*frac)); m=(f>=lo)&(f<hi); o[i]=np.mean(X[m]) if m.any() else np.nan
    return 10*np.log10(o+1e-20)
def n1k(c,S): return S-S[int(np.argmin(np.abs(c-1000)))]
nv=loadw('ir/NEVO - vintage plate, 2.0s.wav'); grid=np.geomspace(90,9500,46); Sn=n1k(grid,spec(nv,grid,3))
def run(env):
    e=dict(os.environ);e.update(env);e['FDN_ER_MIX']='0.30'
    subprocess.run(['./fdnplate5','imp8.f32','c.f32'],env=e,stderr=subprocess.DEVNULL); return loadf('c.f32')
print(f"{'config (input/driver only, NO output EQ)':42s} {'rawMax':>7s} {'rawAvg':>7s}")
for nm,ev in {
 'N32 br12.5k (orig raw)':dict(FDN_N='32',FDN_BRIGHT='12500'),
 'N64 S1.5 br7k':dict(FDN_N='64',FDN_SIZE='1.5',FDN_BRIGHT='7000'),
 'N64 S1.5 br6k':dict(FDN_N='64',FDN_SIZE='1.5',FDN_BRIGHT='6000'),
 'N64 S1.5 br5k':dict(FDN_N='64',FDN_SIZE='1.5',FDN_BRIGHT='5000'),
 'N64 S1.5 br4k':dict(FDN_N='64',FDN_SIZE='1.5',FDN_BRIGHT='4000'),
}.items():
    x=run(ev); d=n1k(grid,spec(x,grid,3))-Sn
    hi=grid>=350
    print(f"{nm:42s} {np.max(np.abs(d)):6.2f}  {np.mean(np.abs(d)):6.2f}   (>=350Hz max {np.max(np.abs(d[hi])):.2f})")
