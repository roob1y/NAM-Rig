# Valimaki-Liski accurate cascade GEQ as the FDN per-line ATTENUATION filter.
# Two-stage: prototype peaking biquads + interaction-matrix gain solve so the
# cascade passes through the target loss at every centre. Target loss per loop
# L(f) dB = -60*m/(T60(f)*SR); scales with line length m (solve once, scale).
import numpy as np
SR=48000.0
BANDS=np.array([125,180,250,355,500,710,1000,1400,2000,2800,4000,5600,8000,11000.])
TGT  =np.array([4.41,4.17,3.97,3.57,3.34,3.09,2.93,2.67,2.38,2.27,2.06,1.80,1.66,1.63])
# GEQ centres = measurement grid + gentle sub/air ends (extrapolate target).
FC=np.concatenate(([62.5],BANDS,[16000.]))
TFC=np.concatenate(([TGT[0]*1.02],TGT,[TGT[-1]*0.98]))
def peak(f0,gdb,Q):
    A=10**(gdb/40.0); w=2*np.pi*f0/SR; c=np.cos(w); s=np.sin(w); al=s/(2*Q)
    a0=1+al/A
    return (np.array([(1+al*A)/a0,-2*c/a0,(1-al*A)/a0]),np.array([1.0,-2*c/a0,(1-al/A)/a0]))
def magdb(coef,f):
    b,a=coef; z=np.exp(-1j*2*np.pi*f/SR)
    H=(b[0]+b[1]*z+b[2]*z*z)/(a[0]+a[1]*z+a[2]*z*z); return 20*np.log10(np.abs(H)+1e-12)
def design(m,Q,t60_fc=TFC,n_refine=3):
    target=-60.0*m/(t60_fc*SR)
    B=np.zeros((len(FC),len(FC)))
    for j,fj in enumerate(FC):
        cj=peak(fj,1.0,Q)
        for k,fk in enumerate(FC): B[k,j]=magdb(cj,fk)
    g=np.linalg.solve(B,target)
    for _ in range(n_refine):
        cur=np.array([sum(magdb(peak(FC[j],g[j],Q),fk) for j in range(len(FC))) for fk in FC])
        g=g+np.linalg.solve(B,target-cur)
    return [peak(FC[j],g[j],Q) for j in range(len(FC))]
def cascade_db(filters,f): return sum(magdb(c,f) for c in filters)
def realized_t60(filters,m,grid): 
    loss=np.array([cascade_db(filters,f) for f in grid]); return -60.0*m/(loss*SR)

# v1 design_damp fitted defaults (fdnplate7) — predict its T60(f) the same way
def v1_t60(grid):
    DC60,h1d,h1f,h2d,h2f,pkd,pkf,pkq=4.6822,-0.2302,1562.40,-0.2720,4834.38,-0.1295,845.03,0.40
    def hs(f0,gdb,f):
        A=10**(gdb/40); w=2*np.pi*f0/SR; c=np.cos(w); s=np.sin(w)
        al=s/2*np.sqrt((A+1/A)*0+2); sa=2*np.sqrt(A)*al; a0=(A+1)-(A-1)*c+sa
        b=[A*((A+1)+(A-1)*c+sa)/a0,-2*A*((A-1)+(A+1)*c)/a0,A*((A+1)+(A-1)*c-sa)/a0]
        a=[1,2*((A-1)-(A+1)*c)/a0,((A+1)-(A-1)*c-sa)/a0]; z=np.exp(-1j*2*np.pi*f/SR)
        return 20*np.log10(np.abs((b[0]+b[1]*z+b[2]*z*z)/(a[0]+a[1]*z+a[2]*z*z))+1e-12)
    def pk(f0,Q,gdb,f):
        A=10**(gdb/40);w=2*np.pi*f0/SR;c=np.cos(w);s=np.sin(w);al=s/(2*Q);a0=1+al/A
        b=[(1+al*A)/a0,-2*c/a0,(1-al*A)/a0];a=[1,-2*c/a0,(1-al/A)/a0];z=np.exp(-1j*2*np.pi*f/SR)
        return 20*np.log10(np.abs((b[0]+b[1]*z+b[2]*z*z)/(a[0]+a[1]*z+a[2]*z*z))+1e-12)
    shelf=hs(h1f,h1d,grid)+hs(h2f,h2d,grid)+pk(pkf,pkq,pkd,grid)
    L=-60.0/(DC60*SR)+shelf/1000.0
    return 60.0/(np.abs(L)*SR)

if __name__=="__main__":
    P=[337,389,431,479,523,571,619,661,709,761,811,857,907,953,1009,1061,1103,1153,1201,1259,1301,1361,1409,1459,1511,1559,1607,1657,1709,1759,1811,1867,1913,1973,2017,2069,2113,2161,2213,2267,2309,2357,2411,2467,2521,2579,2621,2677,2729,2777,2833,2887,2939,2999,3041,3089,3137,3187,3251,3301,3347,3391,3457]
    dl=[int(round(p*1.5))|1 for p in P[:64]]; mref=int(np.mean(dl))
    fine=np.exp(np.linspace(np.log(90),np.log(13000),60))
    tgt_fine=np.interp(np.log(fine),np.log(BANDS),TGT)
    v1=v1_t60(fine); e1=v1-tgt_fine
    print("v1 (3-biquad random search):  mean|err|=%.3fs  max|err|=%.3fs"%(np.mean(np.abs(e1)),np.max(np.abs(e1))))
    for Q in [1.4,2.0,2.5,2.9,3.5]:
        f=design(mref,Q); rt=realized_t60(f,mref,fine); e2=rt-tgt_fine
        print("v2 GEQ Q=%.1f: mean|err|=%.3fs  max|err|=%.3fs"%(Q,np.mean(np.abs(e2)),np.max(np.abs(e2))))
