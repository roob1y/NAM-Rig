#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
using std::vector;
static bool wavRead(const char*p,vector<float>&L,vector<float>&R,int&sr){
    FILE*f=fopen(p,"rb"); if(!f)return false; fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    vector<uint8_t>d(n); if(fread(d.data(),1,n,f)!=(size_t)n){fclose(f);return false;} fclose(f);
    if(memcmp(d.data(),"RIFF",4)||memcmp(d.data()+8,"WAVE",4))return false;
    size_t i=12;int af=1,ch=2,bps=16;size_t off=0,len=0;sr=48000;
    while(i+8<=(size_t)n){char id[5]={0};memcpy(id,&d[i],4);uint32_t sz;memcpy(&sz,&d[i+4],4);
      if(!memcmp(id,"fmt ",4)){memcpy(&af,&d[i+8],2);memcpy(&ch,&d[i+10],2);memcpy(&sr,&d[i+12],4);memcpy(&bps,&d[i+22],2);}
      else if(!memcmp(id,"data",4)){off=i+8;len=sz;} i+=8+sz+(sz&1);}
    int by=bps/8;size_t fr=len/(ch*by);L.resize(fr);R.resize(fr);const uint8_t*pp=&d[off];
    auto rd=[&](size_t k)->float{const uint8_t*s=pp+k*by;if(af==3){float v;memcpy(&v,s,4);return v;}
      if(bps==16){int16_t v;memcpy(&v,s,2);return v/32768.f;}int32_t v;memcpy(&v,s,4);return v/2147483648.f;};
    for(size_t k=0;k<fr;++k){L[k]=rd(k*ch);R[k]=ch>1?rd(k*ch+1):L[k];}return true;}
static void wavWrite(const char*p,const vector<float>&L,const vector<float>&R,int sr){
    uint32_t n=L.size();uint16_t ch=2,bps=32,af=3;uint32_t br=sr*ch*4,dl=n*ch*4,rf=36+dl;uint16_t ba=ch*4;uint32_t fl=16;
    FILE*f=fopen(p,"wb");fwrite("RIFF",1,4,f);fwrite(&rf,4,1,f);fwrite("WAVE",1,4,f);fwrite("fmt ",1,4,f);fwrite(&fl,4,1,f);
    fwrite(&af,2,1,f);fwrite(&ch,2,1,f);fwrite(&sr,4,1,f);fwrite(&br,4,1,f);fwrite(&ba,2,1,f);fwrite(&bps,2,1,f);
    fwrite("data",1,4,f);fwrite(&dl,4,1,f);for(uint32_t i=0;i<n;++i){fwrite(&L[i],4,1,f);fwrite(&R[i],4,1,f);}fclose(f);}
