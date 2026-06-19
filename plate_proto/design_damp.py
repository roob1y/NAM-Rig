# Fit the per-line absorptive damping (broadband gain + 2 high-shelves + 1 peak)
# to the documented studio T60(f). Predicts T60(f) analytically from the biquad
# magnitudes (no rendering): per-sample loss L(f)=-60/(DC60*SR)+shelfdB(f)/1000,
# T60(f)=60/(|L(f)|*SR). Optimises params by random search + coordinate refine.
import numpy as np
SR=48000.0
BANDS=np.array([125,180,250,355,500,710,1000,1400,2000,2800,4000,5600,8000,11000.])
TGT  =np.array([4.41,4.17,3.97,3.57,3.34,3.09,2.93,2.67,2.38,2.27,2.06,1.80,1.66,1.63])
w=2*np.pi*BANDS/SR
def bqmag(kind,f0,gdb,Q):
    A=10**(gdb/40.0); wc=2*np.pi*f0/SR; c=np.cos(wc); s=np.sin(wc)
    if kind=='hs':
        al=s/2*np.sqrt((A+1/A)*(1/1.0-1)+2); sa=2*np.sqrt(A)*al
        a0=(A+1)-(A-1)*c+sa
        b0=A*((A+1)+(A-1)*c+sa)/a0; b1=-2*A*((A-1)+(A+1)*c)/a0; b2=A*((A+1)+(A-1)*c-sa)/a0
        a1=2*((A-1)-(A+1)*c)/a0; a2=((A+1)-(A-1)*c-sa)/a0
    else: # peak
        al=s/(2*Q); a0=1+al/A
        b0=(1+al*A)/a0; b1=(-2*c)/a0; b2=(1-al*A)/a0; a1=(-2*c)/a0; a2=(1-al/A)/a0
    z=np.exp(-1j*w)
    H=(b0+b1*z+b2*z*z)/(1+a1*z+a2*z*z)
    return 20*np.log10(np.abs(H)+1e-12)
def predict(p):
    DC60,h1d,h1f,h2d,h2f,pkd,pkf,pkq=p
    shelf=bqmag('hs',h1f,h1d,1)+bqmag('hs',h2f,h2d,1)+bqmag('pk',pkf,pkd,pkq)
    L=-60.0/(DC60*SR)+shelf/1000.0           # dB per sample (negative)
    return 60.0/(np.abs(L)*SR)
def cost(p):
    if p[0]<2 or p[0]>8: return 1e9
    if p[1]>0 or p[3]>0: return 1e9       # shelves must cut
    pr=predict(p)
    if np.any(pr<=0) or np.any(~np.isfinite(pr)): return 1e9
    return np.sum((np.log(pr)-np.log(TGT))**2)   # log error = match ratio throughout
# init from hand-tuned A region
p=np.array([4.5,-0.20,800,-0.22,3000,-0.10,1200,0.8])
lo=np.array([3.5,-0.8,200,-0.8,1500,-0.5,300,0.4])
hi=np.array([6.5, 0.0,2500, 0.0,9000, 0.0,4000,2.0])
rng=np.random.default_rng(1); best=p.copy(); bc=cost(p)
for it in range(60000):
    q=best+rng.normal(0,1,8)*np.array([0.15,0.05,150,0.05,400,0.04,200,0.15])*(0.3+0.7*np.exp(-it/15000))
    q=np.clip(q,lo,hi); c=cost(q)
    if c<bc: bc=c; best=q
print("cost",round(bc,5))
names=['DC60','HS1_DB','HS1_F','HS2_DB','HS2_F','PK_DB','PK_F','PK_Q']
for n,v in zip(names,best): print(f"  {n:7s}={v:.4f}")
pr=predict(best)
print("band   tgt   pred")
for b,t,pp in zip(BANDS,TGT,pr): print(f"{b:6.0f} {t:5.2f} {pp:5.2f}")
print("ENV: "+" ".join(f"FDN_{n}={v:.4f}" for n,v in zip(names,best)))
