# null_probe.py — like-for-like NULL TESTS for the Tape Echo character.
# Drives our DelayBlock with the EXACT dry input that produced each wet reference
# capture (delay_ref/ -> delay_references/), time-aligns + gain-matches, subtracts,
# and decomposes the residual into the dimensions the magnitude battery can't see:
#   impulse -> magnitude vs PHASE/group-delay of the single-echo transfer
#   levels  -> harmonic-distortion character (even vs odd) of the saturation
#   sustain -> wow/flutter
# Run from /tmp/nb (needs ./dnull + dry/*.f32 + wet/*.f32 already rendered).
import numpy as np, sys, os
sys.path.insert(0,'/sessions/festive-focused-tesla/mnt/NAM-Rig/reverb_analysis')
from wavutil import read_wav
SR=48000.0
REF='/sessions/festive-focused-tesla/mnt/NAM-Rig/delay_references/tape_echo'

def lo(p): return np.fromfile(p,dtype='<f4').astype(np.float64)
def lr(p): a,_=read_wav(p); return a[:,0].astype(np.float64)
def env(x,w=240): return np.convolve(np.abs(x),np.ones(w)/w,'same')
def onset(x,frac=0.05):
    e=env(x); return int(np.argmax(e>frac*e.max()))

def frac_shift(x,tau):
    """shift x by tau samples (sub-sample) via FFT phase ramp."""
    n=len(x); X=np.fft.rfft(x); f=np.fft.rfftfreq(n)
    return np.fft.irfft(X*np.exp(-2j*np.pi*f*tau),n)

def align_gain(a,b,maxlag=64):
    """best integer+frac lag and gain to make g*shift(a)~b. returns aligned a, gain, lag."""
    # integer lag via xcorr
    c=np.correlate(b,a,'full'); lag=np.argmax(np.abs(c))-(len(a)-1)
    lag=int(np.clip(lag,-maxlag,maxlag))
    a2=np.roll(a,lag)
    # refine fractional by scanning
    best=None
    for tau in np.linspace(-1,1,41):
        a3=frac_shift(a2,tau)
        g=np.dot(a3,b)/(np.dot(a3,a3)+1e-30)
        r=b-g*a3; e=np.dot(r,r)
        if best is None or e<best[0]: best=(e,tau,g,a3)
    _,tau,g,a3=best
    return g*a3, g, lag+tau

def null_depth(sig,resid):
    return 10*np.log10((np.dot(resid,resid)+1e-30)/(np.dot(sig,sig)+1e-30))

def band(seg,fcs):
    w=np.hanning(len(seg)); X=np.abs(np.fft.rfft(seg*w))**2; f=np.fft.rfftfreq(len(seg),1/SR)
    out=[]
    for c in fcs:
        s=(f>=c/2**(1/12))&(f<c*2**(1/12)); out.append(10*np.log10(np.mean(X[s])+1e-30) if np.any(s) else np.nan)
    return np.array(out)

FC=40.0*2.0**(np.arange(0,53)/6.0); FC=FC[FC<=16000.0]

# ============ IMPULSE: magnitude vs phase ============
def impulse_null():
    o=lo('wet/impulse.f32'); r=lr(f'{REF}/impulse.wav')
    oon,ron=onset(o),onset(r)
    W=int(0.30*SR)  # 300 ms window = full first-echo IR, before the 2nd echo (+350ms)
    a=o[oon:oon+W].copy(); b=r[ron:ron+W].copy()
    # taper tails to suppress the ref's 2nd-echo leak / edge
    tap=np.ones(W); k=int(0.02*SR); tap[-k:]=np.hanning(2*k)[k:]
    a*=tap; b*=tap
    a_al,g,lag=align_gain(a,b)
    resid=b-a_al
    nd=null_depth(b,resid)
    # complex transfer H = REF/OURS over the echo (zero-pad for resolution)
    n=1<<14
    A=np.fft.rfft(a_al,n); B=np.fft.rfft(b,n); f=np.fft.rfftfreq(n,1/SR)
    sel=(f>=80)&(f<=12000)
    H=B/(A+1e-12)
    magdb=20*np.log10(np.abs(H)+1e-12)
    # remove residual linear-phase (group delay) for a fair phase read
    ph=np.unwrap(np.angle(H))
    A_=np.vstack([f[sel],np.ones(np.sum(sel))]).T
    sl,ic=np.linalg.lstsq(A_,ph[sel],rcond=None)[0]
    phdet=ph-(sl*f+ic)   # phase after removing best linear delay
    grp=-np.gradient(phdet,2*np.pi*(f[1]-f[0]))*1e3  # ms, excess group delay
    # magnitude-matched null: force ours to ref magnitude, keep ours phase, re-null
    A_magfix=np.abs(B)*np.exp(1j*np.angle(A))
    a_magfix=np.fft.irfft(A_magfix,n)[:W]
    a_mf_al,_,_=align_gain(a_magfix,b)
    nd_magfix=null_depth(b,b-a_mf_al)
    print("=== IMPULSE NULL (single echo, ~0.5 impulse) ===")
    print(f"  gain match ours->ref: {g:.3f}  ({20*np.log10(g):+.2f} dB), frac lag {lag:+.2f} smp")
    print(f"  FULL null depth (gain+delay aligned):     {nd:6.1f} dB")
    print(f"  null depth after ALSO matching magnitude: {nd_magfix:6.1f} dB")
    print(f"  -> residual that is PURELY phase/group-delay: {nd-nd_magfix:+.1f} dB "
          f"({'PHASE-dominated' if nd_magfix<nd-3 else 'magnitude-dominated'})")
    # band magnitude error + excess group delay in key bands
    for f0 in [120,250,500,1000,2000,4000,8000]:
        i=np.argmin(np.abs(f-f0))
        print(f"    {f0:5d} Hz: |H| {magdb[i]:+5.1f} dB   excess group delay {grp[i]:+6.2f} ms")
    return dict(nd=nd,nd_magfix=nd_magfix,f=f,magdb=magdb,phdet=phdet,grp=grp,sel=sel,
                resid=resid,a=a_al,b=b)

# ============ LEVELS: harmonic series even vs odd ============
def harmonics(seg,f0=440.0):
    w=np.hanning(len(seg)); X=np.abs(np.fft.rfft(seg*w)); f=np.fft.rfftfreq(len(seg),1/SR)
    def amp(fh):
        i=np.argmin(np.abs(f-fh)); s=slice(max(0,i-3),i+4); return X[s].max()
    base=amp(f0)
    return {h:20*np.log10(amp(h*f0)/base+1e-12) for h in range(2,7)}

def levels_null():
    o=lo('wet/levels.f32'); r=lr(f'{REF}/levels.wav')
    # find the hottest (last) burst echo in each: bursts at ~0.55s spacing
    def hot_burst(x):
        e=env(x,480); thr=0.08*e.max(); hot=e>thr; runs=[]; i=0
        while i<len(hot):
            if hot[i]:
                j=i
                while j<len(hot) and hot[j]: j+=1
                if j-i>int(0.05*SR): runs.append((i,j))
                i=j
            else: i+=1
        a,b=runs[-1]; return x[a:b]
    oseg=hot_burst(o); rseg=hot_burst(r)
    # match length
    L=min(len(oseg),len(rseg)); oseg=oseg[:L]; rseg=rseg[:L]
    ho=harmonics(oseg); hr=harmonics(rseg)
    print("\n=== LEVELS NULL — harmonic series of the hottest burst (input 0.30) ===")
    print(f"  ours peak {np.abs(oseg).max():.4f}  ref peak {np.abs(rseg).max():.4f}")
    print(f"  harmonic (dB rel f0):   {'H2':>8}{'H3':>8}{'H4':>8}{'H5':>8}{'H6':>8}")
    print(f"  ours (odd cubic):     "+"".join(f"{ho[h]:8.1f}" for h in range(2,7)))
    print(f"  reference:            "+"".join(f"{hr[h]:8.1f}" for h in range(2,7)))
    print(f"  Δ(ref-ours):          "+"".join(f"{hr[h]-ho[h]:8.1f}" for h in range(2,7)))
    evn_o=10*np.log10(10**(ho[2]/10)+10**(ho[4]/10)); odd_o=10*np.log10(10**(ho[3]/10)+10**(ho[5]/10))
    evn_r=10*np.log10(10**(hr[2]/10)+10**(hr[4]/10)); odd_r=10*np.log10(10**(hr[3]/10)+10**(hr[5]/10))
    print(f"  EVEN (H2+H4): ours {evn_o:+.1f} dB | ref {evn_r:+.1f} dB  -> Δ {evn_r-evn_o:+.1f} dB")
    print(f"  ODD  (H3+H5): ours {odd_o:+.1f} dB | ref {odd_r:+.1f} dB  -> Δ {odd_r-odd_o:+.1f} dB")
    return dict(ho=ho,hr=hr,oseg=oseg,rseg=rseg)

if __name__=='__main__':
    imp=impulse_null()
    lev=levels_null()
