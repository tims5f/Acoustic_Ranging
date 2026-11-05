from pathlib import Path
import ctypes as C
import subprocess
import numpy as np
root=Path(__file__).resolve().parents[1];out=root/'tmp/impulse_test';out.mkdir(parents=True,exist_ok=True)
s=(root/'Core/Src/app_measure.c').read_text();fn=s[s.index('static uint8_t correlate_envelope'):s.index('static uint8_t correlate(int64_t')]
head="#include <stdint.h>\n#include <math.h>\n#include <string.h>\n#define SAMPLES 3072\n#define MAX_LAG 960\n#define APP_AUDIO_TRIGGER_PRE_SAMPLES 1024\nstatic struct {int16_t pcm[SAMPLES];} local_event,peer_event;\nstatic float envelope_a[SAMPLES],envelope_b[SAMPLES],envelope_scores[1921];\nstatic void service_measure_network(int lag){(void)lag;} static int last_envelope_lag; static uint16_t last_envelope_correlation;\n"
wrapper="int match(const int16_t *a,const int16_t *b,int *lag,int *corr) { int ok;memcpy(local_event.pcm,a,6144);memcpy(peer_event.pcm,b,6144);ok=correlate_envelope(lag,0.60f,1);*corr=last_envelope_correlation;return ok;}"
(out/'match.c').write_text(head+fn+wrapper)
subprocess.run(['D:/mingw64/bin/gcc.exe','-shared','-O2',str(out/'match.c'),'-o',str(out/'match.dll'),'-lm'],check=True)
lib=C.CDLL(str(out/'match.dll'));lib.match.argtypes=[C.POINTER(C.c_int16),C.POINTER(C.c_int16),C.POINTER(C.c_int),C.POINTER(C.c_int)]
def run(a,b):
 a=np.asarray(a,dtype=np.int16);b=np.asarray(b,dtype=np.int16);lag=C.c_int();corr=C.c_int();ok=lib.match(a.ctypes.data_as(C.POINTER(C.c_int16)),b.ctypes.data_as(C.POINTER(C.c_int16)),C.byref(lag),C.byref(corr));return ok,lag.value,corr.value
rng=np.random.default_rng(5);a=np.zeros(3072);attack=rng.normal(0,2500,100)*np.hanning(100);a[1040:1140]=attack
b=np.r_[np.zeros(17),a[:-17]]*.7
# Late reverberation has a different delay; the main attack must dominate the match.
a[1800:1900]+=attack*3;b[1950:2050]+=attack*3
ok,lag,corr=run(a,b);assert ok and abs(lag-17)<=1,(ok,lag,corr)
assert not run(np.zeros(3072),np.zeros(3072))[0]
x=np.loadtxt(root/'tools/pair_samples.csv',delimiter=',');x[x>32767]-=65536
print('PASS synthetic 17-sample delay with late echoes; silence rejected; real capture replay:',run(x[:,0],x[:,1]))
