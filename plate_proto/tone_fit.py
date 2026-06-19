# Output tone-corrector via the two-stage interaction-matrix GEQ (not per-band):
# match the IR's integrated 1/3-oct spectrum. Designed so the cascade passes through
# the residual at every band center without the per-band overshoot.
import numpy as np
from geq_twostage import peak, magdb
sr=48000.0
def spec(x,cf):
    on=np.argmax(np.abs(x)>0.01*np.max(np.abs(x))); seg=x[on:on+int(sr*3)]
    X=np.abs(np.fft.rfft(seg*np.hanning(len(seg))))**2; f=np.fft.rfftfreq(len(seg),1/sr)
    v=np.array([10*np.log10(np.mean(X[(f>=c/2**(1/6))&(f<c*2**(1/6))])+1e-30) for c in cf])
    return v-v[np.argmin(np.abs(cf-1000))]
def design_tone(ir, ours, Q=1.4):
    FC=np.array([63,125,250,500,1000,2000,4000,8000,12500.])
    target=spec(ir,FC)-spec(ours,FC)          # residual we must ADD (dB), already re 1k
    # interaction matrix: dB of unit band j at center k
    B=np.zeros((len(FC),len(FC)))
    for j in range(len(FC)):
        cj=peak(FC[j],1.0,Q)
        for k in range(len(FC)): B[k,j]=magdb(cj,FC[k])
    g=np.linalg.solve(B,target)
    for _ in range(3):
        cur=np.array([sum(magdb(peak(FC[j],g[j],Q),FC[k]) for j in range(len(FC))) for k in range(len(FC))])
        g=g+np.linalg.solve(B,target-cur)
    lines=[(FC[j],Q,g[j]) for j in range(len(FC))]
    with open('tone_geq.txt','w') as f:
        for fq,q,gd in lines:
            f.write("%.1f %.3f %.3f\n"%(fq,q,gd))
    return lines
