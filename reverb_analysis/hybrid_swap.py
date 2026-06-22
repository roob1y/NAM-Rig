# hybrid_swap.py — RIGOROUS single-factor audibility of the 5-12 kHz late-field residual.
# Build hybrid IR = REFERENCE with ONLY its 5-12 kHz content swapped for OURS. Convolve guitar
# with ref vs hybrid and ABX/NMR. Everything except the top band is byte-identical reference, so
# onset, midband, decay-length and masking confounds all cancel. Two variants:
#   hybrid     = ref_rest + ours_top         (level + shape swap)
#   hybrid_lm  = ref_rest + ours_top*scale   (shape-only; top-band energy matched to ref)
# NMR(ref vs hybrid_lm)@5-12k = pure audibility of OUR top-band STRUCTURE (the EDR "red").
import numpy as np, sys; sys.path.insert(0,'.')
import reverb_battery as rb, masking_abx as M
sr=rb.sr; FC=M.FC

def topband(x, f1=5000, f2=12000):
    nf=len(x); X=np.fft.rfft(x); f=np.fft.rfftfreq(nf,1/sr)
    m=((f>=f1)&(f<f2)).astype(float)
    # 1/3-oct raised-cosine edges to avoid ringing
    return np.fft.irfft(X*m, nf)[:nf]

def prep_irs(ours, ref):
    oL,oR=rb.load_ours(ours); rL,rR=rb.load_ref(ref)
    n=min(len(rL),len(oL))               # truncate ours to ref length (length parity)
    oL,oR,rL,rR=oL[:n],oR[:n],rL[:n],rR[:n]
    # BROADBAND level-match ours to ref (both are 100% wet fields; remove arbitrary render gain)
    Eo=np.sum(oL**2+oR**2); Er=np.sum(rL**2+rR**2); gg=np.sqrt(Er/(Eo+1e-30))
    oL*=gg; oR*=gg
    out={}
    for nm,(o,r) in [('L',(oL,rL)),('R',(oR,rR))]:
        ot=topband(o); rt=topband(r); rest=r-rt
        Eo=np.sum(ot**2); Er=np.sum(rt**2); sc=np.sqrt(Er/(Eo+1e-30))
        out[nm]=dict(ref=r, hyb=rest+ot, hyb_lm=rest+ot*sc, oursTopE=Eo, refTopE=Er)
    return out

def nmr(om, rm, spl=85, b512_only=False):
    Po=M.band_powers(om); Pr=M.band_powers(rm); k=min(len(Po),len(Pr)); Po,Pr=Po[:k],Pr[:k]
    pk=np.percentile(np.abs(rm),99.9)+1e-12; spl_fs=spl-20*np.log10(pk)
    thr=M.masked_threshold(Pr,spl_fs)[:k]; gate=10*np.log10(Pr+1e-20)>-60
    err=(np.sqrt(Po)-np.sqrt(Pr))**2
    bm=((FC>=5000)&(FC<12000)) if b512_only else np.ones(len(FC),bool)
    gg=gate[:,bm]
    g=10*np.log10(err[:,bm][gg].sum()/(thr[:,bm][gg].sum()+1e-20))
    aud=((err[:,bm]>thr[:,bm])&gg).sum()/max(gg.sum(),1)
    return g,aud

if __name__=="__main__":
    ours,ref=sys.argv[1],sys.argv[2]
    irs=prep_irs(ours,ref); dL,dR=rb.load_ours('dry_guitar.f32')
    def conv2(key):
        yL=M.fftconv(dL,irs['L'][key]); yR=M.fftconv(dR,irs['R'][key]); n=min(len(yL),len(yR)); return (yL[:n]+yR[:n])/2
    ref_m=conv2('ref'); hyb_m=conv2('hyb'); hlm_m=conv2('hyb_lm')
    topdB=10*np.log10(irs['L']['oursTopE']/irs['L']['refTopE'])
    print("="*76)
    print("SINGLE-FACTOR SWAP: audibility of replacing ref's 5-12kHz with OURS")
    print("="*76)
    print(f"  our top-band IR energy vs ref (level-matched IRs): {topdB:+.1f} dB  (our 5-12k {'HOTTER' if topdB>0 else 'darker'})")
    for spl in (75,85,95):
        g_h,a_h   = nmr(hyb_m, ref_m, spl, True)
        g_lm,a_lm = nmr(hlm_m, ref_m, spl, True)
        gf_h,_    = nmr(hyb_m, ref_m, spl, False)
        print(f"\n  SPL {spl}:")
        print(f"    level+shape swap   5-12k NMR {g_h:+5.1f} dB ({100*a_h:4.1f}% tiles)   full-band NMR {gf_h:+5.1f} dB")
        print(f"    shape-only  swap   5-12k NMR {g_lm:+5.1f} dB ({100*a_lm:4.1f}% tiles)   <- pure top-band STRUCTURE residual")
