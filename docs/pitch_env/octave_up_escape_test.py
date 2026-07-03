import numpy as np
SR=48000.0; DEC=4; DFS=SR/DEC; W=512; HOP=64
MINLAG=max(2,int(DFS/1400)); MAXLAG=min(W-1,int(DFS/55)+1)
def nsdf_at(buf,tau):
    lim=len(buf)-tau; a=buf[:lim]; b=buf[tau:tau+lim]
    r=np.dot(a,b); m=np.dot(a,a)+np.dot(b,b); return (2*r/m) if m>0 else 0.0
def full_acquire(buf,k=0.9):
    vals=np.array([nsdf_at(buf,t) for t in range(MINLAG,MAXLAG+1)]); gmax=vals.max()
    if gmax<=0:return -1
    thr=k*gmax; tau=0
    while tau<len(vals):
        if vals[tau]>0:
            lm=vals[tau];lp=tau
            while tau<len(vals) and vals[tau]>0:
                if vals[tau]>lm:lm=vals[tau];lp=tau
                tau+=1
            if lm>=thr:return MINLAG+lp
        else:tau+=1
    return -1
def seeded(buf,ll):
    lo=max(MINLAG,int(np.floor(ll*0.94)));hi=min(MAXLAG,int(np.ceil(ll*1.06)));best=-1;bv=-2
    for t in range(lo,hi+1):
        v=nsdf_at(buf,t)
        if v>bv:bv=v;best=t
    return (best,bv) if bv>0 else (-1,0)
OCTUP=0.95
class Tracker:
    def __init__(s):
        s.lastlag=0.0;s.reacq=0;s.z1=s.z2=0;s.ring=np.zeros(W);s.wp=0;s.filled=0
        s.dc=0;s.sh=0;s.periodInput=0.0;s.clar=0.0;s.voiced=False
        w=2*np.pi*(0.4*DFS)/SR;cs=np.cos(w);sn=np.sin(w);al=sn/(2*0.707);a0=1+al
        s.b0=(1-cs)/2/a0;s.b1=(1-cs)/a0;s.b2=(1-cs)/2/a0;s.a1=(-2*cs)/a0;s.a2=(1-al)/a0
    def note_onset(s):pass
    def process(s,x):
        for xi in x:
            y=s.b0*xi+s.z1;s.z1=s.b1*xi-s.a1*y+s.z2;s.z2=s.b2*xi-s.a2*y;s.dc+=1
            if s.dc>=DEC:
                s.dc=0;s.ring[s.wp]=y;s.wp=(s.wp+1)%W
                if s.filled<W:s.filled+=1
                s.sh+=1
                if s.sh>=HOP:
                    s.sh=0
                    if s.filled>=W:s.analyze()
    def analyze(s):
        buf=np.concatenate([s.ring[s.wp:],s.ring[:s.wp]])
        if np.dot(buf,buf)<1e-6:s.voiced=False;s.periodInput=0;s.clar=0;return
        reacq=s.reacq>0
        if s.reacq>0:s.reacq-=1
        lag=-1;clar=0
        if s.lastlag>0 and not reacq:
            pk,c=seeded(buf,s.lastlag)
            if pk>0 and c>=0.60:
                lag=pk;clar=c
                # OCTAVE-UP ESCAPE: a note that jumped up an octave still correlates at the
                # old lag (periodic at L too) so the seeded search self-traps. Its TRUE period
                # L/2 then also correlates ~perfectly, whereas a mere strong 2nd harmonic leaves
                # the fundamental in place and NSDF(L/2) stays < ~0.92. High NSDF(L/2) => follow.
                hl=int(round(pk*0.5))
                if hl>=MINLAG:
                    ch=nsdf_at(buf,hl)
                    if ch>=OCTUP:lag=hl;clar=ch
        if lag<0:
            key=full_acquire(buf)
            if key>0:lag=key;clar=nsdf_at(buf,key)
        if lag<=0 or clar<0.60:s.voiced=False;s.periodInput=0;s.clar=0;return
        if s.lastlag>0 and clar<0.93 and not reacq:
            r=lag/s.lastlag
            if abs(r-0.5)<0.06 or abs(r-2.0)<0.12:lag=s.lastlag
        s.lastlag=lag;s.periodInput=lag*DEC;s.clar=clar;s.voiced=True
def make(f1,f2,d=0.6):
    n=int(SR*d)
    def note(f,n):
        t=np.arange(n)/SR;env=np.minimum(1.0,t/0.004)*np.exp(-t*1.2)
        return env*(np.sin(2*np.pi*f*t)+0.7*np.sin(2*np.pi*2*f*t)+0.3*np.sin(2*np.pi*3*f*t))
    return (np.concatenate([note(f1,n),note(f2,n)])*0.3).astype(np.float32)
def track_report(x,use_onset):
    tr=Tracker();blk=128;N=len(x);log=[]
    envF=envS=0;aF=1-np.exp(-1/(1*.001*SR));rF=1-np.exp(-1/(60*.001*SR));sS=1-np.exp(-1/(120*.001*SR));latch=False
    for i in range(0,N,blk):
        block=x[i:i+blk]
        if use_onset:
            for xi in block:
                axr=abs(xi);envF+=(aF if axr>envF else rF)*(axr-envF);envS+=sS*(axr-envS)
                ratio=envF/(envS+1e-6)
                if (not latch) and ratio>1.8 and envF>5e-4:latch=True;tr.note_onset()
                if latch and ratio<1.2:latch=False
        tr.process(block)
        hz=DFS/(tr.periodInput/DEC) if tr.periodInput>0 else 0
        log.append((i/SR,hz))
    return log
for (f1,f2,name) in [(110,220,"oct UP 110->220"),(220,110,"oct DOWN 220->110"),(147,294,"oct UP D3->D4"),(196,130,"P4 down 196->130")]:
    x=make(f1,f2)
    row=[]
    for label,uo in [("no-fix",False),("fix",True)]:
        log=track_report(x,uo)
        hz_at=[h for (t,h) in log if 0.85<t<0.95 and h>0]
        med=np.median(hz_at) if hz_at else 0
        ok=abs(med-f2)<max(20,f2*0.08)
        row.append(f"{label}: ~{med:6.0f}Hz {'FOLLOWS' if ok else 'STUCK '}")
    print(f"{name:20} | "+" | ".join(row))
# Regression: a SUSTAINED single note with a growing 2nd harmonic must NOT trigger reacquire (no onset).
def sustain_beat(f=110,d=2.0):
    n=int(SR*d);t=np.arange(n)/SR;a2=0.85+0.45*np.sin(2*np.pi*0.8*t)
    return (0.3*(np.sin(2*np.pi*f*t)+a2*np.sin(2*np.pi*(2*f+0.6)*t)+0.25*np.sin(2*np.pi*3*f*t))).astype(np.float32)
x=sustain_beat()
log=track_report(x,True)
hz_at=[h for (t,h) in log if t>0.5 and h>0]
frac_at_f=np.mean([abs(h-110)<20 for h in hz_at]) if hz_at else 0
print(f"{'sustained 110 beat':20} | fix: {100*frac_at_f:.0f}% of frames tracked ~110 (want ~100, no false reacquire)")
