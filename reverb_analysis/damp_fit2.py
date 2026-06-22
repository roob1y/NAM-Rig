import numpy as np, subprocess, os, reverb_battery as rb
from wavutil import read_wav
SR=rb.sr; FC=rb.FC
ORIG=open('/sessions/wonderful-funny-mendel/mnt/NAM-Rig/src/rig/ReverbBlock.h','rb').read().decode('latin-1')
rL,rR=rb.load_ref('ir/vintage-plate-1.5s.wav'); rM=(rL+rR)/2
T60r=rb.per_band_decay(rM,6.0,-5,-35); kern,_=read_wav('../resources/plate_early_kernel.wav')
def cen(m,ms,w=2048):
    o=rb.onset(m);i=o+int(ms/1000*SR);seg=m[i:i+w]*np.hanning(w);X=np.abs(np.fft.rfft(seg));f=np.fft.rfftfreq(w,1/SR);s=f>40
    return np.sum(f[s]*X[s])/(np.sum(X[s])+1e-20)
def bm(g1,g2):
    s=ORIG.replace('kDampG1 = -1.46e-4','kDampG1 = %.4e'%g1).replace('kDampG2 = -3.20e-4','kDampG2 = %.4e'%g2)
    open('/tmp/ts/rig/ReverbBlock.h','wb').write(s.encode('latin-1'))
    r=subprocess.run(['g++','-std=c++17','-O2','-I/tmp/ts','-Istub','render_plate_plugin.cpp','-o','/tmp/rf'],capture_output=True)
    if r.returncode!=0: print("BUILD FAIL",r.stderr.decode()[:300]); return None
    subprocess.run(['/tmp/rf','plate','impulse.f32','/tmp/rf.f32'],env={**os.environ,'RV_T60':'2.45','RV_DAMP':'6500','RV_SIZE':'1.2'},check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    L,Rr=rb.load_ours('/tmp/rf.f32'); L[:len(kern)]+=0.18*kern[:,0]; Rr[:len(kern)]+=0.18*kern[:,1]; m=(L+Rr)/2
    T=rb.per_band_decay(m,6.0,-5,-35); e=np.nanmean(np.abs(T[3:13]-T60r[3:13]))
    return T,e,cen(m,500)
import sys
combos=[(-1.46e-4,-3.20e-4),(-1.15e-4,-2.0e-4),(-0.95e-4,-1.3e-4),(-1.15e-4,-1.3e-4)]
print(f"ref: @1k {T60r[7]:.2f} @2k {T60r[9]:.2f} @4k {T60r[11]:.2f} @8k {T60r[13]:.2f}  cen500 {cen(rM,500):.0f}")
for g1,g2 in combos:
    r=bm(g1,g2)
    if r: T,e,c=r; print(f"G1={g1*1e4:+.2f} G2={g2*1e4:+.2f}e-4 | 250-8k err {e:.3f}s | @1k {T[7]:.2f} @2k {T[9]:.2f} @4k {T[11]:.2f} @8k {T[13]:.2f} | cen500 {c:.0f}")
