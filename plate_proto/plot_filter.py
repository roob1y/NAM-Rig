import numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
from geq_twostage import peak,magdb,SR,BANDS,TGT,v1_t60,realized_t60
FC=np.concatenate(([62.5],BANDS,[16000.])); Q=0.7; DC60=4.55
def floor_t60(g,m,grid):
    L0=-60.0*m/(DC60*SR)
    loss=np.array([L0+sum(magdb(peak(FC[j],g[j],Q),f) for j in range(len(FC))) for f in grid])
    return -60.0*m/(loss*SR)
g=np.load('geq_gunit.npy')*2851
fine=np.exp(np.linspace(np.log(90),np.log(13000),120)); tgt=np.interp(np.log(fine),np.log(BANDS),TGT)
fig,ax=plt.subplots(1,2,figsize=(13,5))
ax[0].semilogx(BANDS,TGT,'ko',ms=6,label='studio target')
ax[0].semilogx(fine,v1_t60(fine),'C1',lw=2,label='v1 3-biquad (mean .016/max .124 s)')
ax[0].semilogx(fine,floor_t60(g,2851,fine),'C0',lw=2,label='v2 two-stage GEQ (mean .006/max .029 s)')
ax[0].set_title('Smooth studio T60(f): both accurate (v2 ~3x better)'); ax[0].set_xlabel('Hz'); ax[0].set_ylabel('T60 s'); ax[0].legend(fontsize=8); ax[0].grid(True,alpha=.3)
# read measured rendered curves
import subprocess
# panel 2: generalization on a bumpy target (numbers from generalize_test.py)
b=BANDS
T=np.array([4.40,4.10,3.74,2.98,2.84,2.90,3.06,3.40,2.63,2.17,2.00,1.85,1.70,1.60])
V1=np.array([4.38,4.08,3.66,3.12,2.80,2.89,3.14,3.19,2.76,2.17,1.85,1.76,1.73,1.73])
V2=np.array([4.36,4.10,3.70,3.04,2.85,2.91,3.08,3.30,2.65,2.20,2.00,1.85,1.70,1.61])
ax[1].semilogx(b,T,'ko-',ms=6,label='arbitrary target (bump+dip)')
ax[1].semilogx(b,V1,'C1s--',label='v1 3-biquad (mean .082/max .207 s)')
ax[1].semilogx(b,V2,'C0^--',label='v2 GEQ (mean .025/max .098 s)')
ax[1].set_title('Arbitrary IR shape: v2 tracks it, v1 cannot'); ax[1].set_xlabel('Hz'); ax[1].set_ylabel('T60 s'); ax[1].legend(fontsize=8); ax[1].grid(True,alpha=.3)
plt.tight_layout(); plt.savefig('v2_filter_accuracy.png',dpi=110); print("saved")
