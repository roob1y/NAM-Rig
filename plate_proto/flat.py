import numpy as np,sys
sr=48000
def coloration(path):
    a=np.fromfile(path,dtype='<f4').reshape(-1,2); m=(a[:,0]+a[:,1])/2
    on=np.argmax(np.abs(m)>0.01*np.max(np.abs(m)))
    tail=m[on+int(0.5*sr):on+int(2.5*sr)]              # steady tail
    w=np.hanning(len(tail)); X=np.abs(np.fft.rfft(tail*w))+1e-12
    f=np.fft.rfftfreq(len(tail),1/sr)
    lg=20*np.log10(X)
    # smooth envelope in ~1/3-oct (geometric) then take fine-structure residual
    def smooth(y,frac=0.06):
        out=np.empty_like(y)
        lf=np.log(f+1e-9)
        for i in range(len(y)):
            lo=lf[i]-frac; hi=lf[i]+frac
            j0=np.searchsorted(lf,lo); j1=np.searchsorted(lf,hi)
            out[i]=y[max(0,j0):max(j0+1,j1)].mean()
        return out
    env=smooth(lg)
    resid=lg-env
    band=(f>=200)&(f<=12000)
    return np.std(resid[band])     # dB std of fine modal structure (lower=more colorless)
print(sys.argv[1], "%.3f dB"%coloration(sys.argv[2]))
