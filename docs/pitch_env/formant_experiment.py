import numpy as np
SR=48000.0; N=2048
freqs=np.fft.rfftfreq(N,1/SR)
def synth(f0,forms):
    t=np.arange(N)/SR;x=np.zeros(N)
    rng=np.random.default_rng(1)
    for h in range(1,120):
        fh=f0*h
        if fh>=SR/2:break
        g=0.03
        for (fc,bw,a) in forms:g+=a/np.sqrt(1+((fh-fc)/bw)**2)
        x+=g*np.sin(2*np.pi*fh*t+rng.random()*6.28)
    return x*(0.5-0.5*np.cos(2*np.pi*np.arange(N)/N))
def cep_env(x, lifter):
    # textbook real-cepstrum envelope on the FULL complex spectrum
    X=np.fft.fft(x); logmag=np.log(np.abs(X)+1e-9)
    c=np.fft.ifft(logmag).real
    c2=c.copy(); c2[lifter:-lifter+1]=0.0        # keep low quefrency both ends
    env=np.exp(np.fft.fft(c2).real)              # full-length envelope
    return env[:N//2+1]
def mag_full(x): return np.abs(np.fft.fft(x))
def peak(v,lo,hi):
    m=(freqs>=lo)&(freqs<=hi);return freqs[m][np.argmax(v[m])]
forms=[(700,120,1.0),(1800,220,0.55),(2700,320,0.22)]
for f0 in [100,150,220]:
    x=synth(f0,forms)
    for lifter in [30,50,80]:
        e=cep_env(x,lifter)
        print(f"f0={f0} lifter={lifter}: F1~{peak(e,300,1200):.0f} (true 700), F2~{peak(e,1300,2300):.0f} (true 1800)")
    print()
# validate formant-preserving shift with the working envelope
def shift(a,r):
    o=np.zeros_like(a)
    for k in range(len(a)):
        idx=int(k*r)
        if 0<=idx<len(a):o[idx]+=a[k]
    return o
x=synth(150.0,forms); Xh=np.abs(np.fft.fft(x))[:N//2+1]
env=cep_env(x,50); white=Xh/np.maximum(env,1e-9)
up_naive=shift(Xh,2.0); up_form=shift(white,2.0)*env
def band(v,lo,hi):
    m=(freqs>=lo)&(freqs<=hi);return np.sum(v[m]**2)
print(f"original F1={peak(cep_env(np.fft.irfft(Xh*0+1)*0+x,50),300,1200):.0f}")
print(f"naive  up-oct F1={peak(cep_env_from_mag(up_naive) if False else _env(up_naive),300,3000):.0f}" if False else "")
def envmag(mag,lifter=50):
    logmag=np.log(np.concatenate([mag,mag[-2:0:-1]])+1e-9)
    c=np.fft.ifft(logmag).real;c[lifter:-lifter+1]=0
    return np.exp(np.fft.fft(c).real)[:len(mag)]
print(f"original     F1={peak(envmag(Xh),300,1200):.0f} Hz")
print(f"naive up-oct F1={peak(envmag(up_naive),300,3200):.0f} Hz (chipmunk if ~1400)")
print(f"formant up-8 F1={peak(envmag(up_form),300,1400):.0f} Hz (good if ~700)")
print(f"body 400-1000 vs orig: naive {band(up_naive,400,1000)/band(Xh,400,1000):.2f}x  formant {band(up_form,400,1000)/band(Xh,400,1000):.2f}x")
