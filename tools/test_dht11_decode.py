"""Check the production decoder against manufacturer examples and bad frames."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
out=root/'tmp/temperature_test'
out.mkdir(parents=True,exist_ok=True)
s=(root/'Core/Src/app_dht11.c').read_text()
fn=s[s.index('uint8_t AppDht11_Decode'):s.index('static void finish')]
test=r"""
#include <assert.h>
#include <stdio.h>
int main(void) {
  int16_t t; uint16_t h;
  uint8_t good[5]={53,0,24,4,81};
  uint8_t bad[5]={53,0,24,4,73};
  uint8_t negative[5]={53,0,10,129,192};
  uint8_t range[5]={101,0,24,4,129};
  uint8_t decimal[5]={53,0,24,10,87};
  assert(AppDht11_Decode(good,&t,&h) && t==244 && h==530);
  assert(!AppDht11_Decode(bad,&t,&h));
  assert(AppDht11_Decode(negative,&t,&h) && t==-101);
  assert(!AppDht11_Decode(range,&t,&h));
  assert(!AppDht11_Decode(decimal,&t,&h));
  puts("PASS: DHT11 manufacturer frame, checksum rejection, negative temperature, invalid humidity and decimal");
  return 0;
}
"""
(out/'decode.c').write_text('#include <stdint.h>\n'+fn+test)
subprocess.run(['D:/mingw64/bin/gcc.exe','-std=c99','-Wall','-Wextra','-Werror',str(out/'decode.c'),'-o',str(out/'decode.exe')],check=True)
subprocess.run([str(out/'decode.exe')],check=True)
