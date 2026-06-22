import sys,os,subprocess;sys.path.insert(0,'.')
import numpy as np, matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
import reverb_battery as rb
SR=rb.sr
def analytic(x):
    n=len(x);X=np.fft.fft(x);h=np.zeros(n);h[0]=1
    if n%2==0:h[n//2]=1;h[1:n//2]=2
    else:h[1:(n+1)//2]=2
    return np.fft.ifft(X*h)
def bp(x,f0,bw,nfft):
    X=np.fft.rfft(x,nfft);f=np.fft.rfftfreq(nfft,1/SR);g=np.exp(-0.5*((f-f0)/(bw/2))**2)
    return np.fft.irfft(X*g,nfft)[:len(x)]
def alignLR(LR):
    L,R=LR;m=(L+R)/2;o=rb.onset(m);return L[o:],R[o:]
def ir_render(cmd,cwd,env):
    out=f"/tmp/oc_{abs(hash((cmd,tuple(sorted(env.items())))))%99999}.f32"
    subprocess.run([cmd,"plate",os.path.abspath("impulse.f32"),out],cwd=cwd,env={**os.environ,"RV_T60":"2.45",**env},check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    return rb.load_ours(out)
refL,refR=alignLR(rb.load_ref("ir/vintage-plate-1.5s.wav"))
ourL,ourR=alignLR(ir_render("../plate_proto/render_proto","../plate_proto",{"HFM":"1","VLV":"1","DIFF":"0"}))
def env_db(x,N): e=np.abs(analytic(x[:N])); return 20*np.log10(e/(np.max(np.abs(analytic(x[:int(0.5*SR)])))+1e-20)+1e-9)
N=int(0.15*SR);tt=np.arange(N)/SR*1000
fig,ax=plt.subplots(2,3,figsize=(18,9))
# 1 broadband energy envelope (bloom)
for nm,(L,R),col in [("reference",(refL,refR),"k"),("ours",(ourL,ourR),"g")]:
    m=(L+R)/2; ax[0,0].plot(tt,env_db(m,N),col,label=nm)
ax[0,0].set_title("Broadband onset envelope 0-150ms (BLOOM)");ax[0,0].set_xlabel("ms");ax[0,0].set_ylabel("dB");ax[0,0].legend();ax[0,0].grid(alpha=.3)
# 2 per-band energy-peak time (the bloom is freq-dependent)
fc=np.geomspace(250,11000,20);nfft=1<<16
def peakt(m):return [np.argmax(np.abs(analytic(bp(m[:int(0.2*SR)],f0,max(80,0.18*f0),nfft))))/SR*1000 for f0 in fc]
ax[0,1].semilogx(fc,peakt((refL+refR)/2),'k-o',ms=3,label="reference");ax[0,1].semilogx(fc,peakt((ourL+ourR)/2),'g-o',ms=3,label="ours")
ax[0,1].set_title("Energy-peak time vs freq (bloom timing)");ax[0,1].set_xlabel("Hz");ax[0,1].set_ylabel("ms");ax[0,1].legend();ax[0,1].grid(alpha=.3,which='both')
# 3 brightness over time
win=512;hop=128
def bright(m):
    c=[];t=[]
    for i in range(0,int(0.3*SR)-win,hop):
        fr=np.abs(np.fft.rfft(m[i:i+win]*np.hanning(win)));f=np.fft.rfftfreq(win,1/SR);s=f>40
        c.append(np.sum(f[s]*fr[s])/(np.sum(fr[s])+1e-20));t.append(i/SR*1000)
    return np.array(t),np.array(c)
for nm,m,col in [("reference",(refL+refR)/2,"k"),("ours",(ourL+ourR)/2,"g")]:
    t,c=bright(m);ax[0,2].plot(t,c,col,label=nm)
ax[0,2].set_title("Brightness over time 0-300ms");ax[0,2].set_xlabel("ms");ax[0,2].set_ylabel("centroid Hz");ax[0,2].legend();ax[0,2].grid(alpha=.3)
# 4 L/R coherence over time (the 'in phase' observation)
def coh(L,R):
    c=[];t=[];w=int(0.010*SR)
    for i in range(0,int(0.3*SR)-w,hop):
        a=L[i:i+w]-np.mean(L[i:i+w]);b=R[i:i+w]-np.mean(R[i:i+w])
        c.append(np.sum(a*b)/(np.sqrt(np.sum(a**2)*np.sum(b**2))+1e-20));t.append(i/SR*1000)
    return np.array(t),np.array(c)
for nm,(L,R),col in [("reference",(refL,refR),"k"),("ours",(ourL,ourR),"g")]:
    t,c=coh(L,R);ax[1,0].plot(t,c,col,label=nm)
ax[1,0].set_title("L/R coherence over time ('in phase')");ax[1,0].set_xlabel("ms");ax[1,0].set_ylabel("correlation");ax[1,0].legend();ax[1,0].grid(alpha=.3);ax[1,0].axhline(0,color='gray',lw=.5)
# 5 crest over time (density)
def crest(m):
    c=[];t=[];w=int(0.010*SR)
    for i in range(0,int(0.3*SR)-w,hop):
        sg=m[i:i+w];c.append(np.max(np.abs(sg))/(np.sqrt(np.mean(sg**2))+1e-20));t.append(i/SR*1000)
    return np.array(t),np.array(c)
for nm,m,col in [("reference",(refL+refR)/2,"k"),("ours",(ourL+ourR)/2,"g")]:
    t,c=crest(m);ax[1,1].plot(t,c,col,label=nm)
ax[1,1].set_title("Crest factor over time (lower=denser)");ax[1,1].set_xlabel("ms");ax[1,1].legend();ax[1,1].grid(alpha=.3)
# 6 waveform 0-30ms L (onset shape)
N2=int(0.03*SR)
ax[1,2].plot(np.arange(N2)/SR*1000,refL[:N2],'k',lw=.7,label="ref L");ax[1,2].plot(np.arange(N2)/SR*1000,ourL[:N2],'g',lw=.7,alpha=.8,label="our L")
ax[1,2].set_title("Onset waveform 0-30ms (L)");ax[1,2].set_xlabel("ms");ax[1,2].legend()
fig.suptitle("ONSET characterization: reference vs ours (first 150ms) -- the build target",fontsize=14)
plt.tight_layout(rect=[0,0,1,0.97]);plt.savefig("onset_char.png",dpi=110);plt.close()
# numbers
rt,rc=coh(refL,refR);ot,oc=coh(ourL,ourR)
print("L/R coherence 0-30ms:  ref %.2f  ours %.2f"%(np.mean(rc[rt<30]),np.mean(oc[ot<30])))
print("L/R coherence 30-100ms: ref %.2f  ours %.2f"%(np.mean(rc[(rt>=30)&(rt<100)]),np.mean(oc[(ot>=30)&(ot<100)])))
print("bloom peak (broadband) ref %.0fms ours %.0fms"%(np.argmax(np.abs(analytic((refL+refR)/2)[:N]))/SR*1000,np.argmax(np.abs(analytic((ourL+ourR)/2)[:N]))/SR*1000))
print("wrote onset_char.png")
