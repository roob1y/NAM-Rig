import sys; sys.path.insert(0,'.')
import numpy as np, reverb_battery as rb, onset_fit as of, add_cloud as ac
SR=rb.sr
N=int(0.15*SR)
rL,rR=ac.alLR(rb.load_ref('ir/vintage-plate-1.5s.wav'))
sL,sR=ac.alLR(rb.load_ours('/tmp/plate_anchor.f32'))
ref_m=(rL+rR)/2
R=ac.metrics(ref_m,ref_m); tgt=np.array([R['cen_t0'],R['c5'],R['c50'],R['c150']]); rcrest=R['crest']
ship=ac.metrics((sL+sR)/2,ref_m); ship_tf=ship['tferr']
print(f"target arc {tgt.astype(int)}  ref_crest {rcrest:.1f}  shipped: cen_t0 {ship['cen_t0']:.0f} crest {ship['crest']:.1f} TFerr {ship_tf:.2f}\n")
rows=[]
for gain in (1.0,1.5,2.0,2.5):
  for floor in (2500,6000):
    for air_w in (1.0,1.5,2.0):
      bL,bR,_,_=ac.build_cloud(rL,rR,sL,sR,N,gain=gain,floor_hz=floor,air_w=air_w,air_hz=6000)
      m=ac.metrics((bL+bR)/2,ref_m)
      arc=np.array([m['cen_t0'],m['c5'],m['c50'],m['c150']])
      cen_err=np.mean(np.abs(arc-tgt))
      crest_pen=max(0,m['crest']-(rcrest+1.0))
      ok = m['crest']<=rcrest+1.2 and m['tferr']<=ship_tf+0.1
      rows.append((cen_err,m['crest'],m['tferr'],gain,floor,air_w,m['cen_t0'],m['c50'],ok))
rows.sort(key=lambda x:x[0])
print(f"{'cenErr':>7}{'crest':>6}{'TFerr':>6}{'gain':>6}{'floor':>7}{'air_w':>6}{'cen_t0':>8}{'c@50':>7}  ok")
for r in rows[:12]:
    print(f"{r[0]:>7.0f}{r[1]:>6.1f}{r[2]:>6.2f}{r[3]:>6.1f}{r[4]:>7.0f}{r[5]:>6.1f}{r[6]:>8.0f}{r[7]:>7.0f}  {'Y' if r[8] else ' '}")
