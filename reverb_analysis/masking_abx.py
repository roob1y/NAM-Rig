# masking_abx.py — masking-weighted / NMR ABX-style audibility of OUR plate vs the
# vintage-plate reference, on convolved guitar, loudness-matched. Numpy-only.
#
# Question (null-test branch): how much of the remaining 5-12 kHz EDR residual is
# ACTUALLY AUDIBLE? EDR red = normalized decay-shape; this measures perceptual
# audibility of the literal difference in musical context, masked by the program.
#
# Model: convolve dry guitar w/ each stereo IR -> loudness-match (K-ish RMS) ->
# 1/3-oct-ish ERB framing -> per-tile masking threshold from the REFERENCE program
# (Bark spreading + noise-masker offset + ATH, calibrated to a monitoring SPL) ->
# NMR = error_power / masked_threshold. Reported globally, per-band, and band-limited
# to 5-12 kHz, with a 75/85/95 dB SPL listening-level sweep.
import numpy as np, sys
sys.path.insert(0,'.')
import reverb_battery as rb
sr = rb.sr

def fftconv(x, h):
    n = len(x)+len(h)-1; nf = 1<<int(np.ceil(np.log2(n)))
    return np.fft.irfft(np.fft.rfft(x,nf)*np.fft.rfft(h,nf), nf)[:n]

def render_conv(dry_L, dry_R, irL, irR):
    return fftconv(dry_L, irL), fftconv(dry_R, irR)

# ---- auditory helpers ----
def hz2bark(f): return 13*np.arctan(0.00076*f) + 3.5*np.arctan((f/7500.0)**2)
def ath_db(f):  # absolute threshold of hearing, dB SPL
    fk=np.maximum(f,20)/1000.0
    return 3.64*fk**-0.8 - 6.5*np.exp(-0.6*(fk-3.3)**2) + 1e-3*fk**4

# 1/3-octave band centers 50..16k
def third_oct_centers(f1=50, f2=16000):
    cs=[]; c=1000.0
    while c>f1: c/=2**(1/3)
    c*=2**(1/3)
    while c<=f2:
        cs.append(c); c*=2**(1/3)
    return np.array(cs)

FC = third_oct_centers()
ZC = hz2bark(FC)
ATH = ath_db(FC)

def spreading_matrix(zc):
    # Schroeder spreading function in Bark; SM[i,j] = masking on band i from masker band j
    dz = zc[:,None]-zc[None,:]
    sf = 15.81 + 7.5*(dz+0.474) - 17.5*np.sqrt(1+(dz+0.474)**2)  # dB
    return sf
SF = spreading_matrix(ZC)

def band_powers(x, frame=int(0.020*sr), hop=int(0.010*sr)):
    # returns (nframes, nbands) linear power per 1/3-oct band
    w=np.hanning(frame); nf=1<<int(np.ceil(np.log2(frame)))
    f=np.fft.rfftfreq(nf,1/sr)
    # band edges
    edges=FC/2**(1/6); edges=np.append(edges, FC[-1]*2**(1/6))
    masks=[(f>=edges[i])&(f<edges[i+1]) for i in range(len(FC))]
    out=[]
    for i in range(0, len(x)-frame, hop):
        X=np.abs(np.fft.rfft(x[i:i+frame]*w, nf))**2
        out.append([X[m].sum() for m in masks])
    return np.array(out)

def masked_threshold(P_masker, spl_fullscale):
    # P_masker: (nframes,nbands) linear power, calibrated so 0 dBFS = spl_fullscale dB SPL.
    # returns threshold power per tile (linear, in the same calibrated SPL power domain)
    eps=1e-20
    Lspl = 10*np.log10(P_masker+eps) + spl_fullscale          # band level in dB SPL
    # spread excitation across bands (work in dB via max-of-sum approx -> use power sum of spread)
    # convert each masker band to a spread contribution, sum in power
    nb=len(FC); thr_db=np.full_like(Lspl, -np.inf)
    # noise-masker offset (conservative: noise-like masker => low offset => LESS masking => MORE audible)
    offset = 5.5 + 0.5*ZC  # mild Bark-dependent term
    for j in range(nb):
        contrib = Lspl[:,j][:,None] + SF[None,:,j] - offset[None,:]   # (nframes, nbands)
        thr_db = np.maximum(thr_db, contrib)   # use max (dominant masker) — conservative
    # combine with ATH
    thr_db = np.maximum(thr_db, ATH[None,:])
    # temporal forward masking: post-mask decay ~ -? apply per-band exponential floor (10ms hop)
    a=0.55  # per-10ms-hop forward-mask retention
    fm=thr_db.copy()
    for t in range(1,fm.shape[0]):
        fm[t]=np.maximum(fm[t], fm[t-1]+10*np.log10(a))
    thr_db=np.maximum(thr_db, fm)
    return 10**((thr_db-spl_fullscale)/10)   # back to calibrated linear power (dBFS power domain)

def analyze(label, ours_irL, ours_irR, ref_irL, ref_irR, dryL, dryR, spls=(75,85,95)):
    yoL,yoR = render_conv(dryL,dryR, ours_irL, ours_irR)
    yrL,yrR = render_conv(dryL,dryR, ref_irL, ref_irR)
    n=min(len(yoL),len(yrL)); yoL,yoR,yrL,yrR=yoL[:n],yoR[:n],yrL[:n],yrR[:n]
    # loudness-match ours to ref by broadband RMS (mono sum)
    rm=(yrL+yrR)/2; om=(yoL+yoR)/2
    g=np.sqrt(np.mean(rm**2)/ (np.mean(om**2)+1e-20)); yoL*=g; yoR*=g; om*=g
    # work in mono for the threshold/error (sufficient for top-band energy audibility)
    Po=band_powers(om); Pr=band_powers(rm)
    nfr=min(len(Po),len(Pr)); Po,Pr=Po[:nfr],Pr[:nfr]
    err=(np.sqrt(Po)-np.sqrt(Pr))**2     # band-power of the difference (approx, magnitude domain)
    # calibration: set 0 dBFS so that the 99.9th-pct mono sample of ref = chosen peak SPL
    pk=np.percentile(np.abs(rm),99.9)+1e-12
    band5_12=(FC>=5000)&(FC<12000)
    res={}
    for spl in spls:
        spl_fs = spl - 20*np.log10(pk)     # dBFS->SPL offset so ref peak hits `spl`
        thr=masked_threshold(Pr, spl_fs)[:nfr]
        nmr = 10*np.log10((err+1e-20)/(thr+1e-20))    # per-tile NMR dB
        # only tiles with meaningful program energy (-60 dBFS gate)
        gate = 10*np.log10(Pr+1e-20) > -60
        glob_nmr = 10*np.log10( np.sum(err[gate]) / (np.sum(thr[gate])+1e-20) )
        aud = (nmr>0)&gate
        frac_aud = aud.sum()/max(gate.sum(),1)
        # band-limited 5-12k
        g512 = gate[:,band5_12]
        nmr512 = 10*np.log10( np.sum(err[:,band5_12][g512]) / (np.sum(thr[:,band5_12][g512])+1e-20) )
        aud512 = ((nmr>0)&gate)[:,band5_12].sum()/max(g512.sum(),1)
        res[spl]=dict(glob_nmr=glob_nmr, frac_aud=frac_aud, nmr512=nmr512, aud512=aud512,
                      peak_nmr=np.percentile(nmr[gate],99.5))
    return res, (FC, ZC), (Po,Pr,err)

if __name__=="__main__":
    import argparse
    ap=argparse.ArgumentParser(); ap.add_argument('--ours',required=True); ap.add_argument('--ref',required=True)
    ap.add_argument('--label',default='anchor'); a=ap.parse_args()
    oL,oR=rb.load_ours(a.ours); rL,rR=rb.load_ref(a.ref)
    dL,dR=rb.load_ours('dry_guitar.f32')
    res,_,_=analyze(a.label,oL,oR,rL,rR,dL,dR)
    print(f"\n===== MASKING / NMR AUDIBILITY  ours vs reference  [{a.label}] =====")
    print("Interpretation: NMR < 0 dB = difference below masked threshold (inaudible/transparent).")
    print(f"{'SPL':>5} {'globalNMR':>10} {'%audible':>9} {'5-12kNMR':>9} {'%aud5-12k':>10} {'peakNMR':>8}")
    for spl,d in res.items():
        print(f"{spl:>5} {d['glob_nmr']:>10.1f} {100*d['frac_aud']:>8.1f}% {d['nmr512']:>9.1f} {100*d['aud512']:>9.1f}% {d['peak_nmr']:>8.1f}")
