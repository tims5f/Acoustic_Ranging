from pathlib import Path
import subprocess,ctypes as C
import numpy as np
root=Path(__file__).resolve().parents[1];out=root/'tmp/phone_test';out.mkdir(parents=True,exist_ok=True)
s=(root/'Core/Src/app_measure.c').read_text();a=s.index('static uint8_t phone_lag_consistent');b=s.index('#if defined(__ICCARM__)',a);fn=s[a:b]
code='#include <stdint.h>\n#include <math.h>\n#include <string.h>\n#define MAX_LAG 960\n#define SAMPLES 3072\nstatic struct {int16_t pcm[SAMPLES];}local_event,peer_event;\n'+fn+'\nint match(const int16_t*a,const int16_t*b,int lag){memcpy(local_event.pcm,a,6144);memcpy(peer_event.pcm,b,6144);return phone_lag_consistent(lag);}'
(out/'test.c').write_text(code)
subprocess.run(['D:/mingw64/bin/gcc.exe','-O2','-shared',str(out/'test.c'),'-o',str(out/'test.dll'),'-lm'],check=True)
lib=C.CDLL(str(out/'test.dll'));lib.match.argtypes=[C.POINTER(C.c_int16),C.POINTER(C.c_int16),C.c_int]
rng=np.random.default_rng(10);a=rng.integers(-4000,4000,3072,dtype=np.int16);b=np.r_[np.zeros(17,dtype=np.int16),a[:-17]]
def check(x,y,lag):return lib.match(x.ctypes.data_as(C.POINTER(C.c_int16)),y.ctypes.data_as(C.POINTER(C.c_int16)),lag)
assert check(a,b,17)
assert not check(a,b,23)
bad=b.copy();bad[1048:1432]=a[1024:1408];assert not check(a,bad,17)
assert not check(a,np.zeros(3072,dtype=np.int16),17)
print('PASS consistent delay accepted; wrong lag, changing lag and missing peer rejected')
