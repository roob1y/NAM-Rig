import numpy as np,subprocess,os
from wavutil import read_wav
sr=48000
def loadf(p): a=np.fromfile(p,dtype='<f4').reshape(-1,2); return (a[:,0]+a[:,1])/2
def loadw(p): a,_=read_wav(p); return (a[:,0]+a[:,1])/2
def spec(x,centers,frac):
    on=np.argmax(np.abs(x)>0.01*np.max(np.abs(x))); x=x[on:on+int(sr*5)]
    X=np.abs(np.fft.rfft(x*np.hanning(len(x))))**2; f=np.fft.rfftfreq(len(x),1/sr); o=np.zeros(len(centers))
    for i,c in enumerate(centers):
        lo,hi=c/2**(1/(2*frac)),c*2**(1/(2*frac)); m=(f>=lo)&(f<hi); o[i]=np.mean(X[m]) if m.any() else np.nan
    return 10*np.log10(o+1e-20)
def n1k(c,S): return S-S[int(np.argmin(np.abs(c-1000)))]
nv=loadw('ir/NEVO - vintage plate, 2.0s.wav')
grid3=np.geomspace(90,9500,46)          # smoothed balance grid (1/3-oct)
fine=np.geomspace(80,400,40)            # low-end fine grid for ripple
Sn3=n1k(grid3,spec(nv,grid3,3))
# low-end ripple = std of (fine spectrum minus its smooth trend)
def ripple(x):
    S=n1k(fine,spec(x,fine,12)); tr=np.convolve(S,np.ones(7)/7,'same'); return np.std((S-tr)[3:-3])
nv_rip=ripple(nv)
def run(env):
    e=dict(os.environ);e.update(env);e['FDN_ER_MIX']='0.30'
    subprocess.run(['./fdnplate5','imp8.f32','c.f32'],env=e,stderr=subprocess.DEVNULL); return loadf('c.f32')
print(f"studio low-end ripple ref = {nv_rip:.2f} dB-std")
print(f"{'variant':28s} {'rawMaxDev':>9s} {'rawAvgDev':>9s} {'lowRipple':>9s}")
variants={
 'N32 br12.5k (current raw)':dict(FDN_N='32',FDN_BRIGHT='12500'),
 'N64 br12.5k':dict(FDN_N='64',FDN_BRIGHT='12500'),
 'N64 SIZE1.5 br12.5k':dict(FDN_N='64',FDN_SIZE='1.5',FDN_BRIGHT='12500'),
 'N64 SIZE2.0 br12.5k':dict(FDN_N='64',FDN_SIZE='2.0',FDN_BRIGHT='12500'),
 'N64 SIZE2.0 br8k':dict(FDN_N='64',FDN_SIZE='2.0',FDN_BRIGHT='8000'),
}
for nm,ev in variants.items():
    x=run(ev); d=n1k(grid3,spec(x,grid3,3))-Sn3
    print(f"{nm:28s} {np.max(np.abs(d)):8.2f}  {np.mean(np.abs(d)):8.2f}  {ripple(x):8.2f}")
