# A/B: dry guitar through the AUTO-CAPTURED engine vs convolved with the real IR.
import numpy as np, os, subprocess, sys
from wavutil import read_wav, write_wav
import capture as C
sr=48000
dry=np.fromfile('dry.f32',dtype='<f4').reshape(-1,2)
def conv(dry,ir):
    n=len(dry)+len(ir)-1; nfft=1<<int(np.ceil(np.log2(n))); out=np.zeros((n,2))
    for c in range(2): out[:,c]=np.fft.irfft(np.fft.rfft(dry[:,c],nfft)*np.fft.rfft(ir[:,c],nfft),nfft)[:n]
    return out
def render_guitar(env, gunit_file, tone_file, outp):
    e=dict(os.environ); e.update({k:str(v) for k,v in env.items()}); e['FDN_GUNIT']=gunit_file
    if tone_file: e['FDN_GEQ']=tone_file
    subprocess.run(['./fdnplate10','dry.f32',outp],env=e,stderr=subprocess.DEVNULL,check=True)
def capture_demo(irname, cap=None):
    irp="ir/NEVO - vintage plate, %s.wav"%irname
    if cap: os.environ['CAP_MAXT60']=str(cap)
    elif 'CAP_MAXT60' in os.environ: del os.environ['CAP_MAXT60']
    ir,_=read_wav(irp); m=(ir[:,0]+ir[:,1])/2
    win=min(14.0,max(3.0,C.ir_len_s(m)))
    t60_fc,DC60,gunit,env=C.capture(irp)
    C.render('cap_struct.f32','fit_gunit.txt',env)             # for the tone-corrector
    lines=C.tone_corrector(irp,'cap_struct.f32',win)
    render_guitar(env,'fit_gunit.txt','tone_geq.txt','wcap.f32')
    wet_cap=np.fromfile('wcap.f32',dtype='<f4').reshape(-1,2)
    wet_real=conv(dry, ir[:,:2])
    L=len(dry)
    def fit(x): return np.vstack([x,np.zeros((L-len(x),2))]) if len(x)<L else x[:L]
    wet_cap,wet_real=fit(wet_cap),fit(wet_real)
    def rms(x): return np.sqrt(np.mean(x**2)+1e-20)
    wet_cap*= rms(wet_real)/rms(wet_cap)                       # loudness-match to real
    root='capture_demos/%s'%irname; 
    for nm,w in [('0_real-IR-convolution',wet_real),('1_our-auto-capture',wet_cap)]:
        d=os.path.join(root,nm); os.makedirs(d,exist_ok=True)
        g=0.97/max(np.max(np.abs(0.55*dry+0.45*wet_real)),np.max(np.abs(0.55*dry+0.45*wet_cap)))
        write_wav(os.path.join(d,'guitar_mix.wav'),(0.55*dry+0.45*w)*g,sr)
        write_wav(os.path.join(d,'wet_only.wav'),w/(np.max(np.abs(w))+1e-9)*0.97,sr)
    print("%s: DC60=%.2f bright=%.0f lowcut=%.0f tone=%d bands"%(irname,DC60,env['FDN_BRIGHT'],env['FDN_LOWCUT'],len(lines)))
if __name__=="__main__":
    capture_demo("2.0s")
    capture_demo("4.5s", cap=4.5)
