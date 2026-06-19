# Can each filter hit an ARBITRARY decay curve? Smooth studio: both fine. But a
# real IR can have a NON-MONOTONIC T60(f) (e.g. a mid resonance ringing longer).
# v1's fixed (broadband + 2 shelves + 1 peak) has too few DOF; the two-stage GEQ
# has a band per feature. Fit both to a synthetic bumpy target; compare error.
import numpy as np
from geq_twostage import peak,magdb,SR,BANDS
FC=np.concatenate(([62.5],BANDS,[16000.])); Q=0.7; DC60=4.55
# synthetic target: smooth fall + a +0.8s bump at 1.4k + a dip at 400
bump=0.9*np.exp(-0.5*((np.log(BANDS)-np.log(1400))/0.25)**2)
dip =-0.6*np.exp(-0.5*((np.log(BANDS)-np.log(400))/0.22)**2)
TGT=np.array([4.4,4.1,3.8,3.5,3.2,2.9,2.7,2.5,2.3,2.15,2.0,1.85,1.7,1.6])+bump+dip
def geq_fit(m):
    fit=np.exp(np.linspace(np.log(70),np.log(15000),200))
    t=np.interp(np.log(fit),np.log(BANDS),TGT,left=TGT[0],right=TGT[-1])
    L0=-60.0*m/(DC60*SR); resid=(-60.0*m/(t*SR))-L0
    A=np.zeros((len(fit),len(FC)))
    for j in range(len(FC)): A[:,j]=magdb(peak(FC[j],1.0,Q),fit)
    g,_,_,_=np.linalg.lstsq(A,resid,rcond=None)
    for _ in range(5):
        cur=sum(magdb(peak(FC[j],g[j],Q),fit) for j in range(len(FC)))
        dg,_,_,_=np.linalg.lstsq(A,resid-cur,rcond=None); g=g+dg
    loss=np.array([L0+sum(magdb(peak(FC[j],g[j],Q),f) for j in range(len(FC))) for f in BANDS])
    return -60.0*m/(loss*SR)
def v1_fit(m):
    # v1 structure: broadband + 2 high-shelves + 1 peak, random-searched (as design_damp)
    w=2*np.pi*BANDS/SR
    def bq(kind,f0,gdb,Qq):
        A=10**(gdb/40);wc=2*np.pi*f0/SR;c=np.cos(wc);s=np.sin(wc)
        if kind=='hs':
            al=s/2*np.sqrt(2);sa=2*np.sqrt(A)*al;a0=(A+1)-(A-1)*c+sa
            b=[A*((A+1)+(A-1)*c+sa)/a0,-2*A*((A-1)+(A+1)*c)/a0,A*((A+1)+(A-1)*c-sa)/a0]
            a=[1,2*((A-1)-(A+1)*c)/a0,((A+1)-(A-1)*c-sa)/a0]
        else:
            al=s/(2*Qq);a0=1+al/A;b=[(1+al*A)/a0,-2*c/a0,(1-al*A)/a0];a=[1,-2*c/a0,(1-al/A)/a0]
        z=np.exp(-1j*w);return 20*np.log10(np.abs((b[0]+b[1]*z+b[2]*z*z)/(a[0]+a[1]*z+a[2]*z*z))+1e-12)
    def pred(p):
        DC,h1d,h1f,h2d,h2f,pd,pf,pq=p
        shelf=bq('hs',h1f,h1d,1)+bq('hs',h2f,h2d,1)+bq('pk',pf,pd,pq)
        L=-60.0/(DC*SR)+shelf/1000.0*(m/2851.0)  # scaled like the engine
        return 60.0/(np.abs(L)*SR)
    def cost(p):
        if p[0]<2 or p[0]>8: return 1e9
        pr=pred(p)
        if np.any(pr<=0): return 1e9
        return np.sum((np.log(pr)-np.log(TGT))**2)
    rng=np.random.default_rng(1); best=np.array([4.5,-0.2,800,-0.22,3000,-0.1,1200,0.8]); bc=cost(best)
    for it in range(80000):
        q=best+rng.normal(0,1,8)*np.array([0.2,0.08,200,0.08,500,0.08,300,0.2])*(0.3+0.7*np.exp(-it/20000))
        c=cost(q)
        if c<bc: bc=c;best=q
    return pred(best)
m=2851
g=geq_fit(m); v=v1_fit(m)
print("band    target   v1-3biq   v2-GEQ")
for b,t,a,c in zip(BANDS,TGT,v,g): print("%6.0f  %6.2f   %6.2f    %6.2f"%(b,t,a,c))
print("v1 mean|e|=%.3f max=%.3f"%(np.mean(np.abs(v-TGT)),np.max(np.abs(v-TGT))))
print("v2 mean|e|=%.3f max=%.3f"%(np.mean(np.abs(g-TGT)),np.max(np.abs(g-TGT))))
