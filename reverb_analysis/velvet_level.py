import sys,os,subprocess; sys.path.insert(0,'.')
import numpy as np, reverb_battery as rb
SR=rb.sr
IMP=os.path.abspath("../plate_proto/impulse_2p5.f32")
def align(LR):L,R=LR;m=(L+R)/2;o=rb.onset(m);return L[o:],R[o:]
def render(vlvlv,vlv="1"):
    out=f"/tmp/vl_{vlvlv}_{vlv}.f32"
    e={**os.environ,"RV_T60":"2.45","HFM":"0","EARLY":"0","VLV":vlv,"DARKG":"0.85","DARKHZ":"5000","VLVLV":str(vlvlv)}
    subprocess.run(["../plate_proto/render_proto","plate",IMP,out],env=e,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    return align(rb.load_ours(out))
def cen_at(m,ms,win=512,hop=64):
    o=rb.onset(m);m=m[o:];w=np.hanning(win);f=np.fft.rfftfreq(win,1/SR);s=f>40;c=[];t=[]
    for i in range(0,int(0.6*SR)-win,hop):
        fr=np.abs(np.fft.rfft(m[i:i+win]*w));c.append(np.sum(f[s]*fr[s])/(np.sum(fr[s])+1e-20));t.append(i/SR*1000)
    c=np.array(c);t=np.array(t);return [int(c[min(np.searchsorted(t,x),len(c)-1)]) for x in ms]
def band_db(m,lo,hi,ref1k=None):
    seg=m[:int(2*SR)];n=1<<int(np.ceil(np.log2(len(seg))));P=np.abs(np.fft.rfft(seg,n))**2;f=np.fft.rfftfreq(n,1/SR)
    e=10*np.log10(np.sum(P[(f>=lo)&(f<hi)])+1e-20)
    k=10*np.log10(np.sum(P[(f>=707)&(f<1414)])+1e-20)
    return e-k  # band energy relative to the 1k octave
refL,refR=align(rb.load_ref("ir/vintage-plate-1.5s.wav"));ref=(refL+refR)/2
ref8=band_db(ref,5657,11314); ref_shim=band_db(ref,6000,12000)
print(f"reference: 500ms-plateau cen, 6-12k vs 1k = {cen_at(ref,[300,500])}  shimmer(6-12k) {ref_shim:+.1f} dB")
bL,bR=render(1.4,vlv="0");body=(bL+bR)/2
print(f"body only (VLV off): {cen_at(body,[300,500])}  shimmer {band_db(body,6000,12000):+.1f} dB")
print(f"\n{'VLVLV':>6} | {'cen@300':>7}{'cen@500':>7} | {'shimmer 6-12k vs 1k':>20}")
for v in [1.4,1.0,0.7,0.5,0.35,0.25,0.15]:
    L,R=render(v);m=(L+R)/2;c=cen_at(m,[300,500]);sh=band_db(m,6000,12000)
    print(f"{v:6.2f} | {c[0]:7d}{c[1]:7d} | {sh:+20.1f}")
print("\nTarget: cen@500 ~3864 ; ref shimmer %.1f dB"%ref_shim)
