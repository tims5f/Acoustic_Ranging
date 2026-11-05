from pathlib import Path
import ctypes as C
import subprocess
import numpy as np
root=Path(__file__).resolve().parents[1]
out=root/'tmp/audio_filter_test';out.mkdir(parents=True,exist_ok=True)
subprocess.run(['D:/mingw64/bin/gcc.exe','-shared','-O2','-ICore/Inc','Core/Src/app_audio_filter.c','-o',str(out/'filter.dll')],cwd=root,check=True)
lib=C.CDLL(str(out/'filter.dll'))
class Filter(C.Structure):
    _fields_=[(n,C.c_float) for n in ['hp1','hp2','lp1','lp2','noise']]
lib.AppAudioFilter_Reset.argtypes=[C.POINTER(Filter)]
lib.AppAudioFilter_Process.argtypes=[C.POINTER(Filter),C.c_int16]
lib.AppAudioFilter_Process.restype=C.c_int16

def run(x):
    f=Filter();lib.AppAudioFilter_Reset(C.byref(f))
    y=np.array([lib.AppAudioFilter_Process(C.byref(f),int(v)) for v in x],dtype=float)
    return y,f.noise
rate=48000;t=np.arange(rate)/rate
gains={}
for hz in [50,2000,16000]:
    x=(10000*np.sin(2*np.pi*hz*t)).astype(np.int16)
    y,_=run(x);gains[hz]=np.sqrt(np.mean(y[24000:]**2)/np.mean(x[24000:].astype(float)**2))
assert gains[50]<.05 and gains[2000]>.9 and gains[16000]<.15,gains
x=np.zeros(24000);x[2000:2100]=np.random.default_rng(1).integers(-12000,12000,100)
a,_=run(x);b,_=run(np.r_[np.zeros(73),x[:-73]])
assert np.max(abs(a[:-73]-b[73:]))==0
_,noise=run(np.full(rate*2,10000));assert noise<20,noise
print('PASS gains:',gains,'equal-filter delay preserved: 73 samples; DC background:',noise)

lib.AppAudioFilter_IsImpulse.argtypes=[C.POINTER(C.c_int16),C.c_uint16,C.c_uint16,C.c_uint16]
lib.AppAudioFilter_IsImpulse.restype=C.c_uint8
rng=np.random.default_rng(2)
short=np.zeros(3072,dtype=np.int16);short[1024:1120]=rng.integers(-10000,10000,96,dtype=np.int16)
long=np.zeros(3072,dtype=np.int16);long[1024:]=rng.integers(-10000,10000,2048,dtype=np.int16)
for a,expected in [(short,1),(long,0),(np.zeros(3072,dtype=np.int16),0)]:
    assert lib.AppAudioFilter_IsImpulse(a.ctypes.data_as(C.POINTER(C.c_int16)),3072,1024,int(max(abs(a.astype(int)))))==expected
print('PASS short impulse / sustained burst / silence classification')

lib.AppAudioFilter_PhoneQuality.argtypes=[C.POINTER(C.c_int16),C.c_uint16,C.c_uint16]
lib.AppAudioFilter_PhoneQuality.restype=C.c_uint8
for a,expected in [(short,0),(long,1),(np.zeros(3072,dtype=np.int16),0)]:
    assert lib.AppAudioFilter_PhoneQuality(a.ctypes.data_as(C.POINTER(C.c_int16)),3072,1024)==expected
print('PASS phone sustained burst / transient / silence quality')
