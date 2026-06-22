# abx_decomp.py — isolate the 5-12 kHz LATE-FIELD structural residual (branch-2 "EDR red")
# from (a) the separate onset/transient branch and (b) a static top-band tilt that is just
# an EQ/velvet-level offset. Decomposes the audible top-band difference into:
#   raw 5-12k NMR  ->  static tilt (dB, ours-ref)  ->  NMR after removing that tilt (structure)
# If post-EQ NMR < 0 dB, the "red" is a fixable static offset / inaudible structure (stop tuning,
# at most a velvet-level nudge). If still > 0 dB, there is genuine audible top-band STRUCTURE.
import numpy as np, sys; sys.path.insert(0,'.')
import reverb_battery as rb, masking_abx as M
sr=rb.sr; FC=M.FC

def note_onsets(dry, thr_db=-30, guard=int(0.05*sr)):
    env=np.abs(dry); e=20*np.log10(env/ (np.max(env)+1e-12)+1e-12)
    on=[]; i=0
    while i<len(e):
        if e[i]>thr_db and (not on or i-on[-1]>int(0.12*sr)):
            on.append(i); i+=guard
        else: i+=1
    return np.array(on)

def run(ours, ref, label, spl=85, exclude_onset_ms=None):
    oL,oR=rb.load_ours(ours); rL,rR=rb.load_ref(ref); dL,dR=rb.load_ours('dry_guitar.f32')
    yoL,yoR=M.render_conv(dL,dR,oL,oR); yrL,yrR=M.render_conv(dL,dR,rL,rR)
    n=min(len(yoL),len(yrL)); om=(yoL[:n]+yoR[:n])/2; rm=(yrL[:n]+yrR[:n])/2
    g=np.sqrt(np.mean(rm**2)/(np.mean(om**2)+1e-20)); om*=g
    Po=M.band_powers(om); Pr=M.band_powers(rm); nfr=min(len(Po),len(Pr)); Po,Pr=Po[:nfr],Pr[:nfr]
    pk=np.percentile(np.abs(rm),99.9)+1e-12; spl_fs=spl-20*np.log10(pk)
    thr=M.masked_threshold(Pr,spl_fs)[:nfr]
    gate=10*np.log10(Pr+1e-20)>-60

    # optional: keep only frames >exclude_onset_ms after the nearest note onset (isolate LATE field)
    framemask=np.ones(nfr,bool)
    if exclude_onset_ms is not None:
        hop=int(0.010*sr); fr_t=np.arange(nfr)*hop
        ons=note_onsets((dL[:n]+dR[:n])/2)
        keep=np.zeros(nfr,bool)
        for k,t in enumerate(fr_t):
            prev=ons[ons<=t]
            if len(prev)==0: keep[k]=False
            else: keep[k]= (t-prev[-1]) > int(exclude_onset_ms/1000*sr)
        framemask=keep

    b512=(FC>=5000)&(FC<12000)
    def nmr_band(bmask, P_o, fmask):
        err=(np.sqrt(P_o)-np.sqrt(Pr))**2
        gg=gate[:,bmask]&fmask[:,None]
        sel_err=err[:,bmask][gg]; sel_thr=thr[:,bmask][gg]
        if gg.sum()==0: return np.nan,0
        aud=((err[:,bmask]>thr[:,bmask])&gg).sum()/gg.sum()
        return 10*np.log10(sel_err.sum()/(sel_thr.sum()+1e-20)), aud
    # raw top-band
    raw_nmr,raw_aud=nmr_band(b512,Po,framemask)
    # static tilt over 5-12k (mean per-band level diff ours-ref, energy-weighted by ref presence)
    gg=gate&framemask[:,None]
    Lo=10*np.log10(Po+1e-20); Lr=10*np.log10(Pr+1e-20)
    tilt_per_band=[]
    for bi in np.where(b512)[0]:
        sel=gg[:,bi]
        if sel.sum()>0: tilt_per_band.append(np.mean(Lo[sel,bi]-Lr[sel,bi]))
    tilt=np.mean(tilt_per_band)
    # EQ ours to ref per-band (remove static offset) then re-measure structural residual
    Po_eq=Po.copy()
    for bi in np.where(b512)[0]:
        sel=gg[:,bi]
        if sel.sum()>0:
            off=np.mean(Lo[sel,bi]-Lr[sel,bi]); Po_eq[:,bi]=Po[:,bi]*10**(-off/10)
    eq_nmr,eq_aud=nmr_band(b512,Po_eq,framemask)
    # full-band reference for context
    full_nmr,full_aud=nmr_band(np.ones(len(FC),bool),Po,framemask)
    print(f"\n[{label}] SPL{spl}"+(f"  (LATE-only: >{exclude_onset_ms}ms post note-onset)" if exclude_onset_ms else "  (all frames)"))
    print(f"  full-band   NMR {full_nmr:+5.1f} dB   ({100*full_aud:4.1f}% tiles audible)")
    print(f"  5-12k raw   NMR {raw_nmr:+5.1f} dB   ({100*raw_aud:4.1f}% tiles audible)")
    print(f"  5-12k static tilt (ours-ref): {tilt:+.1f} dB")
    print(f"  5-12k AFTER removing static tilt -> STRUCTURAL residual NMR {eq_nmr:+5.1f} dB   ({100*eq_aud:4.1f}% tiles audible)")
    return dict(full=full_nmr,raw512=raw_nmr,tilt=tilt,struct512=eq_nmr)

if __name__=="__main__":
    ours,ref=sys.argv[1],sys.argv[2]
    print("="*78); print("TOP-BAND (5-12k) RESIDUAL AUDIBILITY DECOMPOSITION  —  ours vs vintage-plate ref")
    print("="*78)
    run(ours,ref,"all frames",85,None)
    run(ours,ref,"late field",85,150)
