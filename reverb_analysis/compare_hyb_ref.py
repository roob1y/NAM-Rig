import sys,os,subprocess;sys.path.insert(0,'.')
import numpy as np, matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
import reverb_battery as rb, lateslope as ls
FC=rb.FC; OCT=rb.OCT
def ir_render(cmd,cwd,env):
    out=f"/tmp/ch_{abs(hash(tuple(sorted(env.items()))))%99999}.f32"
    e={**os.environ,"RV_T60":"2.45",**env}
    subprocess.run([cmd,"plate",os.path.abspath("impulse.f32"),out],cwd=cwd,env=e,check=True,
                   stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    return rb.load_ours(out)
ref=rb.load_ref("ir/vintage-plate-1.5s.wav")
hyb=ir_render("../plate_proto/render_proto","../plate_proto",{"HFM":"1","VLV":"1"})
cands=[("real IR (reference)","k",ref),("hybrid","tab:green",hyb)]
def mono(c):L,R=c;return (L+R)/2
fig,ax=plt.subplots(2,4,figsize=(20,9))
for nm,col,c in cands: ax[0,0].plot(FC,rb.per_band_decay(mono(c),8.0,-5,-35),'o-',color=col,label=nm,ms=4)
ax[0,0].set_xscale('log');ax[0,0].set_title("T30(f) decay -5..-35 (s)");ax[0,0].legend(fontsize=9);ax[0,0].grid(alpha=.3)
for nm,col,c in cands: ax[0,1].plot(FC,rb.per_band_decay(mono(c),6.0,0,-10),'o-',color=col,ms=4)
ax[0,1].set_xscale('log');ax[0,1].set_title("EDT(f) early 0..-10 (s) [plateau]");ax[0,1].grid(alpha=.3)
for nm,col,c in cands: s=rb.oct_spectrum(mono(c),3.0);s=s-s[3];ax[0,2].plot(OCT,s,'o-',color=col,ms=4)
ax[0,2].set_xscale('log');ax[0,2].set_title("Tonal balance (oct dB, norm @1k)");ax[0,2].grid(alpha=.3)
for nm,col,c in cands: ax[0,3].plot(OCT,rb.c80_band(mono(c)),'o-',color=col,ms=4)
ax[0,3].set_xscale('log');ax[0,3].set_title("C80(f) clarity (dB)  [ours below = washier]");ax[0,3].grid(alpha=.3)
for nm,col,c in cands: L,R=c;ax[1,0].plot(OCT,rb.midside_band(L,R),'o-',color=col,ms=4)
ax[1,0].set_xscale('log');ax[1,0].set_title("Side/Mid (dB) width/band");ax[1,0].grid(alpha=.3)
for nm,col,c in cands: ax[1,1].plot([q/1000 for q in ls.FREQS],[ls.late_slope(mono(c),q) for q in ls.FREQS],'o-',color=col,ms=5)
ax[1,1].set_title("Late-slope -35..-55 (dB/s) [HF lifetime]");ax[1,1].set_xlabel("kHz");ax[1,1].grid(alpha=.3)
for nm,col,c in cands: nd=rb.ned(mono(c));ax[1,2].plot(np.arange(len(nd))*0.020,nd,color=col)
ax[1,2].set_title("Echo-density buildup (NED)");ax[1,2].set_xlabel("s");ax[1,2].grid(alpha=.3)
names=[n for n,_,_ in cands];md=[rb.modal_depth(mono(c)) for _,_,c in cands];cen=[rb.centroid(mono(c)) for _,_,c in cands]
axb=ax[1,3];x=np.arange(2);axb.bar(x-0.2,md,0.4,label="modal depth",color="tab:purple");axb2=axb.twinx();axb2.bar(x+0.2,cen,0.4,label="centroid",color="tab:orange")
axb.set_xticks(x);axb.set_xticklabels(names,fontsize=8);axb.set_ylabel("modal depth");axb2.set_ylabel("centroid Hz");axb.set_title("Modal depth (lush>6) + centroid")
fig.suptitle("Plate battery @ anchor 2.45s: REAL IR (reference) vs HYBRID",fontsize=14)
plt.tight_layout(rect=[0,0,1,0.97]);plt.savefig("compare_hyb_ref.png",dpi=115);plt.close();print("wrote compare_hyb_ref.png")
# deltas
rm,hm=mono(ref),mono(hyb)
print("\n           band-by-band  (hybrid - reference)")
print("C80(f) OCT:",[round(float(a-b),2) for a,b in zip(rb.c80_band(hm),rb.c80_band(rm))])
print("T30(f) FC :",[round(float(a-b),2) for a,b in zip(rb.per_band_decay(hm,8.0,-5,-35),rb.per_band_decay(rm,8.0,-5,-35))])
print("S/M(f) OCT:",[round(float(a-b),2) for a,b in zip(rb.midside_band(*hyb),rb.midside_band(*ref))])
print(f"modal {rb.modal_depth(hm):.2f} vs {rb.modal_depth(rm):.2f} | centroid {rb.centroid(hm):.0f} vs {rb.centroid(rm):.0f}")
