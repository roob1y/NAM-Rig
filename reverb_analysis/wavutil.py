import struct,numpy as np
def read_wav(p):
    with open(p,'rb') as f: d=f.read()
    assert d[:4]==b'RIFF' and d[8:12]==b'WAVE'
    i=12; fmt=None; data=None
    while i<len(d):
        cid=d[i:i+4]; sz=struct.unpack('<I',d[i+4:i+8])[0]; ch=d[i+8:i+8+sz]
        if cid==b'fmt ': fmt=ch
        elif cid==b'data': data=ch
        i+=8+sz+(sz&1)
    af,nch,sr,_,_,bps=struct.unpack('<HHIIHH',fmt[:16])
    if af==3 and bps==32: a=np.frombuffer(data,dtype='<f4').astype(np.float64)
    elif af==1 and bps==16: a=np.frombuffer(data,dtype='<i2').astype(np.float64)/32768.0
    elif af==1 and bps==24:
        raw=np.frombuffer(data,dtype=np.uint8).reshape(-1,3).astype(np.int32)
        v=(raw[:,0]|(raw[:,1]<<8)|(raw[:,2]<<16)); v=np.where(v&0x800000,v-0x1000000,v); a=v/8388608.0
    elif af==1 and bps==32: a=np.frombuffer(data,dtype='<i4').astype(np.float64)/2147483648.0
    elif af==3 and bps==64: a=np.frombuffer(data,dtype='<f8').astype(np.float64)
    else: raise ValueError(f"af={af} bps={bps}")
    a=a.reshape(-1,nch)
    return a,sr
def write_wav(p,a,sr):
    a=np.atleast_2d(a)
    if a.shape[0]<a.shape[1] and a.shape[0] in (1,2): a=a.T
    a=np.clip(a,-1,1).astype('<f4'); nch=a.shape[1]
    data=a.tobytes(); 
    fmt=struct.pack('<HHIIHH',3,nch,sr,sr*nch*4,nch*4,32)
    fact=struct.pack('<I',a.shape[0])
    chunks=b'fmt '+struct.pack('<I',len(fmt))+fmt+b'fact'+struct.pack('<I',4)+fact+b'data'+struct.pack('<I',len(data))+data
    with open(p,'wb') as f: f.write(b'RIFF'+struct.pack('<I',4+len(chunks))+b'WAVE'+chunks)
