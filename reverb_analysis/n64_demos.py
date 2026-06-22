import sys,os,subprocess; sys.path.insert(0,'.')
import numpy as np, reverb_battery as rb, wavutil
SR=rb.sr
dry=np.fromfile('dry_guitar.f32',dtype='<f4').reshape(-1,2)
dry.tofile('/tmp/dg.f32')
def run(binn,out,env):
    e={**os.environ,"RV_T60":"2.45",**env}
    subprocess.run([f"../plate_proto/{binn}","plate",os.path.abspath('/tmp/dg.f32'),out],env=e,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    return np.fromfile(out,dtype='<f4').reshape(-1,2)
def mix(wet,wmix=0.35):
    n=min(len(dry),len(wet)); m=(1-wmix)*dry[:n]+wmix*wet[:n]; return m
def norm(m,target=0.2):
    p=np.sqrt(np.mean(m**2)); return m*(target/(p+1e-12))
cfg={
 'N32':       ('render_proto',   {"DARKHZ":"5000","DARKG":"0.85","VLV":"1","VLVLV":"1.4","HFM":"0"}),
 'N64_dark':  ('render_proto64', {"DARKHZ":"5000","DARKG":"0.85","VLV":"1","VLVLV":"1.4","HFM":"0"}),
 'N64_bright':('render_proto64', {"RV_DAMP":"11000","DARKHZ":"7000","DARKG":"0.90","VLV":"1","VLVLV":"1.4","HFM":"0"}),
}
def wwav(path,m):
    m=norm(mix(m)); 
    # write 16-bit stereo wav via wavutil if available else manual
    import struct,wave
    w=wave.open(path,'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(int(SR))
    d=np.clip(m,-1,1); d=(d*32767).astype('<i2'); w.writeframes(d.tobytes()); w.close()
for nm,(b,env) in cfg.items():
    wet=run(b,f"/tmp/wet_{nm}.f32",env); wwav(f"demo_{nm}.wav",wet); print("wrote demo_%s.wav"%nm)
