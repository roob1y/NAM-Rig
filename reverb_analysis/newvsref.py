import sys,os;sys.path.insert(0,'.')
import numpy as np, matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
import reverb_battery as rb
SR=rb.sr
def an(x):
    n=len(x);X=np.fft.fft(x);h=np.zeros(n);h[0]=1
    if n%2==0:h[n//2]=1;h[1:n//2]=2
    else:h[1:(n+1)//2]=2
    return np.fft.ifft(X*h)
def al(LR):L,R=LR;m=(L+R)/2;o=rb.onset(m);return m[o:],L[o:],R[o:]
refm,refL,refR=al(rb.load_ref("ir/vintage-plate-1.5s.wav"))
newm,newL,newR=al(rb.load_ours("/tmp/new_ir.f32"))
N=int(0.15*SR);tt=np.arange(N)/SR*1000
fig,ax=plt.subplots(1,3,figsize=(17,5))
# envelope dB (smoothed)
def edb(m):
    e=np.abs(an(m[:N]));e=np.convolve(e,np.ones(64)/64,mode='same');return 20*np.log10(e/(np.max(e)+1e-12)+1e-9)
ax[0].plot(tt,edb(refm),'k',label='reference onset');ax[0].plot(tt,edb(newm),'g',label='new onset')
ax[0].set_title("Onset envelope 0-150ms (shape/bloom)");ax[0].set_xlabel("ms");ax[0].set_ylabel("dB");ax[0].legend();ax[0].grid(alpha=.3);ax[0].set_ylim(-30,2)
# spectrum of first 150ms
def sp(m):
    seg=m[:N]*np.hanning(N);X=20*np.log10(np.abs(np.fft.rfft(seg))+1e-6);f=np.fft.rfftfreq(N,1/SR)
    return f,X-np.max(X)
f,Xr=sp(refm);_,Xn=sp(newm)
ax[1].semilogx(f,Xr,'k',label='reference',lw=1);ax[1].semilogx(f,Xn,'g',label='new',lw=1)
ax[1].set_xlim(100,20000);ax[1].set_ylim(-50,2);ax[1].set_title("Onset spectrum (first 150ms)");ax[1].set_xlabel("Hz");ax[1].legend();ax[1].grid(alpha=.3,which='both')
# brightness over time
win=512;hop=128
def br(m):
    c=[];t=[]
    for i in range(0,int(0.25*SR)-win,hop):
        fr=np.abs(np.fft.rfft(m[i:i+win]*np.hanning(win)));ff=np.fft.rfftfreq(win,1/SR);s=ff>40
        c.append(np.sum(ff[s]*fr[s])/(np.sum(fr[s])+1e-20));t.append(i/SR*1000)
    return np.array(t),np.array(c)
t,c=br(refm);ax[2].plot(t,c,'k',label='reference');t,c=br(newm);ax[2].plot(t,c,'g',label='new')
ax[2].set_title("Brightness over time");ax[2].set_xlabel("ms");ax[2].set_ylabel("Hz");ax[2].legend();ax[2].grid(alpha=.3)
fig.suptitle("NEW onset vs REFERENCE onset -- what's 'off'",fontsize=13)
plt.tight_layout();plt.savefig("newvsref.png",dpi=120);plt.close()
# numbers
print("onset env @0ms/10ms/40ms/70ms dB:")
print("  ref",[round(edb(refm)[int(x*SR/1000)],1) for x in (0,10,40,70)])
print("  new",[round(edb(newm)[int(x*SR/1000)],1) for x in (0,10,40,70)])
print("centroid first150ms: ref %d new %d"%(rb.centroid(refm[:N]),rb.centroid(newm[:N])))
print("wrote newvsref.png")
