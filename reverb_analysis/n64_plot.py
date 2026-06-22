#!/usr/bin/env python3
import sys, os; sys.path.insert(0,'.')
import numpy as np, matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
import reverb_battery as rb
SR=rb.sr

def load_any(path):
    if path.endswith('.wav'):
        L,R=rb.load_ref(path)
    else:
        d=np.fromfile(path,dtype='<f4').reshape(-1,2); L,R=d[:,0],d[:,1]
    m=(L+R)/2.0; o=rb.onset(m); return m[o:]

def running_centroid(m,win=512,hop=128,tmax=0.6):
    w=np.hanning(win); f=np.fft.rfftfreq(win,1/SR); s=f>40; c=[];t=[]
    for i in range(0,int(tmax*SR)-win,hop):
        fr=np.abs(np.fft.rfft(m[i:i+win]*w)); c.append(np.sum(f[s]*fr[s])/(np.sum(fr[s])+1e-20)); t.append(i/SR)
    return np.array(t),np.array(c)

def fine_spec_25k(m,t0=0.20,t1=1.0,nfft=1<<17):
    seg=m[int(t0*SR):int(t1*SR)]
    P=np.abs(np.fft.rfft(seg*np.hanning(len(seg)),nfft)); f=np.fft.rfftfreq(nfft,1/SR)
    b=(f>=2000)&(f<=5000); return f[b], 20*np.log10(P[b]+1e-20)

def detrend(fb,PdB,smooth_hz=80.0):
    df=fb[1]-fb[0]; k=max(3,int(smooth_hz/df)|1)
    trend=np.convolve(PdB,np.ones(k)/k,mode='same'); return PdB-trend, trend

def modal_metrics(fb,PdB):
    res,_=detrend(fb,PdB); crest=res.max()-res.min(); std=res.std()
    pk=sum(1 for i in range(1,len(res)-1) if res[i]>3.0 and res[i]>res[i-1] and res[i]>=res[i+1])
    return crest,std,pk

def octaves(m):
    seg=m[:int(2.0*SR)]; n=1<<int(np.ceil(np.log2(len(seg))))
    P=np.abs(np.fft.rfft(seg,n))**2; f=np.fft.rfftfreq(n,1/SR)
    fc=[125,250,500,1000,2000,4000,8000]; o=[10*np.log10(np.sum(P[(f>=c/np.sqrt(2))&(f<c*np.sqrt(2))])+1e-20) for c in fc]
    o=np.array(o); return fc,o-o[3]

items={'reference':'ir/vintage-plate-1.5s.wav','N32':'/tmp/n32.f32','N64':'/tmp/n64.f32'}
cols={'reference':'k','N32':'tab:orange','N64':'tab:green'}
data={k:load_any(v) for k,v in items.items()}
fig,ax=plt.subplots(2,2,figsize=(15,9))

for k,m in data.items():
    t,c=running_centroid(m); ax[0,0].plot(t*1000,c,color=cols[k],label=k,lw=1.8)
for x in (100,200,350,500): ax[0,0].axvline(x,color='grey',ls=':',alpha=.4)
ax[0,0].set_title('Running spectral centroid (brightness over time)'); ax[0,0].set_xlabel('ms'); ax[0,0].set_ylabel('Hz'); ax[0,0].set_xlim(0,600); ax[0,0].legend(); ax[0,0].grid(alpha=.3)

metr={}
for off,(k,m) in zip([0,-14,-28],data.items()):
    fb,PdB=fine_spec_25k(m); res,_=detrend(fb,PdB)
    ax[0,1].plot(fb,res+off,color=cols[k],lw=.7,label=k); metr[k]=modal_metrics(fb,PdB)
ax[0,1].set_title('2-5 kHz fine spectrum, de-trended (modal lines; traces offset 14 dB)'); ax[0,1].set_xlabel('Hz'); ax[0,1].set_ylabel('dB over local trend'); ax[0,1].legend(); ax[0,1].grid(alpha=.3)

names=list(items); x=np.arange(len(names)); w=0.27
crest=[metr[k][0] for k in names]; std=[metr[k][1] for k in names]; pk=[metr[k][2] for k in names]
b1=ax[1,0].bar(x-w,crest,w,label='crest max-min (dB)',color='tab:blue')
b2=ax[1,0].bar(x,std,w,label='ripple std (dB)',color='tab:red')
ax[1,0].set_ylabel('dB'); ax[1,0].set_ylim(0,65)
axt=ax[1,0].twinx(); b3=axt.bar(x+w,pk,w,label='# peaks >3dB',color='tab:purple'); axt.set_ylabel('# peaks'); axt.set_ylim(0,500)
for bb in (b1,b2): ax[1,0].bar_label(bb,fmt='%.1f',fontsize=7)
axt.bar_label(b3,fmt='%d',fontsize=7)
ax[1,0].set_xticks(x); ax[1,0].set_xticklabels(names)
ax[1,0].set_title('2-5 kHz modal-line metrics (lower crest/std=no dominant line; higher #peaks=denser)')
l1,la1=ax[1,0].get_legend_handles_labels(); l2,la2=axt.get_legend_handles_labels()
ax[1,0].legend(l1+l2,la1+la2,fontsize=8,loc='upper left'); ax[1,0].grid(alpha=.3,axis='y')

for k,m in data.items():
    fc,o=octaves(m); ax[1,1].semilogx(fc,o,'-o',color=cols[k],ms=4,label=k)
ax[1,1].set_title('Octave tonal balance (norm @1kHz)'); ax[1,1].set_xlabel('Hz'); ax[1,1].set_ylabel('dB'); ax[1,1].legend(); ax[1,1].grid(alpha=.3,which='both')

fig.suptitle('Plate N32 vs N64 vs vintage-plate reference (anchor knob 1.5, T60=2.45)',fontsize=14)
fig.tight_layout(); fig.savefig('n64_experiment.png',dpi=110)
print('OK saved n64_experiment.png')
