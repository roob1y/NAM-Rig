# EDR side-by-side: python3 edr_plot.py ours.f32  (compares vs ir/NEVO - vintage plate, 2.0s.wav)
import numpy as np, sys, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
from wavutil import read_wav
sr=48000
def edr(m,tmax=2.5):
    win=1024;hop=256;m=m[:int(sr*tmax)];w=np.hanning(win);fr=[]
    for i in range(0,len(m)-win,hop): fr.append(np.abs(np.fft.rfft(m[i:i+win]*w))**2)
    S=np.array(fr);E=np.cumsum(S[::-1],axis=0)[::-1];E/=E[0:1]+1e-20
    return np.arange(S.shape[0])*hop/sr,np.fft.rfftfreq(win,1/sr),10*np.log10(E+1e-12)
def lf(p): a=np.fromfile(p,dtype='<f4').reshape(-1,2); return (a[:,0]+a[:,1])/2
ir,_=read_wav('ir/NEVO - vintage plate, 2.0s.wav'); tr,fr,Er=edr((ir[:,0]+ir[:,1])/2); to,fo,Eo=edr(lf(sys.argv[1]))
fig,ax=plt.subplots(1,2,figsize=(13,5),sharey=True)
for a,(t,f,E,ti) in zip(ax,[(tr,fr,Er,'REAL NEVO'),(to,fo,Eo,'OURS')]):
    im=a.pcolormesh(t,f/1000,E.T,vmin=-60,vmax=0,cmap='magma',shading='auto')
    a.set_ylim(0,16);a.set_title(ti);a.set_xlabel('time (s)');a.set_yscale('symlog',linthresh=1)
    a.set_yticks([0.1,0.25,0.5,1,2,4,8,16]);a.set_yticklabels(['100','250','500','1k','2k','4k','8k','16k'])
fig.colorbar(im,ax=ax,fraction=0.04); plt.savefig('edr_out.png',dpi=110,bbox_inches='tight'); print("saved edr_out.png")
