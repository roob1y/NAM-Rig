"""EDR: reference vs multi-band vs multi-band+velvet HYBRID (anchor 2.45)."""
import os, sys, subprocess
sys.path.insert(0, os.path.dirname(__file__))
import numpy as np, matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
import reverb_battery as rb, lateslope as ls
TMAX=3.0; OUT=os.path.join(os.path.dirname(__file__),"edr_hybrid.png")
def trim(m): return m[rb.onset(m):]
def main():
    rL,rR=rb.load_ref("../reverb_analysis/ir/vintage-plate-1.5s.wav"); ref=trim((rL+rR)/2)
    e={**os.environ,"RV_T60":"2.45","HFM":"1"}
    subprocess.run(["./render_proto","plate","impulse.f32","/tmp/h_mb.f32"],env=e,check=True,
                   stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    mL,mR=rb.load_ours("/tmp/h_mb.f32"); mb=trim((mL+mR)/2)
    hL,hR=rb.load_ours("/tmp/vp_hybrid.f32"); hy=trim((hL+hR)/2)
    sets=[("Reference (vintage plate, 1.5)",ref),("Multi-band only",mb),("Multi-band + velvet HYBRID",hy)]
    t,f,Eref=rb.edr(ref,TMAX)
    fig,ax=plt.subplots(2,3,figsize=(16,8.5))
    for j,(nm,m) in enumerate(sets):
        t_,f_,E=rb.edr(m,TMAX)
        im=ax[0,j].pcolormesh(t_,f_/1000,E.T,vmin=-60,vmax=0,cmap="magma",shading="auto")
        ax[0,j].set_ylim(0,12);ax[0,j].set_title(nm,fontsize=11)
        ax[0,j].set_xlabel("time (s)");ax[0,j].set_ylabel("kHz")
        ax[0,j].axhspan(6,12,fc="none",ec="cyan",lw=1.2,ls="--");fig.colorbar(im,ax=ax[0,j],label="dB")
    for j,(nm,m) in enumerate([sets[1],sets[2]]):
        _,_,E=rb.edr(m,TMAX);n=min(E.shape[0],Eref.shape[0]);D=E[:n]-Eref[:n]
        im=ax[1,j].pcolormesh(t[:n],f/1000,D.T,vmin=-20,vmax=20,cmap="RdBu_r",shading="auto")
        ax[1,j].set_ylim(0,12);ax[1,j].set_title(f"({nm}) - reference",fontsize=11)
        ax[1,j].set_xlabel("time (s)");ax[1,j].set_ylabel("kHz")
        ax[1,j].axhspan(6,12,fc="none",ec="k",lw=1.0,ls="--");fig.colorbar(im,ax=ax[1,j],label="dB diff")
    ax[1,0].text(0.5,-0.32,"blue = energy MISSING vs ref   |   note the top-octave blue band CLOSING in the hybrid",
                 transform=ax[1,0].transAxes,ha="center",fontsize=8,style="italic")
    axg=ax[1,2];fq=ls.FREQS;xk=[q/1000 for q in fq]
    axg.plot(xk,[ls.late_slope(ref,q) for q in fq],"o-",color="k",lw=2,label="reference")
    axg.plot(xk,[ls.late_slope(mb,q) for q in fq],"d--",color="tab:blue",label="multi-band only")
    axg.plot(xk,[ls.late_slope(hy,q) for q in fq],"^-",color="tab:green",lw=2,label="hybrid (+velvet)")
    axg.set_title("Late-slope gradient (-35..-55 dB)",fontsize=11)
    axg.set_xlabel("kHz");axg.set_ylabel("dB/s (higher = rings longer)")
    axg.grid(alpha=0.3);axg.legend(fontsize=8)
    axg.annotate("hybrid now RISES\nwith the reference",xy=(11,-13),xytext=(6.2,-19),
                 fontsize=8,arrowprops=dict(arrowstyle="->",color="gray"))
    fig.suptitle("Plate EDR - anchor (2.45 s @1k): the velvet field closes the top-octave gap",fontsize=13)
    plt.tight_layout(rect=[0,0,1,0.97]);plt.savefig(OUT,dpi=120);plt.close();print("wrote",OUT)
if __name__=="__main__": main()
