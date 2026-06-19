# Least-squares two-stage GEQ: fit command gains so the cascade dB-loss matches
# the target loss over a DENSE grid (overdetermined), not just at band centres.
# Better-conditioned for a smooth target; avoids between-band sag.
import numpy as np
from geq_twostage import peak,magdb,SR,BANDS,TGT,FC,TFC,v1_t60,realized_t60
def design_ls(m,Q,n_refine=4):
    fit=np.exp(np.linspace(np.log(70),np.log(15000),200))
    target_fit=-60.0*m/(np.interp(np.log(fit),np.log(BANDS),TGT,left=TGT[0],right=TGT[-1])*SR)
    # interaction matrix on the dense grid: A[i,j]=dB of unit filter j at fit[i]
    A=np.zeros((len(fit),len(FC)))
    for j in range(len(FC)): A[:,j]=magdb(peak(FC[j],1.0,Q),fit)
    g,_,_,_=np.linalg.lstsq(A,target_fit,rcond=None)
    for _ in range(n_refine):
        cur=np.zeros(len(fit))
        for j in range(len(FC)): cur+=magdb(peak(FC[j],g[j],Q),fit)
        dg,_,_,_=np.linalg.lstsq(A,target_fit-cur,rcond=None); g=g+dg
    return [peak(FC[j],g[j],Q) for j in range(len(FC))],g
if __name__=="__main__":
    P=[337,389,431,479,523,571,619,661,709,761,811,857,907,953,1009,1061,1103,1153,1201,1259,1301,1361,1409,1459,1511,1559,1607,1657,1709,1759,1811,1867,1913,1973,2017,2069,2113,2161,2213,2267,2309,2357,2411,2467,2521,2579,2621,2677,2729,2777,2833,2887,2939,2999,3041,3089,3137,3187,3251,3301,3347,3391,3457]
    dl=[int(round(p*1.5))|1 for p in P[:64]]; mref=int(np.mean(dl))
    fine=np.exp(np.linspace(np.log(90),np.log(13000),60)); tgt=np.interp(np.log(fine),np.log(BANDS),TGT)
    e1=v1_t60(fine)-tgt
    print("v1 (3-biquad random search):       mean|err|=%.3fs max=%.3fs"%(np.mean(np.abs(e1)),np.max(np.abs(e1))))
    for Q in [0.7,1.0,1.4,2.0]:
        f,g=design_ls(mref,Q); e2=realized_t60(f,mref,fine)-tgt
        print("v2 GEQ-LS Q=%.1f (%d bands): mean|err|=%.3fs max=%.3fs"%(Q,len(FC),np.mean(np.abs(e2)),np.max(np.abs(e2))))
