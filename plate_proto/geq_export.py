# Jot/Schlecht attenuation filter: broadband gain FLOOR (guarantees loss at every
# frequency incl. DC/Nyquist => stable + sub/air decay) + two-stage GEQ shaping
# the RESIDUAL loss above the floor. All GEQ command gains are then <=0 (extra cut).
import numpy as np
from geq_twostage import peak,magdb,SR,BANDS,TGT,v1_t60,realized_t60
FC=np.concatenate(([62.5],BANDS,[16000.])); Q=0.7
DC60=4.55     # broadband-floor T60 (a touch above the longest band 4.41s @125)
def design(m,Q,n_refine=5):
    L0=-60.0*m/(DC60*SR)                       # broadband floor loss (dB/loop)
    fit=np.exp(np.linspace(np.log(70),np.log(15000),200))
    t60f=np.interp(np.log(fit),np.log(BANDS),TGT,left=TGT[0],right=TGT[-1])
    resid=(-60.0*m/(t60f*SR))-L0               # <=0 residual to be added by GEQ
    A=np.zeros((len(fit),len(FC)))
    for j in range(len(FC)): A[:,j]=magdb(peak(FC[j],1.0,Q),fit)
    g,_,_,_=np.linalg.lstsq(A,resid,rcond=None)
    for _ in range(n_refine):
        cur=np.zeros(len(fit))
        for j in range(len(FC)): cur+=magdb(peak(FC[j],g[j],Q),fit)
        dg,_,_,_=np.linalg.lstsq(A,resid-cur,rcond=None); g=g+dg
    return g,L0
mref=2851
g,L0=design(mref,Q)
gunit=g/mref
# realized T60 incl. broadband floor: total loss = L0 + GEQ(f)
def realized_floor(g,m,grid):
    L0=-60.0*m/(DC60*SR)
    loss=np.array([L0+sum(magdb(peak(FC[j],g[j],Q),f) for j in range(len(FC))) for f in grid])
    return -60.0*m/(loss*SR),loss
fine=np.exp(np.linspace(np.log(90),np.log(13000),60)); tgt=np.interp(np.log(fine),np.log(BANDS),TGT)
v2,_=realized_floor(g,mref,fine); e2=v2-tgt; e1=v1_t60(fine)-tgt
# stability across full band
allf=np.linspace(20,23900,800); _,la=realized_floor(g,mref,allf)
print("DC60=%.3f  max total loss over 0..Nyquist=%.5f dB (must be<0)"%(DC60,la.max()))
print("v1: mean|e|=%.4f max=%.4f   v2(floor+GEQ): mean|e|=%.4f max=%.4f"%(np.mean(np.abs(e1)),np.max(np.abs(e1)),np.mean(np.abs(e2)),np.max(np.abs(e2))))
np.save('geq_gunit.npy',gunit)
print("GUNIT(dB/sample)=",["%.3e"%v for v in gunit])
# emit C arrays
print("static const double GEQ_FC[16]={%s};"%",".join("%.2f"%x for x in FC))
print("static const double GEQ_GUNIT[16]={%s};"%",".join("%.8e"%x for x in gunit))
print("DC60=%.4f Q=%.3f"%(DC60,Q))
