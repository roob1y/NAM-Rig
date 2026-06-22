# render_plate.py — FAITHFUL offline render of the SHIPPED plugin plate.
# plate = FDN tail (early tap 0.15, via render_plate_plugin) + 0.18*conv(dry, plate_early_kernel).
# Matches EarlyConvolver::addEarly exactly (stereo, zero-latency, no normalise). Use THIS for any
# plate ONSET analysis; the old render_character (no convolver, early tap 0.6) is dark/muffled and
# is only valid for LATE-field work. (See playbook harness-trap note.)
import os, subprocess, numpy as np, sys; sys.path.insert(0,'.')
import reverb_battery as rb, masking_abx as M
from wavutil import read_wav
SR=rb.sr
KERNEL_WAV=os.path.abspath('../resources/plate_early_kernel.wav')
EARLY_GAIN=0.14
def _kernel():
    k,_=read_wav(KERNEL_WAV); return k[:,0].copy(), k[:,1].copy()
def plate_ir(t60=2.45, damp=6500.0, size=1.2):   # damp 6500 = plugin DEFAULT Tone 0.4 over plate dampRange[1500,14000]
    """Return the faithful plugin plate IR (stereo L,R) at the given knob settings."""
    out=f"/tmp/plate_fdn_{t60}_{damp}.f32"
    subprocess.run(['./render_plate_plugin','plate','impulse.f32',out],
        env={**os.environ,'RV_T60':str(t60),'RV_DAMP':str(damp),'RV_SIZE':str(size)},
        check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    L,R=rb.load_ours(out); kL,kR=_kernel()
    # the convolver onset = kernel itself (dry=impulse) scaled by EARLY_GAIN, added at input t=0
    n=len(L); L[:len(kL)]+=EARLY_GAIN*kL; R[:len(kR)]+=EARLY_GAIN*kR
    return L,R
def plate_wet(dryL,dryR, t60=2.45, damp=6500.0, size=1.2):
    """Convolve a dry signal through the faithful plugin plate (linear -> = IR convolution)."""
    L,R=plate_ir(t60,damp,size)
    return M.fftconv(dryL,L), M.fftconv(dryR,R)
if __name__=="__main__":
    import sys
    dL,dR=rb.load_ours('dry_guitar.f32')
    yL,yR=plate_wet(dL,dR); m=(yL+yR)/2
    def cen(x):
        X=np.abs(np.fft.rfft(x*np.hanning(len(x))));f=np.fft.rfftfreq(len(x),1/SR);s=f>40
        return np.sum(f[s]*X[s])/(np.sum(X[s])+1e-20)
    rL,rR=rb.load_ref('ir/vintage-plate-1.5s.wav'); refm=M.fftconv(dL,rL)
    print(f"FAITHFUL plugin plate (with kernel)  guitar-wet centroid: {cen(m):.0f}")
    print(f"reference convolved                  guitar-wet centroid: {cen(refm):.0f}")
    print(f"(old no-convolver harness was ~3921; approved hybconv demo ~4348)")
