import numpy as np, subprocess, os, reverb_battery as rb
KNOBS=[0.5,0.7,1.0,1.5,2.0,2.5,3.0,3.5,4.0,4.5]
RVT60={0.5:0.81,0.7:1.34,1.0:1.87,1.5:2.45,2.0:2.93,2.5:3.16,3.0:3.42,3.5:3.59,4.0:3.78,4.5:3.90}
_refcache={}
def ref_curve(k):
    if k not in _refcache:
        s,sr=rb.read_wav(f"ir/vintage-plate-{k}s.wav")
        mono=(s[:,0]+s[:,1])/2 if s.ndim==2 else s
        _refcache[k]=rb.per_band_decay(mono,8.0,-5,-35)
    return _refcache[k]
def render(k,a1,b1,a2,b2,g2p=1.7,exe="./render_fit"):
    out=f"/tmp/f_{k}.f32"
    env=dict(os.environ); env["RV_T60"]=str(RVT60[k])
    env["FIT_A1"]=str(a1);env["FIT_B1"]=str(b1);env["FIT_A2"]=str(a2);env["FIT_B2"]=str(b2);env["FIT_G2P"]=str(g2p)
    subprocess.run([exe,"plate","impulse.f32",out],env=env,stderr=subprocess.DEVNULL,check=True)
    a=np.fromfile(out,dtype='<f4').reshape(-1,2)
    mono=(a[:,0]+a[:,1])/2
    return mono
def curve(k,*p):
    m=render(k,*p)
    if not np.all(np.isfinite(m)): return None,float('inf')
    return rb.per_band_decay(m,8.0,-5,-35), float(np.max(np.abs(m)))
# weighted error: prioritize 125Hz..1kHz (idx1..7), 62Hz idx0 low weight, 11k idx14 low weight
W=np.array([0.3,1.0,1.0,1.0,1.0,1.0,1.0,1.0,0.6,0.4,0.3,0.3,0.3,0.3,0.2])
def werr(our,ref):
    return float(np.sum(W*np.abs(our-ref))/np.sum(W))

def optimize(k, p0, steps, weighted=True, iters=6):
    ref=ref_curve(k)
    def cost(p):
        our,mx=curve(k,*p)
        if our is None or mx>4.0 or mx!=mx: return 1e9
        return werr(our,ref) if weighted else float(np.mean(np.abs(our-ref)))
    p=list(p0); c=cost(p)
    for it in range(iters):
        improved=False
        for j in range(len(p)):
            for d in (steps[j],-steps[j]):
                q=list(p); q[j]=max(0.0,q[j]+d)
                cc=cost(q)
                if cc<c-1e-4: p,c=q,cc; improved=True
        steps=[s*0.6 for s in steps]
        if not improved and all(s<1e-6 for s in steps): break
    return p,c
