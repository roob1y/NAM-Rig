import numpy as np, subprocess, os, reverb_battery as rb
from wavutil import read_wav
SR=rb.sr; FC=rb.FC
ORIG=open('/sessions/wonderful-funny-mendel/mnt/NAM-Rig/src/rig/ReverbBlock.h','rb').read().decode('latin-1')
rL,rR=rb.load_ref('ir/vintage-plate-1.5s.wav'); rM=(rL+rR)/2
kern,_=read_wav('../resources/plate_early_kernel.wav')
def feats(m):
    def cen(ms,w=2048):
        o=rb.onset(m);i=o+int(ms/1000*SR);seg=m[i:i+w]*np.hanning(w);X=np.abs(np.fft.rfft(seg));f=np.fft.rfftfreq(w,1/SR);s=f>40
        return np.sum(f[s]*X[s])/(np.sum(X[s])+1e-20)
    def lb(f1,f2,t1=0.3,t2=1.5):
        o=rb.onset(m);seg=m[o+int(t1*SR):o+int(t2*SR)];X=np.abs(np.fft.rfft(seg*np.hanning(len(seg))))**2;f=np.fft.rfftfreq(len(seg),1/SR);s=(f>=f1)&(f<f2);return 10*np.log10(np.mean(X[s])+1e-30)
    return [cen(300),cen(500),cen(800)], lb(2000,5000), lb(5000,12000)
def render(binp,t60):
    subprocess.run([binp,'plate','impulse.f32','/tmp/rv.f32'],env={**os.environ,'RV_T60':str(t60),'RV_DAMP':'6500','RV_SIZE':'1.2'},check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    L,Rr=rb.load_ours('/tmp/rv.f32'); L[:len(kern)]+=0.18*kern[:,0]; Rr[:len(kern)]+=0.18*kern[:,1]; return (L+Rr)/2
g1,g2=-1.15e-4,-1.9e-4
s=ORIG.replace('kDampG1 = -1.46e-4','kDampG1 = %.4e'%g1).replace('kDampG2 = -3.20e-4','kDampG2 = %.4e'%g2)
open('/tmp/ts/rig/ReverbBlock.h','wb').write(s.encode('latin-1'))
assert subprocess.run(['g++','-std=c++17','-O2','-I/tmp/ts','-Istub','render_plate_plugin.cpp','-o','/tmp/rfit'],capture_output=True).returncode==0
# reference + candidate, anchor
rc,r25,r512=feats(rM); rT=rb.per_band_decay(rM,6.0,-5,-35)
fit=render('/tmp/rfit',2.45); cc,c25,c512=feats(fit); cT=rb.per_band_decay(fit,6.0,-5,-35)
cur=render('./render_plate_plugin',2.45); uc,u25,u512=feats(cur)
print("=== ANCHOR (knob 2.45) ===")
print(f"centroid 300/500/800:  ref {[round(x) for x in rc]}  current {[round(x) for x in uc]}  FIT {[round(x) for x in cc]}")
print(f"2-5k late band:  ref {r25:.1f}  current {u25:.1f}  FIT {c25:.1f} dB")
print(f"5-12k late band: ref {r512:.1f}  current {u512:.1f}  FIT {c512:.1f} dB  (top not over-energized?)")
print(f"T60(f) FIT vs ref:  @1k {cT[7]:.2f}/{rT[7]:.2f}  @2k {cT[9]:.2f}/{rT[9]:.2f}  @4k {cT[11]:.2f}/{rT[11]:.2f}  @8k {cT[13]:.2f}/{rT[13]:.2f}")
print(f"|T60 err 250-8k| FIT {np.nanmean(np.abs(cT[3:13]-rT[3:13])):.3f}  current {np.nanmean(np.abs(rb.per_band_decay(cur,6.0,-5,-35)[3:13]-rT[3:13])):.3f}")
# scale check at a different knob
f2=render('/tmp/rfit',3.16); T2=rb.per_band_decay(f2,6.0,-5,-35)
print(f"\n=== knob 3.16 scale check === FIT @1k {T2[7]:.2f} @4k {T2[11]:.2f} (should scale up sensibly, no blow-up)")
