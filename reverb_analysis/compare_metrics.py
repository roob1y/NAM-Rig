import sys,os,subprocess;sys.path.insert(0,'.')
import numpy as np, matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
import reverb_battery as rb, lateslope as ls
FC=rb.FC; OCT=rb.OCT
def ir_ref():
    L,R=rb.load_ref("ir/vintage-plate-1.5s.wav");return L,R
def ir_render(cmd,cwd,env):
    out=f"/tmp/cm_{abs(hash((cmd,tuple(sorted(env.items())))))%99999}.f32"
    e={**os.environ,"RV_T60":"2.45",**env}
    subprocess.run([cmd,"plate","impulse.f32" if cwd=="." else os.path.abspath("impulse.f32"),out],
                   cwd=cwd,env=e,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    return rb.load_ours(out)
cands=[]
cands.append(("reference","k",ir_ref()))
cands.append(("shipped-v3","tab:gray",ir_render("./render_character",".",{})))
cands.append(("multiband","tab:blue",ir_render("../plate_proto/render_proto","../plate_proto",{"HFM":"1"})))
cands.append(("hybrid","tab:green",ir_render("../plate_proto/render_proto","../plate_proto",{"HFM":"1","VLV":"1"})))

fig,ax=plt.subplots(2,4,figsize=(20,9))
def mono(c): L,R=c; return (L+R)/2
# 1 T30(f)
for nm,col,c in cands:
    m=mono(c); ax[0,0].plot(FC,rb.per_band_decay(m,8.0,-5,-35),'o-',color=col,label=nm,ms=3)
ax[0,0].set_xscale('log');ax[0,0].set_title("T30(f)  decay -5..-35 (s)");ax[0,0].set_xlabel("Hz");ax[0,0].legend(fontsize=7);ax[0,0].grid(alpha=.3)
# 2 EDT(f) plateau
for nm,col,c in cands:
    m=mono(c); ax[0,1].plot(FC,rb.per_band_decay(m,6.0,0,-10),'o-',color=col,ms=3)
ax[0,1].set_xscale('log');ax[0,1].set_title("EDT(f)  early 0..-10 (s) [plateau]");ax[0,1].set_xlabel("Hz");ax[0,1].grid(alpha=.3)
# 3 tonal balance (oct spectrum, norm @1k)
for nm,col,c in cands:
    m=mono(c); s=rb.oct_spectrum(m,3.0); s=s-s[3]; ax[0,2].plot(OCT,s,'o-',color=col,ms=3)
ax[0,2].set_xscale('log');ax[0,2].set_title("Tonal balance (oct dB, norm @1k)");ax[0,2].set_xlabel("Hz");ax[0,2].grid(alpha=.3)
# 4 C80(f)
for nm,col,c in cands:
    m=mono(c); ax[0,3].plot(OCT,rb.c80_band(m),'o-',color=col,ms=3)
ax[0,3].set_xscale('log');ax[0,3].set_title("C80(f) clarity (dB)");ax[0,3].set_xlabel("Hz");ax[0,3].grid(alpha=.3)
# 5 mid/side width
for nm,col,c in cands:
    L,R=c; ax[1,0].plot(OCT,rb.midside_band(L,R),'o-',color=col,ms=3)
ax[1,0].set_xscale('log');ax[1,0].set_title("Side/Mid (dB) width/band");ax[1,0].set_xlabel("Hz");ax[1,0].grid(alpha=.3)
# 6 late-slope gradient
for nm,col,c in cands:
    m=mono(c); ax[1,1].plot([q/1000 for q in ls.FREQS],[ls.late_slope(m,q) for q in ls.FREQS],'o-',color=col,ms=4)
ax[1,1].set_title("Late-slope -35..-55 (dB/s) [HF lifetime]");ax[1,1].set_xlabel("kHz");ax[1,1].grid(alpha=.3)
# 7 NED echo density
for nm,col,c in cands:
    m=mono(c); nd=rb.ned(m); ax[1,2].plot(np.arange(len(nd))*0.020,nd,color=col)
ax[1,2].set_title("Echo-density buildup (NED)");ax[1,2].set_xlabel("s");ax[1,2].grid(alpha=.3)
# 8 modal depth + centroid (bars)
names=[nm for nm,_,_ in cands]; md=[rb.modal_depth(mono(c)) for _,_,c in cands]; cen=[rb.centroid(mono(c)) for _,_,c in cands]
axb=ax[1,3]; x=np.arange(len(names))
axb.bar(x-0.2,md,0.4,label="modal depth",color="tab:purple")
axb2=axb.twinx(); axb2.bar(x+0.2,cen,0.4,label="centroid",color="tab:orange")
axb.set_xticks(x);axb.set_xticklabels(names,rotation=20,fontsize=7);axb.set_ylabel("modal depth");axb2.set_ylabel("centroid Hz")
axb.set_title("Modal depth (lush>6) + centroid")
fig.suptitle("Plate metric battery @ anchor 2.45s: reference vs shipped-v3 vs multiband vs hybrid",fontsize=14)
plt.tight_layout(rect=[0,0,1,0.97]);plt.savefig("compare_metrics.png",dpi=110);plt.close();print("wrote compare_metrics.png")
# also print numeric table
print("\nmetric            "+" ".join(f"{n:>11s}" for n,_,_ in cands))
def rowf(lbl,vals,f="%.2f"): print(f"{lbl:18s}"+" ".join(f"{f%v:>11s}" for v in vals))
rowf("centroid Hz",cen,"%.0f"); rowf("modal depth",md)
rowf("C80 1k",[rb.c80_band(mono(c))[3] for _,_,c in cands])
rowf("C80 250",[rb.c80_band(mono(c))[1] for _,_,c in cands])
