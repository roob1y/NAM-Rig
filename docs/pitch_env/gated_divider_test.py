import numpy as np
SR=48000.0
def lp2(x,fc,Q=0.707,zi=(0.0,0.0)):
    w=2*np.pi*fc/SR;cs=np.cos(w);sn=np.sin(w);al=sn/(2*Q);a0=1+al
    b0=(1-cs)/2/a0;b1=(1-cs)/a0;b2=(1-cs)/2/a0;a1=(-2*cs)/a0;a2=(1-al)/a0
    y=np.zeros_like(x);z1,z2=zi
    for i,xi in enumerate(x):
        yi=b0*xi+z1;z1=b1*xi-a1*yi+z2;z2=b2*xi-a2*yi;y[i]=yi
    return y,(z1,z2)

def nsdf_period(x,tmin,tmax,k=0.9):
    x=x-np.mean(x);W=len(x);taus=np.arange(tmin,min(tmax,W//2));n=np.zeros(len(taus))
    for i,tau in enumerate(taus):
        m=(W-tau)//2;j0=W//2-m
        a=x[j0:j0+m];b=x[j0+tau:j0+tau+m];r=np.sum(a*b);mm=np.sum(a*a+b*b)
        n[i]=2*r/mm if mm>0 else 0
    keys=[];i=1
    while i<len(n)-1:
        if n[i-1]<0 and n[i]>=0:
            j=i;best=i
            while j<len(n)-1 and not(n[j]>0 and n[j+1]<=0):
                if n[j]>n[best]:best=j
                j+=1
            if n[best]>0:keys.append(best)
            i=j+1
        else:i+=1
    if not keys:return 0.0,0.0
    nmax=max(n[q] for q in keys);thr=k*nmax
    for q in keys:
        if n[q]>=thr:return float(taus[q]),float(nmax)
    return float(taus[keys[0]]),float(nmax)

# Full tracker-gated divider (mirrors PitchBlock.grainDown Option A)
kDetLp=800.0;kHyst=0.25;kSiLeak=0.05;kGateFrac=0.75;kSub=2.8;kClarLo=0.55;kClarHi=0.75
def clamp01(v):return max(0.0,min(1.0,v))
def cf(ms):return 1.0-np.exp(-1.0/(max(0.05,ms)*0.001*SR))
def run(x):
    N=len(x);out=np.zeros(N)
    d1=lp2;  # placeholder
    # filter whole signal for detection (state continuous)
    d_full,_=lp2(x,kDetLp);d_full,_=lp2(d_full,kDetLp)
    # per-block NSDF tracking (block=128), T0 from a ~3-period-ish 1500-sample window of raw x
    blk=128;win=1536
    peak=0.0;armed=False;ff1=False;since=0;gate=0.0;clar=0.0
    pa,pr,gc,cc=cf(1),cf(300),cf(5),cf(8)
    T0=0.0;voiced=False;clarity=0.0
    o1lp=(0.0,0.0);tn=(0.0,0.0)
    # precompute output filters sample by sample below (use simple 1-pole? keep 2-pole state)
    def bq(fc,Q=0.707):
        w=2*np.pi*fc/SR;cs=np.cos(w);sn=np.sin(w);al=sn/(2*Q);a0=1+al
        return ((1-cs)/2/a0,(1-cs)/a0,(1-cs)/2/a0,(-2*cs)/a0,(1-al)/a0)
    ob=bq(2800.0);tb=bq(500*2**(0.5*4))
    oz1=oz2=tz1=tz2=0.0
    for i in range(N):
        if i%blk==0:
            s=max(0,i-win)
            seg=x[s:i] if i-s>=400 else x[max(0,i-400):i+1]
            if len(seg)>200 and np.sqrt(np.mean(seg**2))>1e-3:
                T0,clarity=nsdf_period(seg,20,900)
                voiced=T0>4.0
            else:
                voiced=False;clarity=0.0
        d=d_full[i];ad=abs(d)
        peak+= (pa if ad>peak else pr)*(ad-peak)
        hy=kHyst*peak;active=peak>5e-4
        since+=1;falling=False
        if active:
            if (not armed) and d>hy:armed=True
            elif armed and d<-hy:armed=False;falling=True
        else:armed=False
        if falling:
            if (not voiced) or since>=int(kGateFrac*T0):
                since=0;ff1=not ff1
        hw=d if d>0 else kSiLeak*d
        oo1=hw*(1.0 if ff1 else -1.0)
        # output 2-pole anti-hash
        y=ob[0]*oo1+oz1;oz1=ob[1]*oo1-ob[3]*y+oz2;oz2=ob[2]*oo1-ob[4]*y
        y2=tb[0]*y+tz1;tz1=tb[1]*y-tb[3]*y2+tz2;tz2=tb[2]*y-tb[4]*y2
        gate+=gc*((1.0 if active else 0.0)-gate)
        ct=clamp01((clarity-kClarLo)/(kClarHi-kClarLo)) if voiced else 0.0
        clar+=cc*(ct-clar)
        out[i]=y2*gate*clar*kSub
    return out

def jumps(out,f):
    w=int(SR*0.15);J=0;T=0
    for s in range(w,len(out)-w,w):
        seg=out[s:s+w];t=np.arange(len(seg))
        def e(fr):
            a=2*np.pi*fr*(s+t)/SR;return (np.sum(seg*np.cos(a))**2+np.sum(seg*np.sin(a))**2)/len(seg)**2
        h=e(f/2);g=e(f);T+=1
        if g>h:J+=1
    return J,T

# Case 1: the "atrocious" beating-harmonic sustain, 110 Hz
f=110;t=np.arange(int(SR*3))/SR
a2=0.85+0.45*np.sin(2*np.pi*0.8*t)
x1=(0.3*(np.sin(2*np.pi*f*t)+a2*np.sin(2*np.pi*(2*f+0.6)*t)+0.25*np.sin(2*np.pi*3*f*t))).astype(np.float32)
J,T=jumps(run(x1),f);print(f"beating 110Hz sustain: {J}/{T} octave-jumps  -> {'PASS' if J==0 else 'FAIL'}")
# Case 2: worst-case low-E beating
f=82.4;t=np.arange(int(SR*3))/SR;a2=0.7+0.5*np.sin(2*np.pi*1.1*t)
x2=(0.3*(np.sin(2*np.pi*f*t)+a2*np.sin(2*np.pi*(2*f+0.4)*t)+0.3*np.sin(2*np.pi*3*f*t))).astype(np.float32)
J,T=jumps(run(x2),f);print(f"beating low-E sustain: {J}/{T} octave-jumps  -> {'PASS' if J==0 else 'FAIL'}")
# Case 3: clean single note still produces f/2
f=147;t=np.arange(int(SR*1.2))/SR
x3=(0.3*(np.sin(2*np.pi*f*t)+0.5*np.sin(2*np.pi*2*f*t))).astype(np.float32)
o=run(x3)
def e(seg,fr,off):
    tt=np.arange(len(seg));a=2*np.pi*fr*(off+tt)/SR;return (np.sum(seg*np.cos(a))**2+np.sum(seg*np.sin(a))**2)/len(seg)**2
half=e(o[len(o)//3:],f/2,len(o)//3);fund=e(o[len(o)//3:],f,len(o)//3)
print(f"clean 147Hz note: f/2 {half:.2e} vs f {fund:.2e} -> {'PASS' if half>fund*5 else 'FAIL'}")
