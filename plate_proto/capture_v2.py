# Reverb Capture v2 — HF rearchitected + closed-loop EDR/spectrum fit.
#  * wideband bright input (pass the transient) tuned to match the IR onset centroid
#  * ALL HF rolloff in the per-line two-stage decay damping, closed-loop to the
#    measured T60(f) per band (no blanket multiplier)
#  * no bright direct ER (tank diffusion carries the onset)
#  * decay capped at the reverb's max range; tone-corrector for low-mid residual
import numpy as np, os, subprocess, sys
from wavutil import read_wav, write_wav
from geq_twostage import peak, magdb
import capture as C
sr=48000.0; FC=C.FC

def render(env, gunit, outp, src='dry_imp.f32'):
    np.savetxt('gv.txt',gunit,fmt='%.8e')
    e=dict(os.environ); e.update({k:str(v) for k,v in env.items()}); e['FDN_GUNIT']='gv.txt'
    subprocess.run(['./fdnplate10',src,outp],env=e,stderr=subprocess.DEVNULL,check=True)

def t60f_of(x,win):
    on=np.argmax(np.abs(x)>0.01*np.max(np.abs(x))); seg=x[on:on+int(sr*win)]
    return np.array([C.t60(C.bp(seg,fc)) for fc in FC])

def early_centroid(x,t0=0.0):
    on=np.argmax(np.abs(x)>0.01*np.max(np.abs(x))); seg=x[on:on+int(sr*0.03)]*np.hanning(int(sr*0.03))
    X=np.abs(np.fft.rfft(seg)); f=np.fft.rfftfreq(len(seg),1/sr); return np.sum(f*X)/(np.sum(X)+1e-9)

def design_gunit(t60_target):
    DC60=float(np.nanmax(t60_target))*1.03
    fit=np.exp(np.linspace(np.log(70),np.log(15000),200)); tf=np.interp(np.log(fit),np.log(FC),t60_target)
    m=2807.0; L0=-60.0*m/(DC60*sr); resid=(-60.0*m/(tf*sr))-L0
    A=np.zeros((len(fit),len(FC)))
    for j in range(len(FC)): A[:,j]=magdb(peak(FC[j],1.0,0.7),fit)
    tilt=float(np.nanmax(t60_target)/max(np.nanmin(t60_target),1e-3))
    if tilt>=4.0:
        g=np.zeros(len(FC))
        for _ in range(40):
            dg,_,_,_=np.linalg.lstsq(A,resid-A@g,rcond=None); g=np.minimum(g+0.5*dg,0.0)
    else:
        g,_,_,_=np.linalg.lstsq(A,resid,rcond=None)
        for _ in range(6):
            dg,_,_,_=np.linalg.lstsq(A,resid-A@g,rcond=None); g=g+dg
    return DC60, g/m

def capture_v2(irpath, max_t60=None, iters=4, verbose=True):
    ir,_=read_wav(irpath); m=(ir[:,0]+ir[:,1])/2
    win=min(12.0,max(3.0,C.ir_len_s(m)))
    target=C.measure_t60f(m,win=win)        # no decay cap: match the IR's true T60(f)
    cap=float(np.nanmax(target))*2.0        # clamp ceiling for the closed-loop only (not a decay cap)
    imp=int(sr*max(8.0, cap*1.6)); a=np.zeros((imp,2),'<f4'); a[10]=1; a.tofile('dry_imp.f32')
    bright,lowcut=C.spectral_edges(m)
    # onset target from the IR
    tgt_ec=early_centroid(m)
    env={'FDN_N':64,'FDN_SIZE':1.5,'FDN_GEQ_Q':0.70,'FDN_BRIGHT':9000,'FDN_AIR':0,'FDN_AIRF':11000,
         'FDN_LOWCUT':round(lowcut,1),'FDN_DRV_LM':5.0,'FDN_DRV_LMF':220,'FDN_ER_MIX':0.0}  # ER OFF
    work=target.copy()
    for it in range(iters):
        DC60,gunit=design_gunit(work); env['FDN_DC60']=round(DC60,4)
        render(env,gunit,'v2.f32')
        r=np.fromfile('v2.f32',dtype='<f4').reshape(-1,2); rm=(r[:,0]+r[:,1])/2
        realized=t60f_of(rm,win)
        # closed loop on per-band T60 (skip unreliable bands beyond the cap/window)
        rel=(target<win*0.9)&np.isfinite(realized)
        err=realized-target
        work=np.where(rel,np.clip(work-0.6*err,0.1,cap),work)
        # onset: nudge bright toward the IR centroid
        ec=early_centroid(rm); env['FDN_BRIGHT']=float(np.clip(env['FDN_BRIGHT']*(tgt_ec/max(ec,1)),5500,9500))
        if verbose: print("  it%d: T60 mean|e|=%.3fs (reliable) | onset %dHz->tgt %dHz bright=%.0f"%(
            it,np.nanmean(np.abs(err[rel])),ec,tgt_ec,env['FDN_BRIGHT']))
    return env,gunit,target,win

if __name__=="__main__":
    irp=sys.argv[1]; mx=float(sys.argv[2]) if len(sys.argv)>2 else None
    env,gunit,target,win=capture_v2(irp,max_t60=mx)
    print("FINAL: DC60=%.2f bright=%.0f lowcut=%.0f ER=%s"%(env['FDN_DC60'],env['FDN_BRIGHT'],env['FDN_LOWCUT'],env['FDN_ER_MIX']))
