import sys,os,subprocess;sys.path.insert(0,'.')
import numpy as np, reverb_battery as rb, synth_v3 as v3
SR=rb.sr
def align(LR):L,R=LR;m=(L+R)/2;o=rb.onset(m);return L[o:],R[o:]
def minphase(sig):
    # min-phase IR with the same magnitude spectrum as sig (real cepstrum; no phase copied)
    n=1<<int(np.ceil(np.log2(len(sig)*2)))
    mag=np.abs(np.fft.fft(sig,n))
    cep=np.fft.ifft(np.log(mag+1e-9)).real
    w=np.zeros(n); w[0]=1; w[1:n//2]=2; w[n//2]=1
    mp=np.real(np.fft.ifft(np.exp(np.fft.fft(cep*w))))
    return mp[:len(sig)]
def build(lead_ms, lead_lv, he):
    rL,rR=align(rb.load_ref("ir/vintage-plate-1.5s.wav"))
    # random-phase dense bloom (the working v3 early field), built into full IR
    hL,hR=v3.build(150,40,8.0,2.0,0.10,10,he)
    # min-phase coherent leading transient from ref first lead_ms, added at the very start
    K=int(lead_ms*0.001*SR)
    mpL=minphase(rL[:K]); mpR=minphase(rR[:K])
    # level: lead_lv relative to the onset rms
    onr=np.sqrt(np.mean(hL[:int(0.02*SR)]**2+hR[:int(0.02*SR)]**2))+1e-12
    mpL*=lead_lv*onr/(np.sqrt(np.mean(mpL**2))+1e-12); mpR*=lead_lv*onr/(np.sqrt(np.mean(mpR**2))+1e-12)
    hL=hL.copy(); hR=hR.copy(); hL[:K]+=mpL; hR[:K]+=mpR
    return hL,hR
if __name__=="__main__":
    import onset_fit as of
    lead=float(sys.argv[1]) if len(sys.argv)>1 else 8.0
    lv=float(sys.argv[2]) if len(sys.argv)>2 else 0.5
    he=float(sys.argv[3]) if len(sys.argv)>3 else 1.0
    hL,hR=build(lead,lv,he); np.stack([hL,hR],1).astype('<f4').tofile("/tmp/gl_ir.f32")
    m=(hL+hR)/2;o=rb.onset(m);mm=m[o:]
    def cen_t0(x):
        fr=np.abs(np.fft.rfft(x[:1024]*np.hanning(1024)));ff=np.fft.rfftfreq(1024,1/SR);s=ff>40
        return np.sum(ff[s]*fr[s])/(np.sum(fr[s])+1e-20)
    crest=np.max(np.abs(mm))/(np.sqrt(np.mean(mm**2))+1e-20)
    err=float(np.mean(np.abs(of.norm(of.tf_map(mm))-of.REFM)))
    print("lead %sms lv %s he %s: TF %.2f cen_t0 %d(ref 7651) crest %.1f(ref 16.6)"%(lead,lv,he,err,cen_t0(mm),crest))
