import numpy as np, struct, json, engine, loss
def rd(p):
    d=open(p,'rb').read();i=12;af=1;ch=2;sr=48000;off=0;ln=0;bps=16
    while i+8<=len(d):
        c=d[i:i+4];sz=struct.unpack('<I',d[i+4:i+8])[0]
        if c==b'fmt ':af,ch,sr,_,_,bps=struct.unpack('<HHIIHH',d[i+8:i+24])
        elif c==b'data':off=i+8;ln=sz
        i+=8+sz+(sz&1)
    a=np.frombuffer(d[off:off+ln],dtype='<f4').copy() if af==3 else np.frombuffer(d[off:off+ln],dtype='<i2').astype(np.float32)/32768
    a=a.reshape(-1,ch);return a,sr
def ww(p,L,R,sr):
    x=np.stack([L,R],1).astype('<f4');dt=x.tobytes();n=len(dt)
    h=b'RIFF'+struct.pack('<I',36+n)+b'WAVE'+b'fmt '+struct.pack('<IHHIIHH',16,3,2,sr,sr*8,8,32)+b'data'+struct.pack('<I',n)
    open(p,'wb').write(h+dt)
def fftconv(x,h):
    n=len(x)+len(h)-1;N=1<<(n-1).bit_length()
    return np.fft.irfft(np.fft.rfft(x,N)*np.fft.rfft(h,N),N)[:n]
sr=48000
th=json.load(open('best_theta.json')); th['stereo']=0.65
irL,irR=engine.render_ir(th,n_sec=3.5)
dry,_=rd('dry.wav'); dm=0.5*(dry[:,0]+dry[:,1])
wetL=fftconv(dm,irL)[:len(dm)]; wetR=fftconv(dm,irR)[:len(dm)]
# match wet-only RMS to the studio spring reference wet-only for fair A/B
ref,_=rd('bx20_wet.wav'); refrms=np.sqrt(np.mean(ref**2))
w=np.sqrt(np.mean((0.5*(wetL+wetR))**2)); g=refrms/(w+1e-12)
wetL*=g; wetR*=g
ww('eng_wet-only.wav',wetL,wetR,sr)
# 35% wet mix with dry
mixL=0.65*dry[:,0]+0.35*wetL; mixR=0.65*dry[:,1]+0.35*wetR
mx=max(np.abs(mixL).max(),np.abs(mixR).max(),1e-9)
ww('eng_mix_35pct-wet.wav',mixL/mx*0.98,mixR/mx*0.98,sr)
# final fingerprint of the engine IR vs studio spring IR
ir=np.load('bx20_ir.npy')
fe=loss.fingerprint(irL,irR); fb=loss.fingerprint(ir[:,0],ir[:,1])
print('%-9s %8s %8s'%('metric','ENGINE','studio spring'))
for fc in loss.BANDS:
    print('ttp %4d  %7.0f %7.0f ms'%(fc,fe['ttp'][fc],fb['ttp'][fc]))
for fc in loss.BANDS:
    print('rt60%4d  %7.2f %7.2f s'%(fc,fe['rt60'][fc],fb['rt60'][fc]))
print('centroid  %7.0f %7.0f Hz'%(fe['centroid'],fb['centroid']))
print('L/R corr  %7.2f %7.2f'%(fe['corr'],fb['corr']))
