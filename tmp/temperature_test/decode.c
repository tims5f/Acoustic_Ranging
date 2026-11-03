#include <stdint.h>
uint8_t AppDht11_Decode(const uint8_t b[5], int16_t *temperature, uint16_t *humidity)
{
  int16_t t;
  uint16_t h;
  if(b==0 || temperature==0 || humidity==0 ||
     (uint8_t)(b[0]+b[1]+b[2]+b[3])!=b[4] || b[1]>9 || (b[3]&0x7FU)>9) return 0;
  h=(uint16_t)(b[0]*10U+b[1]);
  t=(int16_t)(b[2]*10U+(b[3]&0x7FU));
  if(b[3]&0x80U) t=(int16_t)-t;
  if(h>1000U || t < -200 || t>600) return 0;
  *temperature=t; *humidity=h;
  return 1;
}


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
