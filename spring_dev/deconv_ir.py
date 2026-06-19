import numpy as np, struct
def rd(p):
    d=open(p,'rb').read(); i=12;af=1;ch=2;bps=16;off=0;ln=0;sr=48000
    while i+8<=len(d):
        c=d[i:i+4]; sz=struct.unpack('<I',d[i+4:i+8])[0]
        if c==b'fmt ': af,ch,sr,_,_,bps=struct.unpack('<HHIIHH',d[i+8:i+24])
        elif c==b'data': off=i+8;ln=sz
        i+=8+sz+(sz&1)
    a=np.frombuffer(d[off:off+ln],dtype='<f4').copy() if af==3 else np.frombuffer(d[off:off+ln],dtype='<i2').astype(np.float32)/32768
    a=a.reshape(-1,ch); return a,sr
def wwrite(p,L,R,sr):
    x=np.stack([L,R],1).astype('<f4'); data=x.tobytes(); n=len(data)
    h=b'RIFF'+struct.pack('<I',36+n)+b'WAVE'+b'fmt '+struct.pack('<IHHIIHH',16,3,2,sr,sr*8,8,32)+b'data'+struct.pack('<I',n)
    open(p,'wb').write(h+data)
dry,sr=rd('dry.wav'); wet,_=rd('spring_voicing_demos/0_reference_real-studio spring/guitar_wet-only.wav') if False else rd('../spring_voicing_demos/0_reference_real-studio spring/guitar_wet-only.wav')
n=len(dry); 
def deconv(wch,dch,lam):
    W=np.fft.rfft(wch,2*n); D=np.fft.rfft(dch,2*n)
    H=W*np.conj(D)/(np.abs(D)**2+lam*np.mean(np.abs(D)**2))
    h=np.fft.irfft(H)[:n]; return h
# use mono dry as excitation (convolution reverb usually feeds same dry to both IR channels)
drym=0.5*(dry[:,0]+dry[:,1])
for lam in [1e-3,1e-2,1e-1]:
    hl=deconv(wet[:,0],drym,lam); hr=deconv(wet[:,1],drym,lam)
    # trim to onset, take 1.5s
    e=hl**2+hr**2; on=np.argmax(e>0.02*e.max()); 
    seg=slice(on,on+int(1.5*sr))
    hL,hR=hl[seg],hr[seg]
    # quick quality: energy concentration (good IR = compact onset, smooth decay)
    pk=max(np.abs(hL).max(),np.abs(hR).max())
    print(f"lam={lam:.0e}  onset_idx={on}  peak={pk:.3f}  L/Rcorr={np.corrcoef(hL,hR)[0,1]:+.3f}")
# pick lam=1e-2 as default, save
hl=deconv(wet[:,0],drym,1e-2); hr=deconv(wet[:,1],drym,1e-2)
e=hl**2+hr**2; on=np.argmax(e>0.02*e.max()); seg=slice(on,on+int(1.5*sr))
hL,hR=hl[seg].copy(),hr[seg].copy()
m=max(np.abs(hL).max(),np.abs(hR).max()); hL/=m; hR/=m
wwrite('bx20_ir_recovered.wav',hL,hR,sr)
np.save('bx20_ir.npy',np.stack([hL,hR],1))
print("saved bx20_ir_recovered.wav + bx20_ir.npy  len=%.2fs"%(len(hL)/sr))
