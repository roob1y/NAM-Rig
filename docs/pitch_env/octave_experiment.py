import numpy as np

SR = 48000.0

def lp2(x, fc, Q=0.707):
    # RBJ 2-pole lowpass, transposed df2
    w = 2*np.pi*fc/SR; cs=np.cos(w); sn=np.sin(w); al=sn/(2*Q); a0=1+al
    b0=(1-cs)/2/a0; b1=(1-cs)/a0; b2=(1-cs)/2/a0; a1=(-2*cs)/a0; a2=(1-al)/a0
    y=np.zeros_like(x); z1=z2=0.0
    for i,xi in enumerate(x):
        yi=b0*xi+z1; z1=b1*xi-a1*yi+z2; z2=b2*xi-a2*yi; y[i]=yi
    return y

def detect_lp(x, fc=800.0):
    return lp2(lp2(x, fc), fc)   # 4-pole, matches PitchBlock detection

def up_zero_crossings(x):
    s = np.sign(x); s[s==0]=1
    return np.sum((s[:-1]<0)&(s[1:]>0))

def comparator_ratio(f, a, phi, use_lp=True, dur=1.0):
    # x = fundamental + 2nd harmonic (amp a, phase phi). Measure upward zero-crossings/sec
    # of the (optionally LP-filtered) detection signal, divided by f.
    # ratio ~1 => divider tracks f (correct f/2 sub); ratio ~2 => doubles to f (octave up).
    n=int(SR*dur); t=np.arange(n)/SR
    x=np.sin(2*np.pi*f*t)+a*np.sin(2*np.pi*2*f*t+phi)
    d=detect_lp(x) if use_lp else x
    d=d[n//4:]  # drop filter transient
    zc=up_zero_crossings(d); secs=len(d)/SR
    return (zc/secs)/f

def nsdf_period(x, tau_min, tau_max, k=0.9):
    # McLeod NSDF (symmetric type II) + 'first key max above k*nmax' peak pick.
    x=x-np.mean(x); W=len(x)
    taus=np.arange(tau_min, min(tau_max, W//2))
    n=np.zeros(len(taus))
    for i,tau in enumerate(taus):
        m=(W-tau)//2
        j0=W//2-m
        a=x[j0:j0+m]; b=x[j0+tau:j0+tau+m]
        r=np.sum(a*b); mm=np.sum(a*a+b*b)
        n[i]=2*r/mm if mm>0 else 0.0
    # key maxima: highest point between a +slope zero-cross and the next -slope zero-cross
    keys=[]; i=1
    while i<len(n)-1:
        if n[i-1]<0 and n[i]>=0:  # positive-going zero crossing -> start of a hump
            j=i; best=i
            while j<len(n)-1 and not (n[j]>0 and n[j+1]<=0):
                if n[j]>n[best]: best=j
                j+=1
            if n[best]>0: keys.append(best)
            i=j+1
        else: i+=1
    if not keys: return None,0.0
    nmax=max(n[k_] for k_ in keys)
    thr=k*nmax
    for k_ in keys:
        if n[k_]>=thr:
            return taus[k_], nmax
    return taus[keys[0]], nmax

def nsdf_ratio(f, a, phi):
    # True period T0 = SR/f. Return detected_period / T0 (1.0 = correct, 0.5 = octave up).
    T0=SR/f
    n=int(max(4*T0, SR*0.06)); t=np.arange(n)/SR
    x=np.sin(2*np.pi*f*t)+a*np.sin(2*np.pi*2*f*t+phi)
    per,cl=nsdf_period(x, int(T0*0.4), int(T0*3))
    if per is None: return np.nan
    return per/T0

print("="*74)
print("EXPERIMENT 1 - threshold/zero-crossing divider: upward-ZC-per-period vs 2nd-harmonic")
print("ratio ~1.0 = tracks fundamental (correct f/2 sub) ; ~2.0 = doubled to f (OCTAVE UP)")
print("worst-case phase phi=pi (2nd harmonic adds a mid-cycle swing). 4-pole 800Hz detect LP ON.")
print("-"*74)
print(f"{'a (2nd/1st)':>12} | {'low note f=98Hz':>18} | {'mid f=196Hz':>14} | {'high f=587Hz':>14}")
for a in [0.0,0.5,0.8,1.0,1.2,1.5,2.0,3.0]:
    r_lo=comparator_ratio(98.0,a,np.pi); r_mid=comparator_ratio(196.0,a,np.pi); r_hi=comparator_ratio(587.0,a,np.pi)
    tag=lambda r:"DOUBLES" if r>1.5 else ("ok" if r<1.2 else "edge")
    print(f"{a:>12.1f} | {r_lo:>7.2f} {tag(r_lo):>9} | {r_mid:>5.2f} {tag(r_mid):>7} | {r_hi:>5.2f} {tag(r_hi):>7}")

print()
print("="*74)
print("EXPERIMENT 2 - same signals, MPM/NSDF autocorrelation period pick (k=0.9)")
print("detected_period / true_period : 1.00 = CORRECT ; 0.50 = octave-up error")
print("(NO low-pass filtering used - MPM works on the raw harmonic-rich signal)")
print("-"*74)
print(f"{'a (2nd/1st)':>12} | {'low f=98Hz':>12} | {'mid f=196Hz':>12} | {'high f=587Hz':>12}")
for a in [0.0,0.5,0.8,1.0,1.2,1.5,2.0,3.0]:
    rr=[nsdf_ratio(f,a,np.pi) for f in (98.0,196.0,587.0)]
    tag=lambda r:"CORRECT" if abs(r-1.0)<0.1 else ("OCT-ERR" if abs(r-0.5)<0.1 else "?")
    print(f"{a:>12.1f} | {rr[0]:>5.2f} {tag(rr[0]):>6} | {rr[1]:>5.2f} {tag(rr[1]):>6} | {rr[2]:>5.2f} {tag(rr[2]):>6}")
