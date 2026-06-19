#!/usr/bin/env python3
"""
flamo_fit.py — differentiable FDN fitter (FLAMO-style), self-contained PyTorch.

Fits our `fdnplate11` reverb engine's parameters to a target impulse response by
gradient descent on the FDN's ACTUAL frequency-domain response. Crucially it makes
the FEEDBACK MATRIX learnable: the modal coupling of the matrix is what the analytic
per-line damping model (and the heuristic capture in capture_v2.py) could not
account for — that was the 500 Hz-1.4 kHz "knee" that wouldn't converge on the steep
6.5:1 decay of the studio 4.5 s plate. A differentiable fit against the real response
sees that coupling and corrects for it.

WHY THIS IS CPU-FRIENDLY: it's an OFFLINE, one-time fit per IR (a small parameter
set, minutes on CPU, no GPU). The output is the same cheap real-time C++ reverb
(fdnplate11 / ReverbBlock) — zero added runtime cost.

------------------------------------------------------------------------------------
RUN (on a machine with Python + PyTorch; CPU is fine):
    pip install torch numpy soundfile
    python flamo_fit.py "ir/NEVO - vintage plate, 4.5s.wav" --rate 16000 --iters 500
Outputs (drop into fdnplate11 via env / files):
    fit_gunit.txt   (16 per-band per-sample damping gains  -> FDN_GUNIT)
    fit_qvec.txt    (16 per-band GEQ Q                      -> FDN_QVEC)
    fit_matrix.txt  (N*N learned orthogonal feedback matrix -> FDN_MATRIX)
    fit_env.txt     (DC60, BRIGHT, LOWCUT, ... engine env vars)
Then render:  FDN_MATRIX=fit_matrix.txt FDN_GUNIT=fit_gunit.txt FDN_QVEC=fit_qvec.txt \
              FDN_DC60=... FDN_BRIGHT=... ./fdnplate11 imp.f32 out.f32
------------------------------------------------------------------------------------
NOTES / KNOBS TO TUNE (this is v1 — expect to adjust):
  * --rate: fit sample rate. Default 16 kHz so the long (~13 s) low decay fits a
    tractable FFT. Bands up to rate/2 are fit; the top octave (>8 k) is handled by
    the engine's air shelf + tone-corrector, not here.
  * --iters / --lr: Adam steps / learning rate.
  * loss weights W_EDR / W_SPEC below.
  * If memory is tight, lower --nfft_pow or --rate.
Report the printed per-band T60 + spectrum error so we can iterate the loss/weights.
"""
import argparse, numpy as np

# ----------------------------------------------------------------------------------
# engine constants (must match fdnplate11.cpp)
PRIMES = [337,389,431,479,523,571,619,661,709,761,811,857,907,953,1009,1061,1103,
          1153,1201,1259,1301,1361,1409,1459,1511,1559,1607,1657,1709,1759,1811,
          1867,1913,1973,2017,2069,2113,2161,2213,2267,2309,2357,2411,2467,2521,
          2579,2621,2677,2729,2777,2833,2887,2939,2999,3041,3089,3137,3187,3251,
          3301,3347,3391,3457,3461]
GEQ_FC = np.array([62.5,125,180,250,355,500,710,1000,1400,2000,2800,4000,5600,8000,
                   11000,16000.])
SIZE = 1.5
SR_ENGINE = 48000.0

def read_wav(p):
    # dependency-light WAV reader (float32 / PCM16/24/32, mono or stereo) -> (samples[N,ch], sr)
    import struct
    with open(p,'rb') as f: d=f.read()
    assert d[:4]==b'RIFF' and d[8:12]==b'WAVE', "not a WAV"
    i=12; fmt=None; data=None
    while i+8<=len(d):
        cid=d[i:i+4]; sz=struct.unpack('<I',d[i+4:i+8])[0]; ch=d[i+8:i+8+sz]
        if cid==b'fmt ': fmt=ch
        elif cid==b'data': data=ch
        i+=8+sz+(sz&1)
    af,nch,sr,_,_,bps=struct.unpack('<HHIIHH',fmt[:16])
    if af==3 and bps==32: a=np.frombuffer(data,dtype='<f4').astype(np.float64)
    elif af==1 and bps==16: a=np.frombuffer(data,dtype='<i2').astype(np.float64)/32768.0
    elif af==1 and bps==24:
        raw=np.frombuffer(data,dtype=np.uint8).reshape(-1,3).astype(np.int32)
        v=(raw[:,0]|(raw[:,1]<<8)|(raw[:,2]<<16)); v=np.where(v&0x800000,v-0x1000000,v); a=v/8388608.0
    elif af==1 and bps==32: a=np.frombuffer(data,dtype='<i4').astype(np.float64)/2147483648.0
    elif af==3 and bps==64: a=np.frombuffer(data,dtype='<f8').astype(np.float64)
    else: raise ValueError(f"unsupported WAV: af={af} bps={bps}")
    return a.reshape(-1,nch), sr

def resample(x, sr_in, sr_out):
    if sr_in == sr_out: return x
    n = int(round(len(x)*sr_out/sr_in))
    # simple FFT resample (mono)
    return np.fft.irfft(np.fft.rfft(x), n) * (n/len(x))

def octave_bands_db(mag2, f, centers):
    out=[]
    for c in centers:
        lo,hi = c/2**(1/6), c*2**(1/6); s=(f>=lo)&(f<hi)
        out.append(10*np.log10(mag2[s].mean()+1e-30) if s.any() else -120.0)
    return np.array(out)

def main():
    import torch
    ap = argparse.ArgumentParser()
    ap.add_argument("ir")
    ap.add_argument("--rate", type=int, default=16000)
    ap.add_argument("--nfft_pow", type=int, default=18)   # 2^18 @16k = 16.4 s
    ap.add_argument("--iters", type=int, default=500)
    ap.add_argument("--lr", type=float, default=0.02)
    ap.add_argument("--chunk", type=int, default=4096)     # freq-bins per solve chunk
    ap.add_argument("--fp64", action="store_true")         # use float64 (slower, for a final accurate pass)
    args = ap.parse_args()
    dev = "cuda" if torch.cuda.is_available() else "cpu"
    print(f"device: {dev}" + (f" ({torch.cuda.get_device_name(0)})" if dev=="cuda" else ""))
    RDTYPE = torch.float64 if args.fp64 else torch.float32
    CDTYPE = CDTYPE if args.fp64 else torch.complex64
    torch.set_default_dtype(RDTYPE)
    print(f"precision: {'float64' if args.fp64 else 'float32 (faster; pass --fp64 for a final accurate pass)'}")

    # ---- target IR ----
    a, sr = read_wav(args.ir); m = a.mean(axis=1)
    on = int(np.argmax(np.abs(m) > 0.01*np.max(np.abs(m)))); m = m[on:]
    m = resample(m, sr, args.rate)
    rate = args.rate
    N = len(PRIMES)
    dl = np.array([max(2, int(round(p*SIZE*rate/SR_ENGINE)))|1 for p in PRIMES])

    # ---- target features ----
    nfft = 1 << args.nfft_pow
    m = m[:nfft] if len(m) >= nfft else np.pad(m, (0, nfft-len(m)))
    f = np.fft.rfftfreq(nfft, 1/rate)
    f[0] = f[1]            # guard DC: the peaking biquad is 0/0 at w=0 -> NaN
    # band centers within the fit band
    centers = GEQ_FC[GEQ_FC < rate*0.46]
    def edr_db(x):                       # per-band energy-decay curves (downsampled)
        # STFT
        win=1024; hop=512; w=np.hanning(win)
        idx=range(0,len(x)-win,hop)
        S=np.stack([np.abs(np.fft.rfft(x[i:i+win]*w))**2 for i in idx])  # [T,Fbin]
        ff=np.fft.rfftfreq(win,1/rate)
        E=np.cumsum(S[::-1],axis=0)[::-1]; E/=(E[0:1]+1e-20)
        db=10*np.log10(E+1e-12)
        # per center band, take nearest bin column
        cols=[np.argmin(np.abs(ff-c)) for c in centers]
        return db[:, cols]               # [T, nbands]
    tgt_edr = torch.tensor(edr_db(m), device=dev)
    tgt_spec = torch.tensor(octave_bands_db(np.abs(np.fft.rfft(m))**2, f, centers), device=dev)
    tgt_spec = tgt_spec - tgt_spec[np.argmin(np.abs(centers-1000))]

    # ---- learnable params ----
    # feedback matrix via Cayley transform of a skew-symmetric (always orthogonal)
    Wsk = torch.zeros(N, N, device=dev, requires_grad=True)
    torch.nn.init.normal_(Wsk, std=1.0)   # well-mixed orthogonal (small std -> A~=I = poor mixing + near-singular solve)
    # decay params reparameterised to O(1) scale so a single Adam lr behaves:
    #   DC60 in seconds (~5), band gains scaled by GB_SCALE (per-sample dB ~ raw*1e-4)
    GB_SCALE = 1e-3   # per-band gain scale: gb_param~O(1) must reach ~ -60/(T60_band*rate)-l0;
                      #   too small (1e-4) -> gb_param needs ~-20 -> never converges -> flat decay
    dc_param = torch.tensor(5.0, device=dev, requires_grad=True)              # broadband T60 (s)
    gb_param = torch.zeros(len(GEQ_FC), device=dev, requires_grad=True)       # O(1) raw band gains
    Qv   = np.where(GEQ_FC <= 1000, 2.2, 0.9)                                 # fixed per-band Q (matches narrow-band fix)

    fch = torch.tensor(f, device=dev)
    dlt = torch.tensor(dl.astype(float), device=dev)
    # precompute peaking-filter dB shapes (unit gain) per band over the freq grid
    def peak_db_unit(f0, Q):
        w = 2*np.pi*f/rate; A_=10**(1.0/40); c=np.cos(w); s=np.sin(w); al=s/(2*Q); a0=1+al/A_
        b0=(1+al*A_)/a0; b1=-2*c/a0; b2=(1-al*A_)/a0; a1=-2*c/a0; a2=(1-al/A_)/a0
        z=np.exp(-1j*w)
        with np.errstate(invalid='ignore', divide='ignore'):
            H=(b0+b1*z+b2*z*z)/(1+a1*z+a2*z*z)
        db=20*np.log10(np.abs(H)+1e-12)            # dB at +1 dB command -> ~ the shape
        return np.nan_to_num(db, nan=0.0, posinf=0.0, neginf=0.0)  # peak filter ~0 dB at the
        #   numerically-singular low bins (catastrophic cancellation); 0 dB is correct there
    shapes = torch.tensor(np.stack([peak_db_unit(GEQ_FC[k], Qv[k]) for k in range(len(GEQ_FC))]),
                          device=dev)              # [nband, Fbins]

    b = torch.ones(N, device=dev) / np.sqrt(N)
    cc = torch.ones(N, device=dev) / np.sqrt(N)

    def cayley(W):
        Wsk2 = W - W.t()
        I = torch.eye(N, device=dev)
        return torch.linalg.solve(I + Wsk2, I - Wsk2)   # orthogonal

    def render_ir():
        A = cayley(Wsk)
        l0 = -60.0/(torch.clamp(dc_param, 0.2, 60.0)*rate)   # broadband per-sample loss (dB)
        gband = gb_param * GB_SCALE                          # per-band extra per-sample loss
        # per-line per-sample loss (dB) at each freq:  l0 + sum_k gband[k]*shape_k(f)
        loss_db = l0 + (gband[:,None]*shapes).sum(0)     # [Fbins], dB per sample
        loss_db = torch.clamp(loss_db, max=-1e-6)        # must attenuate (margin keeps G<1 -> stable solve)
        # per-line magnitude over freq: 10^(loss_db * m_i / 20)  -> [N, Fbins]
        Gmag = 10**(loss_db[None,:]*dlt[:,None]/20.0)
        # transfer function per freq via batched solve over chunks
        Hf = torch.zeros(len(f), dtype=CDTYPE, device=dev)
        z = torch.exp(-1j*2*np.pi*fch/rate)              # [Fbins]
        Ac = A.to(CDTYPE)
        for i in range(0, len(f), args.chunk):
            sl = slice(i, min(i+args.chunk, len(f)))
            zc = z[sl]                                    # [c]
            Dc = (zc[:,None]**dlt[None,:]).to(CDTYPE)   # [c, N]  diag(z^-m) entries
            G  = Gmag[:, sl].t().to(CDTYPE)     # [c, N]
            # M = I - diag(Dc) @ A @ diag(G)   ->  M[c] = I - (Dc[:,:,None]*A)*G[:,None,:]
            M = -(Dc[:,:,None]*Ac[None,:,:])*G[:,None,:]
            M = M + torch.eye(N, dtype=CDTYPE, device=dev)[None]
            rhs = (Dc * b[None,:]).to(CDTYPE)   # [c, N]
            s = torch.linalg.solve(M, rhs.unsqueeze(-1)).squeeze(-1)  # [c, N]
            Hf[sl] = (s * cc[None,:].to(CDTYPE)).sum(-1)
        ir = torch.fft.irfft(Hf, n=nfft)
        return ir

    def features(ir):
        # spectrum
        spec = 10*torch.log10(torch.abs(torch.fft.rfft(ir))**2 + 1e-30)
        # octave bands
        sb=[]
        for c in centers:
            lo,hi=c/2**(1/6),c*2**(1/6); msk=(fch>=lo)&(fch<hi)
            sb.append(10*torch.log10((torch.abs(torch.fft.rfft(ir))[msk]**2).mean()+1e-30))
        sb=torch.stack(sb); sb=sb-sb[int(np.argmin(np.abs(centers-1000)))]
        # EDR per band (matching edr_db layout)
        win=1024; hop=512; w=torch.hann_window(win, device=dev)
        frames=ir.unfold(0,win,hop)*w                    # [T,win]
        S=torch.abs(torch.fft.rfft(frames,dim=1))**2     # [T,Fbin]
        E=torch.flip(torch.cumsum(torch.flip(S,[0]),0),[0]); E=E/(E[0:1]+1e-20)
        db=10*torch.log10(E+1e-12)
        ff=np.fft.rfftfreq(win,1/rate); cols=[np.argmin(np.abs(ff-c)) for c in centers]
        return db[:, cols], sb

    W_EDR, W_SPEC = 1.0, 0.5
    opt = torch.optim.Adam([Wsk, dc_param, gb_param], lr=args.lr)
    Tmin = min(tgt_edr.shape[0], 99999)
    for it in range(args.iters):
        opt.zero_grad()
        ir = render_ir()
        edr, sb = features(ir)
        T = min(edr.shape[0], tgt_edr.shape[0])
        # decay region (-3..-60 dB of the target). NOTE: normalise PER BAND so the
        # long low bands (many frames) don't dominate and starve the fast high bands
        # of gradient -> that was forcing a flat compromise decay across all bands.
        wmask = ((tgt_edr[:T] < -3) & (tgt_edr[:T] > -60)).to(edr.dtype)
        se = ((edr[:T]-tgt_edr[:T])**2)*wmask          # [T, nbands]
        per_band = se.sum(0)/(wmask.sum(0)+1.0)        # each band normalised by its own frames
        edr_loss = per_band.mean()                     # equal weight per band -> tilt is learnable
        spec_loss = ((sb-tgt_spec)**2).mean()
        loss = W_EDR*edr_loss + W_SPEC*spec_loss
        loss.backward(); opt.step()
        if it % 25 == 0 or it == args.iters-1:
            print(f"it{it:4d} loss={loss.item():.3f} edr={edr_loss.item():.3f} spec={spec_loss.item():.3f}")

    # ---- export ----
    with torch.no_grad():
        A = cayley(Wsk).cpu().numpy()
        # per-band per-sample gains: l0 broadband -> DC60; gband -> FDN_GUNIT (per-sample dB)
        DC60 = float(torch.clamp(dc_param, 0.2, 60.0).item())
        # gains are per-sample dB at the FIT rate; the engine runs at 48 kHz, and
        # T60 ~ persample_dB * rate, so rescale by (rate/48000) for the engine.
        gunit = (gb_param*GB_SCALE*(rate/48000.0)).cpu().numpy()   # per-sample dB command per band (engine rate)
    np.savetxt("fit_matrix.txt", A, fmt="%.8f")
    np.savetxt("fit_gunit.txt", gunit, fmt="%.8e")
    np.savetxt("fit_qvec.txt", Qv, fmt="%.3f")
    with open("fit_env.txt","w") as fo:
        fo.write(f"FDN_N={N}\nFDN_SIZE={SIZE}\nFDN_DC60={DC60:.4f}\n")
        fo.write("FDN_BRIGHT=9000\nFDN_AIR=0\nFDN_LOWCUT=25\nFDN_ER_MIX=0.0\n")
        fo.write("# matrix: FDN_MATRIX=fit_matrix.txt  gains: FDN_GUNIT=fit_gunit.txt  Q: FDN_QVEC=fit_qvec.txt\n")
    print("\nWrote fit_matrix.txt, fit_gunit.txt, fit_qvec.txt, fit_env.txt")
    print(f"DC60={DC60:.2f}  (then render with fdnplate11 + the tone-corrector for the integrated spectrum)")

    # ---- post-fit graph check: fitted MODEL impulse vs target IR ----
    with torch.no_grad():
        ir = render_ir().cpu().numpy()
    def _bp(x, fc, Q=2.0):
        w0=2*np.pi*fc/rate; al=np.sin(w0)/(2*Q); c=np.cos(w0)
        b0,b2=al/(1+al),-al/(1+al); a1,a2=(-2*c)/(1+al),(1-al)/(1+al)
        nf=1<<int(np.ceil(np.log2(len(x)+8))); X=np.fft.rfft(x,nf)
        om=2*np.pi*np.fft.rfftfreq(nf); z=np.exp(-1j*om)
        return np.fft.irfft(X*((b0+b2*z*z)/(1+a1*z+a2*z*z)),nf)[:len(x)]
    def _t60(x, fc, lo=-5, hi=-35):
        e=_bp(x,fc)**2; s=np.cumsum(e[::-1])[::-1]; s/=s[0]+1e-20; db=10*np.log10(s+1e-20)
        i1=int(np.argmax(db<=lo)); i2=int(np.argmax(db<=hi))
        return (i2-i1)/rate*(60.0/(lo-hi)) if i2>i1 else float('nan')
    def _oct(x, cf):
        X=np.abs(np.fft.rfft(x*np.hanning(len(x))))**2; ff=np.fft.rfftfreq(len(x),1/rate)
        v=np.array([10*np.log10(X[(ff>=c/2**(1/6))&(ff<c*2**(1/6))].mean()+1e-30) for c in cf])
        return v - v[int(np.argmin(np.abs(cf-1000)))]
    L=min(len(ir),len(m)); irc=ir[:L]; mc=m[:L]
    bands=[b for b in [125,250,500,1000,2000,4000,8000] if b<rate*0.45]
    print("\n  GRAPH CHECK (fitted model vs target IR)")
    print("  per-band T60 (s):  band  target  model   err")
    te=[]
    for b in bands:
        tt,to=_t60(mc,b),_t60(irc,b); te.append(abs(to-tt))
        print(f"                    {b:5d}  {tt:5.2f}  {to:5.2f}  {to-tt:+.2f}")
    print(f"    T60 mean|err| = {np.nanmean(te):.3f}s")
    cf=np.array([b for b in [63,125,250,500,1000,2000,4000,8000] if b<rate*0.45])
    si,so=_oct(mc,cf),_oct(irc,cf)
    print("  spectrum (dB re1k): "+"  ".join("%d:%+.1f"%(c,so[i]-si[i]) for i,c in enumerate(cf)))
    print(f"    spectrum mean|err| = {np.mean(np.abs(so-si)):.2f}dB")

if __name__ == "__main__":
    main()
