"""EDR surface comparison: reference vintage plate vs synthesis (single reservoir)
vs multi-band reservoir, at the anchor knob (RV_T60=2.45 = the 1.5 IR).
Run from plate_proto/ :  python3 ../reverb_analysis/edr_compare.py
"""
import os, sys, subprocess
sys.path.insert(0, os.path.join(os.path.dirname(__file__)))
import numpy as np
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
import reverb_battery as rb
import lateslope as ls

TMAX = 3.0
OUT = os.path.join(os.path.dirname(__file__), "edr_compare.png")

def trim(m): return m[rb.onset(m):]

def render(env):
    e = {**os.environ, "RV_T60": "2.45"}; e.update({k: str(v) for k, v in env.items()})
    out = f"/tmp/edr_{abs(hash(str(env)))%99999}.f32"
    subprocess.run(["./render_proto","plate","impulse.f32",out], env=e, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    L, R = rb.load_ours(out); return trim((L + R) / 2)

def main():
    rL, rR = rb.load_ref("../reverb_analysis/ir/vintage-plate-1.5s.wav")
    ref = trim((rL + rR) / 2)
    syn = render({})
    mb  = render({"HFM": "1"})
    sets = [("Reference (vintage plate, 1.5)", ref),
            ("Synthesis - single reservoir", syn),
            ("Multi-band reservoir R1/R2/R3", mb)]
    t, f, E_ref = rb.edr(ref, TMAX)

    fig, ax = plt.subplots(2, 3, figsize=(16, 8.5))
    for j, (nm, m) in enumerate(sets):
        t_, f_, E = rb.edr(m, TMAX)
        im = ax[0, j].pcolormesh(t_, f_/1000, E.T, vmin=-60, vmax=0, cmap="magma", shading="auto")
        ax[0, j].set_ylim(0, 12); ax[0, j].set_title(nm, fontsize=11)
        ax[0, j].set_xlabel("time (s)"); ax[0, j].set_ylabel("kHz")
        ax[0, j].axhspan(6, 12, fc="none", ec="cyan", lw=1.2, ls="--")
        fig.colorbar(im, ax=ax[0, j], label="dB")

    for j, (nm, m) in enumerate([sets[1], sets[2]]):
        _, _, E = rb.edr(m, TMAX); n = min(E.shape[0], E_ref.shape[0])
        D = E[:n] - E_ref[:n]
        im = ax[1, j].pcolormesh(t[:n], f/1000, D.T, vmin=-20, vmax=20, cmap="RdBu_r", shading="auto")
        ax[1, j].set_ylim(0, 12); ax[1, j].set_title(f"({nm}) - reference", fontsize=11)
        ax[1, j].set_xlabel("time (s)"); ax[1, j].set_ylabel("kHz")
        ax[1, j].axhspan(6, 12, fc="none", ec="k", lw=1.0, ls="--")
        fig.colorbar(im, ax=ax[1, j], label="dB diff")
    ax[1,0].text(0.5,-0.32,"blue = ours decays FASTER than ref (energy missing)   |   red = ours rings LONGER",
                 transform=ax[1,0].transAxes, ha="center", fontsize=8, style="italic")

    axg = ax[1, 2]; fq = ls.FREQS; xk=[q/1000 for q in fq]
    axg.plot(xk,[ls.late_slope(ref,q) for q in fq],"o-",color="k",lw=2,label="reference")
    axg.plot(xk,[ls.late_slope(syn,q) for q in fq],"s--",color="tab:orange",label="synthesis (single)")
    axg.plot(xk,[ls.late_slope(mb,q)  for q in fq],"d--",color="tab:blue",label="multi-band")
    axg.set_title("Late-slope gradient (-35..-55 dB)", fontsize=11)
    axg.set_xlabel("kHz"); axg.set_ylabel("dB/s  (higher = rings longer)")
    axg.grid(alpha=0.3); axg.legend(fontsize=8)
    axg.annotate("ref RISES:\ntop rings longest", xy=(11,-9.6), xytext=(6.3,-13.5),
                 fontsize=8, arrowprops=dict(arrowstyle="->",color="gray"))

    fig.suptitle("Plate EDR - anchor (2.45 s @1k): reference vs synthesis vs multi-band", fontsize=13)
    plt.tight_layout(rect=[0,0,1,0.97]); plt.savefig(OUT, dpi=120); plt.close()
    print("wrote", OUT)

if __name__ == "__main__":
    main()
