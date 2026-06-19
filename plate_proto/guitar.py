import numpy as np
from wavutil import write_wav
sr=48000; rng=np.random.default_rng(7)
def note(freq,dur,amp=0.5,damp=0.996,bright=0.5):
    N=int(sr*dur); L=int(sr/freq)
    buf=(rng.uniform(-1,1,L)); 
    # pick brightness: lowpass the initial burst
    for i in range(1,L): buf[i]=bright*buf[i]+(1-bright)*buf[i-1]
    y=np.zeros(N); 
    # Karplus-Strong
    b=buf.copy(); idx=0
    for n in range(N):
        y[n]=b[idx]
        nx=(idx+1)%L
        b[idx]=damp*0.5*(b[idx]+b[nx])
        idx=nx
    env=np.exp(-np.linspace(0,dur,N)*0.6)
    return y*env*amp
def freq(midi): return 440.0*2**((midi-69)/12)
# A clean phrase: ascending arpeggio (gaps to hear tail), pause, then a strummed Em chord ringing out
seq=[]
def add_at(t,sig):
    s=int(t*sr); 
    if len(out)<s+len(sig): out.extend([0.0]*(s+len(sig)-len(out)))
    for i,v in enumerate(sig): out[s+i]+=v
out=[]
# arpeggio E3 G3 B3 E4 (midi 52 55 59 64), one every 0.5s, ringing
for i,m in enumerate([52,55,59,64]):
    add_at(0.3+i*0.55, note(freq(m),2.2,amp=0.55,bright=0.45))
# pause then strummed Em chord (E2 B2 E3 G3 B3 E4) lightly spread = exposes dense reverb wash + tail
chord=[40,47,52,55,59,64]
for j,m in enumerate(chord):
    add_at(3.1+j*0.018, note(freq(m),3.8,amp=0.4,bright=0.5))
y=np.array(out)
# total ~7s, leave tail room
y=np.concatenate([y,np.zeros(int(sr*1.0))])
y/=np.max(np.abs(y))*1.15
st=np.stack([y,y],axis=1)  # mono DI -> dual mono
write_wav('dry.wav',st,sr)
st.astype('<f4').tofile('dry.f32')
print("dry guitar:",len(y)/sr,"s  frames",len(y))
