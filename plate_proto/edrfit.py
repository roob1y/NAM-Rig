import numpy as np,sys
from wavutil import read_wav
sr=48000
def edr(m,tmax=3.0):
    win=1024;hop=256;m=m[:int(sr*tmax)];w=np.hanning(win);fr=[]
    for i in range(0,len(m)-win,hop): fr.append(np.abs(np.fft.rfft(m[i:i+win]*w))**2)
    S=np.array(fr);E=np.cumsum(S[::-1],axis=0)[::-1];E/=E[0:1]+1e-20
    return np.arange(S.shape[0])*hop/sr,np.fft.rfftfreq(win,1/sr),10*np.log10(E+1e-12)
def loadf(p): a=np.fromfile(p,dtype='<f4').reshape(-1,2); return (a[:,0]+a[:,1])/2
def tat(t,f,E,fq,db):
    bi=np.argmin(np.abs(f-fq));c=E[:,bi];i=np.argmax(c<=db);return t[i] if np.any(c<=db) else np.nan
def show(m,nm):
    t,f,E=edr(m)
    s=" ".join(f"{tat(t,f,E,fq,-20):4.2f}/{tat(t,f,E,fq,-40):4.2f}" for fq in (200,500,1000,2000,4000,8000))
    print(f"{nm:16s} {s}")
print("t@-20/-40 per band (200 500 1k 2k 4k 8k):")
for v in sys.argv[1:]:
    nm=v.split('/')[-1].replace('.f32',''); show(loadf(v),nm)
ir,_=read_wav('ir/NEVO - vintage plate, 2.0s.wav'); show((ir[:,0]+ir[:,1])/2,'REF')
