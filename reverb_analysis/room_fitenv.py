# room_fitenv.py - fit room3_v2 to the reference's DECAY ENVELOPE SHAPE (faithful two-slope),
# with spectral/width/onset/EDR as secondary terms. Envelope match -> C80/Ts/EDT/convexity follow.
import numpy as np, subprocess, os, json, sys
sys.path.insert(0,'.'); import room_battery as rb
W='/tmp/room_rebuild'; BIN=W+'/roomv2'; SR=48000
TGT=sys.argv[2] if len(sys.argv)>2 else 'wood'
RL,RR,RM=rb.load_ir(f'{W}/{TGT}.f32'); RB=rb.battery(RM); RS=rb.stereo_metrics(RL,RR); ROC=rb.octave_edt(RM)
FF,EREF=rb.edr(RM)
def envdb(m,sm_ms=16):
    e=m**2; k=int(sm_ms*0.001*SR); e=np.convolve(e,np.ones(k)/k,'same'); d=10*np.log10(e/e.max()+1e-12); return d
PROBES=[20,40,67,100,150,200,280,400,550,750,1000,1300]
def envsample(m):
    d=envdb(m); return np.array([d[int(t*.001*SR)] if int(t*.001*SR)<len(d) else -90 for t in PROBES])
REF_ENV=np.clip(envsample(RM),-45,0)
DEF=dict(SIZE=0.35,DECAY=0.5,DAMP=0.4,BW=0.55,SHELFG=0.6,SHELFHZ=2500,IDIF1=0.75,IDIF2=0.625,DDIF1=0.5,DDIF2=0.6,PRE=2.7,EARLYSEND=0.6,ESLP=6000,
         EXC=3,LATEG=0.3,LATEDECAY=6.0,LATEDAMP=13000,LATEHP=250)
GRID=dict(DECAY=[0.42,0.5,0.58],DAMP=[0.3,0.4,0.55],BW=[0.45,0.6,0.8],SIZE=[0.30,0.35,0.42],
          DDIF1=[0.5,0.6],IDIF1=[0.62,0.75],EARLYSEND=[0.4,0.55,0.7,0.85],ESLP=[4000,6000,9000],SHELFG=[0.4,0.55,0.75,1.0],SHELFHZ=[2500,3500,5000],
          LATEG=[0.0,0.15,0.25,0.4,0.55],LATEDECAY=[3,5,7,10],LATEDAMP=[8000,12000,16000],LATEHP=[120,250,400])
def render(cfg):
    e=dict(os.environ); e.update({k:str(v) for k,v in cfg.items()})
    subprocess.run([BIN,W+'/imp.f32',W+'/ir.f32'],env=e,stderr=subprocess.DEVNULL,check=True)
    return rb.load_ir(W+'/ir.f32',dur=2.2)
def cost(cfg):
    L,R,m=render(cfg); b=rb.battery(m); s=rb.stereo_metrics(L,R)
    env=np.clip(envsample(m),-45,0); emse=np.mean((env-REF_ENV)**2)
    _,EO=rb.edr(m); white,edrm=rb.edr_white(EREF,EO,FF)
    c = (emse/9.0)*1.0 \
      + abs(b['C80']-RB['C80'])/1.0 \
      + abs(b['Ts']-RB['Ts'])/35.0 \
      + abs(b['cen_cap']-RB['cen_cap'])/600 \
      + abs(b['cen1']-RB['cen1'])/1200 \
      + abs(s['corr_full']-RS['corr_full'])/0.12 \
      + abs(b['crest']-RB['crest'])/2.0 \
      + abs(b['echo']-RB['echo'])/15 \
      + abs(b['bloom']-RB['bloom'])/25 \
      + (edrm/6.0)*1.5
    return c, dict(b=b,s=s,emse=emse,white=white,edrm=edrm,env=env)
if __name__=='__main__':
    rounds=int(sys.argv[1]) if len(sys.argv)>1 else 5
    cur=dict(DEF); sj=f'{W}/room_fitenv_{TGT}.json'
    if os.path.exists(sj): cur.update(json.load(open(sj)))
    bestc,info=cost(cur)
    print('TARGET %s: C80 %+.1f Ts %.0f bloom %.0f echo %.0f crest %.1f cen_cap %.0f cen1 %.0f corr %.2f'%(
        TGT.upper(),RB['C80'],RB['Ts'],RB['bloom'],RB['echo'],RB['crest'],RB['cen_cap'],RB['cen1'],RS['corr_full']))
    print('ref env(dB)@'+','.join(map(str,PROBES))+':\n  '+' '.join('%.0f'%v for v in REF_ENV))
    print('start cost %.2f'%bestc)
    for rd in range(rounds):
        for k in GRID:
            for v in GRID[k]:
                t=dict(cur); t[k]=v; c,_=cost(t)
                if c<bestc-1e-6: bestc=c; cur=t
        print('round %d cost %.2f'%(rd+1,bestc))
    bestc,info=cost(cur); b=info['b']; s=info['s']
    print('FINAL cost %.2f  env-MSE %.1f  EDR white %.0f%% mean %.1f'%(bestc,info['emse'],info['white'],info['edrm']))
    print('  C80 %+.1f(t%+.1f) Ts %.0f(t%.0f) bloom %.0f(t%.0f) echo %.0f(t%.0f) crest %.1f(t%.1f)'%(
        b['C80'],RB['C80'],b['Ts'],RB['Ts'],b['bloom'],RB['bloom'],b['echo'],RB['echo'],b['crest'],RB['crest']))
    print('  cen_cap %.0f(t%.0f) cen1 %.0f(t%.0f) corr %.2f(t%.2f)'%(b['cen_cap'],RB['cen_cap'],b['cen1'],RB['cen1'],s['corr_full'],RS['corr_full']))
    print('  our env(dB):  '+' '.join('%.0f'%v for v in info['env']))
    json.dump({k:cur[k] for k in DEF},open(sj,'w'))
    print('ENV:', ' '.join('%s=%s'%(k,cur[k]) for k in DEF))
