# Reverb Capture (Track 1, deterministic): IR -> engine params, analytically.
#   profile -> T60(f) -> two-stage GEQ damping -> driver/low-cut/air -> tone-corrector
# Emits FDN_GUNIT file + an env line to drive fdnplate10. Auto-grades vs the IR.
import numpy as np, sys, os, subprocess
from wavutil import read_wav
from geq_twostage import peak, magdb
sr=48000.0
FC=np.array([62.5,125,180,250,355,500,710,1000,1400,2000,2800,4000,5600,8000,11000,16000.])
def bp(x,fc,Q=2.0):
    w0=2*np.pi*fc/sr; al=np.sin(w0)/(2*Q); c=np.cos(w0)
    b0,b2=al/(1+al),-al/(1+al); a1,a2=(-2*c)/(1+al),(1-al)/(1+al)
    nfft=1<<int(np.ceil(np.log2(len(x)+8))); X=np.fft.rfft(x,nfft)
    om=2*np.pi*np.fft.rfftfreq(nfft); z=np.exp(-1j*om)
    H=(b0+b2*z*z)/(1+a1*z+a2*z*z); return np.fft.irfft(X*H,nfft)[:len(x)]
def t60(x,lo=-5,hi=-35):
    e=x**2; sch=np.cumsum(e[::-1])[::-1]; sch/=sch[0]+1e-20; db=10*np.log10(sch+1e-20)
    i1=np.argmax(db<=lo); i2=np.argmax(db<=hi)
    return (i2-i1)/sr*(60.0/(lo-hi)) if i2>i1 else np.nan
def oct3(m,cf):
    X=np.abs(np.fft.rfft(m*np.hanning(len(m))))**2; f=np.fft.rfftfreq(len(m),1/sr); out=[]
    for c in cf:
        lo,hi=c/2**(1/6),c*2**(1/6); s=(f>=lo)&(f<hi); out.append(10*np.log10(np.mean(X[s])+1e-30))
    return np.array(out)

def measure_t60f(m,win=7.0,max_t60=None):
    on=np.argmax(np.abs(m)>0.01*np.max(np.abs(m))); seg=m[on:on+int(sr*win)]
    t=np.array([t60(bp(seg,fc)) for fc in FC])
    # fill non-finite by interpolation across log-f
    ok=np.isfinite(t)
    t=np.interp(np.log(FC),np.log(FC[ok]),t[ok])
    # clean: monotonic non-increasing above 355 Hz (plate decay falls with freq;
    # removes HF noise-floor bumps), then cap at the max perceived decay.
    k355=int(np.argmin(np.abs(FC-355.0)))
    for i in range(k355+1,len(t)): t[i]=min(t[i],t[i-1])
    if max_t60 is not None: t=np.minimum(t,max_t60)
    return t

def design_damping(t60_fc,Q=0.70):
    # Attenuation filter on the longest-T60 broadband floor. For gentle/moderate
    # tilts, an unconstrained least-squares (small +/- gains) matches best. For
    # STEEP tilts (>=4:1) the free fit uses BOOSTS to sculpt the steepness, and a
    # boosted band sustains/contaminates the decay in the render -> switch to a
    # cuts-only projected-gradient fit (physically correct: no anti-damping).
    DC60=float(np.nanmax(t60_fc))*1.03
    tilt=float(np.nanmax(t60_fc)/max(np.nanmin(t60_fc),1e-3))
    cutsonly=tilt>=4.0
    fit=np.exp(np.linspace(np.log(70),np.log(15000),200))
    t60_fit=np.interp(np.log(fit),np.log(FC),t60_fc)
    m=2807.0; L0=-60.0*m/(DC60*sr); resid=(-60.0*m/(t60_fit*sr))-L0
    A=np.zeros((len(fit),len(FC)))
    for j in range(len(FC)): A[:,j]=magdb(peak(FC[j],1.0,Q),fit)
    if cutsonly:
        g=np.zeros(len(FC))
        for _ in range(40):
            dg,_,_,_=np.linalg.lstsq(A,resid-A@g,rcond=None); g=np.minimum(g+0.5*dg,0.0)
    else:
        g,_,_,_=np.linalg.lstsq(A,resid,rcond=None)
        for _ in range(5):
            dg,_,_,_=np.linalg.lstsq(A,resid-A@g,rcond=None); g=g+dg
    return DC60, g/m

def spectral_edges(m):
    on=np.argmax(np.abs(m)>0.01*np.max(np.abs(m))); seg=m[on:on+int(sr*3)]
    cf=np.array([40,63,100,160,250,400,630,1000,1600,2500,4000,6300,9000,12500,16000.])
    s=oct3(seg,cf); s-=s[(cf>=800)&(cf<=2000)].mean()    # ref to ~1k
    midref=s[(cf>=1000)&(cf<=2500)].mean()
    # driver bandwidth: HF freq where smoothed spectrum drops 6 dB below mid
    hf=cf>=2000; below=np.where(hf & (s<midref-6))[0]
    # driver bandwidth floored at 5500 Hz: a plate driver passes body to ~6k; a
    # lower 2-pole LP cliffs the top and reads dark (the air is restored separately
    # by the render-based air shelf below, like the v1 plate's 6k LP + 14k sheen).
    bright=float(np.clip(cf[below[0]] if len(below) else 8000,5500,8500))
    # low-cut: lowest band where spectrum is >6 dB below the 100-250 plateau
    plateau=s[(cf>=100)&(cf<=250)].mean(); lowbands=cf<100
    lc=np.where(lowbands & (s<plateau-6))[0]
    lowcut=float(np.clip(cf[lc[-1]] if len(lc) else 40,25,120))
    return bright,lowcut

def fit_air(m, renderp, airf=11000.0):
    # render-based air shelf: lift our top octave to the IR's. The 2-pole driver LP
    # cliffs the top; a real plate keeps gentle air to 16k. Measure (IR - render)
    # over 10-15 kHz (re 1-3k) and set the shelf to close the deficit.
    a=np.fromfile(renderp,dtype='<f4').reshape(-1,2); r=(a[:,0]+a[:,1])/2
    def top(x):
        on=np.argmax(np.abs(x)>0.01*np.max(np.abs(x))); seg=x[on:on+int(sr*3)]
        X=np.abs(np.fft.rfft(seg*np.hanning(len(seg))))**2; f=np.fft.rfftfreq(len(seg),1/sr)
        ref=10*np.log10(np.mean(X[(f>=1000)&(f<3000)])+1e-30)
        hi=10*np.log10(np.mean(X[(f>=10000)&(f<15000)])+1e-30)
        return hi-ref
    return float(np.clip(top(m)-top(r),0,12)), airf

def render(out, gunit_file, env):
    e=dict(os.environ); e.update({k:str(v) for k,v in env.items()}); e['FDN_GUNIT']=gunit_file
    subprocess.run(['./fdnplate10','dry_imp.f32',out],env=e,stderr=subprocess.DEVNULL,check=True)

def capture(irpath):
    ir,_=read_wav(irpath); m=(ir[:,0]+ir[:,1])/2
    win=min(14.0,max(3.0,ir_len_s(m)))
    # no decay cap: match the IR's true T60(f), incl. its long lows (cap was a misread)
    t60_fc=measure_t60f(m,win=win); DC60,gunit=design_damping(t60_fc)
    bright,lowcut=spectral_edges(m)
    np.savetxt('fit_gunit.txt',gunit,fmt='%.8e')
    env={'FDN_N':64,'FDN_SIZE':1.5,'FDN_DC60':round(DC60,4),'FDN_GEQ_Q':0.70,
         'FDN_BRIGHT':round(bright,1),'FDN_AIR':0,'FDN_AIRF':11000,
         'FDN_LOWCUT':round(lowcut,1),'FDN_DRV_LM':5.0,'FDN_DRV_LMF':220,'FDN_ER_MIX':0.30}
    # render-based air shelf to match the IR's top octave (driver LP cliffs the top)
    render('_airprobe.f32','fit_gunit.txt',env)
    air,airf=fit_air(m,'_airprobe.f32'); env['FDN_AIR']=round(air,2); env['FDN_AIRF']=airf
    return t60_fc,DC60,gunit,env

def ir_len_s(m):
    on=np.argmax(np.abs(m)>0.01*np.max(np.abs(m))); seg=np.abs(m[on:])
    e=seg**2; sch=np.cumsum(e[::-1])[::-1]; sch/=sch[0]+1e-20; db=10*np.log10(sch+1e-20)
    i=np.argmax(db<=-58); return (i/sr) if i>0 else len(seg)/sr

def tone_corrector(irp,renderp,win):
    # measure 1/3-oct residual (render - IR), design a sparse broadband GEQ for |resid|>1dB
    ir,_=read_wav(irp); m=(ir[:,0]+ir[:,1])/2
    a=np.fromfile(renderp,dtype='<f4').reshape(-1,2); r=(a[:,0]+a[:,1])/2
    cf=np.array([200,315,500,800,1250,2000,3150,5000,8000.])
    def sp(x):
        on=np.argmax(np.abs(x)>0.01*np.max(np.abs(x))); seg=x[on:on+int(sr*win)]
        X=np.abs(np.fft.rfft(seg*np.hanning(len(seg))))**2; f=np.fft.rfftfreq(len(seg),1/sr); o=[]
        for c in cf:
            lo,hi=c/2**(1/6),c*2**(1/6); ss=(f>=lo)&(f<hi); o.append(10*np.log10(np.mean(X[ss])+1e-30))
        o=np.array(o); return o-o[4]
    resid=sp(r)-sp(m)   # positive = we have too much -> cut
    lines=[]
    for c,d in zip(cf,resid):
        if abs(d)>1.0: lines.append((c,1.0,round(-d,2)))  # correct broadband residual only
    with open('tone_geq.txt','w') as f:
        for fq,q,g in lines: f.write("%.1f %.2f %.2f\n"%(fq,q,g))
    return lines

if __name__=="__main__":
    irp=sys.argv[1]
    ir,_=read_wav(irp); m=(ir[:,0]+ir[:,1])/2
    Lir=ir_len_s(m); win=min(14.0,max(3.0,Lir*1.0)); imp=int(sr*max(8.0,Lir*2.0))
    a=np.zeros((imp,2),'<f4'); a[10]=1; a.tofile('dry_imp.f32')
    t60_fc,DC60,gunit,env=capture(irp)
    print("CAPTURE %s  (IR~%.1fs, win=%.1fs)"%(os.path.basename(irp),Lir,win))
    print("  DC60=%.3f BRIGHT=%.0f AIR=%.1f LOWCUT=%.0f"%(DC60,env['FDN_BRIGHT'],env['FDN_AIR'],env['FDN_LOWCUT']))
    render('cap_struct.f32','fit_gunit.txt',env)
    print(" -- structural fit (no tone-corrector) --")
    import grade as G; G.grade(irp,'cap_struct.f32',win)
    lines=tone_corrector(irp,'cap_struct.f32',win)
    env2=dict(env); env2['FDN_GEQ']='tone_geq.txt'
    render('cap_final.f32','fit_gunit.txt',env2)
    print(" -- after tone-corrector (%d bands: %s) --"%(len(lines),", ".join("%.0fHz%+.1f"%(f,g) for f,q,g in lines)))
    G.grade(irp,'cap_final.f32',win)
