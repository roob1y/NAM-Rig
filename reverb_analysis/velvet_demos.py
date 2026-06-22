import sys,os,subprocess,wave; sys.path.insert(0,'.')
import numpy as np, reverb_battery as rb
SR=rb.sr; VT0=[2.45*1.878,2.45*2.204,2.45*2.571]
dry=np.fromfile('dry_guitar.f32',dtype='<f4').reshape(-1,2); dry.tofile('/tmp/dg.f32')
def render(v,tm):
    out=f"/tmp/vd_{v}_{tm}.f32"; e={**os.environ,"RV_T60":"2.45","HFM":"0","EARLY":"0","VLV":"1","DARKG":"0.85","DARKHZ":"5000","VLVLV":str(v)}
    for i,t in enumerate(VT0,1): e[f"V{i}T"]=str(t*tm)
    subprocess.run(["../plate_proto/render_proto","plate",os.path.abspath('/tmp/dg.f32'),out],env=e,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    return np.fromfile(out,dtype='<f4').reshape(-1,2)
def conv_ref():
    L,R=rb.load_ref("ir/vintage-plate-1.5s.wav")
    from numpy.fft import rfft,irfft
    n=len(dry)+len(L); N=1<<int(np.ceil(np.log2(n)))
    wL=irfft(rfft(dry[:,0],N)*rfft(L,N))[:len(dry)]; wR=irfft(rfft(dry[:,1],N)*rfft(R,N))[:len(dry)]
    return np.stack([wL,wR],1)
def wwav(path,wet,wmix=0.35):
    n=min(len(dry),len(wet)); m=(1-wmix)*dry[:n]+wmix*wet[:n]
    p=np.sqrt(np.mean(m**2)); m=m*(0.2/(p+1e-12)); d=(np.clip(m,-1,1)*32767).astype('<i2')
    w=wave.open(path,'wb');w.setnchannels(2);w.setsampwidth(2);w.setframerate(int(SR));w.writeframes(d.tobytes());w.close();print("wrote",path)
wwav("vdemo_0_reference.wav",conv_ref())
wwav("vdemo_1_OLD_velvet1.4.wav",render(1.4,1.0))
wwav("vdemo_2_FIT_0.6_t2.0.wav",render(0.6,2.0))
wwav("vdemo_3_SIMPLE_0.45.wav",render(0.45,1.0))
