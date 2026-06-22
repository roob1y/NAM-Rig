import numpy as np, subprocess, os, reverb_battery as rb
from wavutil import read_wav
SR=rb.sr; FC=rb.FC
HDR_SRC='/sessions/wonderful-funny-mendel/mnt/NAM-Rig/src/rig/ReverbBlock.h'
ORIG=open(HDR_SRC,'rb').read().decode('latin-1')
rL,rR=rb.load_ref('ir/vintage-plate-1.5s.wav'); rM=(rL+rR)/2
T60r=rb.per_band_decay(rM,6.0,-5,-35)
kern,_=read_wav('../resources/plate_early_kernel.wav')
def cen(m,ms,w=2048):
    o=rb.onset(m);i=o+int(ms/1000*SR);seg=m[i:i+w]*np.hanning(w);X=np.abs(np.fft.rfft(seg));f=np.fft.rfftfreq(w,1/SR);s=f>40
    return np.sum(f[s]*X[s])/(np.sum(X[s])+1e-20)
def lateband(m,f1,f2,t1=0.3,t2=1.5):
    o=rb.onset(m);seg=m[o+int(t1*SR):o+int(t2*SR)];X=np.abs(np.fft.rfft(seg*np.hanning(len(seg))))**2;f=np.fft.rfftfreq(len(seg),1/SR)
    s=(f>=f1)&(f<f2);return 10*np.log10(np.mean(X[s])+1e-30)
def build_measure(g1,g2):
    s=ORIG.replace('static constexpr double kDampG1 = -1.46e-4','static constexpr double kDampG1 = %.4e'%g1)
    s=s.replace('static constexpr double kDampG2 = -3.20e-4','static constexpr double kDampG2 = %.4e'%g2)
    os.makedirs('/tmp/ts/rig',exist_ok=True)
    # copy siblings once
    if not os.path.exists('/tmp/ts/rig/.done'):
        subprocess.run('cp -r ../src/rig/* /tmp/ts/rig/',shell=True); open('/tmp/ts/rig/.done','w').close()
    open('/tmp/ts/rig/ReverbBlock.h','wb').write(s.encode('latin-1'))
    r=subprocess.run(['g++','-std=c++17','-O2','-I/tmp/ts','-Istub','render_plate_plugin.cpp','-o','/tmp/rf'],capture_output=True)
    if r.returncode!=0: return None
    subprocess.run(['/tmp/rf','plate','impulse.f32','/tmp/rf.f32'],env={**os.environ,'RV_T60':'2.45','RV_DAMP':'6500','RV_SIZE':'1.2'},check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    L,Rr=rb.load_ours('/tmp/rf.f32'); L[:len(kern)]+=0.18*kern[:,0]; Rr[:len(kern)]+=0.18*kern[:,1]; m=(L+Rr)/2
    T=rb.per_band_decay(m,6.0,-5,-35)
    derr=np.nanmean(np.abs(T[3:13]-T60r[3:13]))  # 250Hz-8k focus
    return T,derr,[cen(m,t) for t in (300,500,800)],lateband(m,5000,12000)
print("baseline (current kDampG1=-1.46e-4 kDampG2=-3.20e-4):")
T,e,c,tb=build_measure(-1.46e-4,-3.20e-4)
print(f"  250-8k T60 err {e:.3f}s  cen300/500/800 {[round(x) for x in c]}  5-12k_late {tb:.1f}dB")
print(f"  ref cen {[round(cen(rM,t)) for t in (300,500,800)]}  ref 5-12k_late {lateband(rM,5000,12000):.1f}")
print("\nfit grid (reduce |kDampG1|,|kDampG2| -> lengthen mids/highs):")
best=None
for g1 in (-1.46e-4,-1.15e-4,-0.90e-4):
  for g2 in (-3.20e-4,-2.2e-4,-1.4e-4,-0.7e-4):
    r=build_measure(g1,g2)
    if r is None: continue
    T,e,c,tb=r
    flag='' 
    print(f"  G1={g1*1e4:+.2f}e-4 G2={g2*1e4:+.2f}e-4 | 250-8k err {e:.3f}s @1k {T[7]:.2f} @4k {T[11]:.2f} | cen500 {c[1]:.0f} | 5-12k {tb:+.1f}")
    if best is None or e<best[0]: best=(e,g1,g2,T,c,tb)
print(f"\nBEST: G1={best[1]*1e4:+.2f}e-4 G2={best[2]*1e4:+.2f}e-4  250-8k err {best[0]:.3f}s (was {build_measure(-1.46e-4,-3.20e-4)[1]:.3f})")
