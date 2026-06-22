import sys,os,subprocess;sys.path.insert(0,'.')
import numpy as np, reverb_battery as rb
SR=rb.sr
def an(x):
    n=len(x);X=np.fft.fft(x);h=np.zeros(n);h[0]=1
    if n%2==0:h[n//2]=1;h[1:n//2]=2
    else:h[1:(n+1)//2]=2
    return np.fft.ifft(X*h)
def erb_bands(lo,hi,n):
    h=lambda f:21.4*np.log10(4.37e-3*f+1);ih=lambda e:(10**(e/21.4)-1)/4.37e-3
    return ih(np.linspace(h(lo),h(hi),n))
def bp(x,f0,nf):
    X=np.fft.rfft(x,nf);f=np.fft.rfftfreq(nf,1/SR);bw=max(60,0.14*f0);g=np.exp(-0.5*((f-f0)/(bw/2))**2)
    return np.fft.irfft(X*g,nf)[:len(x)]
def align(LR):L,R=LR;m=(L+R)/2;o=rb.onset(m);return L[o:],R[o:]
def build(onms,nbands,smooth_lo_ms,smooth_hi_ms,common,seed_base,hfemph=1.0):
    rL,rR=align(rb.load_ref("ir/vintage-plate-1.5s.wav")); ON=int(onms*0.001*SR)
    FC=erb_bands(150,16000,nbands); nf=1<<15
    def synth(refch,seed,noiseC):
        rng=np.random.default_rng(seed);out=np.zeros(ON)
        for f0 in FC:
            b=bp(refch[:ON],f0,nf); env=np.abs(an(b))
            sm=int((smooth_lo_ms+(smooth_hi_ms-smooth_lo_ms)*min(1,f0/8000))*0.001*SR); sm=max(4,sm)
            # shorter smoothing at HF -> preserve transient; longer at LF
            sm=int(smooth_lo_ms*0.001*SR) if f0<2000 else max(4,int(smooth_hi_ms*0.001*SR))
            env=np.convolve(env,np.ones(sm)/sm,mode='same')
            if f0>4000 and hfemph!=1.0:
                w_e=np.ones(ON); k=int(0.025*SR); w_e[:k]=hfemph; w_e[k:k+200]=np.linspace(hfemph,1,min(200,ON-k)); env=env*w_e
            noise=(1-common)*rng.standard_normal(ON)+common*noiseC
            nb=bp(noise,f0,nf); nb*=env/(np.sqrt(np.mean(nb**2)+1e-12))
            nb*=np.sqrt(np.mean(b**2)+1e-20)/(np.sqrt(np.mean(nb**2)+1e-20)); out+=nb
        return out
    rng=np.random.default_rng(seed_base); common_noise=rng.standard_normal(ON)
    sL=synth(rL,seed_base+1,common_noise); sR=synth(rR,seed_base+2,common_noise)
    g=np.sqrt(np.mean(rL[:ON]**2+rR[:ON]**2))/(np.sqrt(np.mean(sL**2+sR**2))+1e-12); sL*=g;sR*=g
    # tail
    subprocess.run(["../plate_proto/render_proto","plate",os.path.abspath("impulse.f32"),"/tmp/tail_ir.f32"],cwd="../plate_proto",
        env={**os.environ,"RV_T60":"2.45","HFM":"1","VLV":"1","EARLY":"0"},check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    tL,tR=align(rb.load_ours("/tmp/tail_ir.f32")); N=max(len(tL),ON)+10
    def pad(a):return np.concatenate([a,np.zeros(N-len(a))]) if len(a)<N else a[:N]
    S=ON-int(0.008*SR);xf=int(0.014*SR)
    def splice(eL,tch):
        out=np.zeros(N);eLp=pad(eL);tt=pad(tch)
        r_e=np.sqrt(np.mean(eLp[S-xf:S]**2)+1e-20);r_t=np.sqrt(np.mean(tt[S:S+xf]**2)+1e-20);tt=tt*(r_e/(r_t+1e-12))
        a=S-xf//2;b=S+xf//2;w=np.linspace(1,0,b-a)
        out[:a]=eLp[:a];out[a:b]=eLp[a:b]*w+tt[a:b]*(1-w);out[b:]=tt[b:];return out
    if os.environ.get('WANT_EARLY'):
        return pad(sL),pad(sR)
    return splice(sL,tL),splice(sR,tR)
if __name__=="__main__":
    import onset_fit as of
    import sys
    he=float(sys.argv[1]) if len(sys.argv)>1 else 1.0
    hL,hR=build(150,40,8.0,2.0,0.10,10,he)
    np.stack([hL,hR],1).astype('<f4').tofile("/tmp/gl_ir.f32")
    err=float(np.mean(np.abs(of.norm(of.tf_map((hL+hR)/2))-of.REFM)))
    # key metrics
    m=(hL+hR)/2;o=rb.onset(m);mm=m[o:];e=mm**2;t=np.arange(len(mm))/SR
    cen0=of.tf_map  # not used
    import numpy as _np
    def cen_t0(x):
        fr=np.abs(np.fft.rfft(x[:1024]*np.hanning(1024)));ff=np.fft.rfftfreq(1024,1/SR);s=ff>40
        return np.sum(ff[s]*fr[s])/(np.sum(fr[s])+1e-20)
    crest=np.max(np.abs(mm))/(np.sqrt(np.mean(mm**2))+1e-20)
    print("TF err %.2f | cen_t0 %d (ref 7651) | crest %.1f (ref 16.6)"%(err,cen_t0(mm),crest))
