#include <stdint.h>
#include <assert.h>
#include <stdio.h>
static uint32_t previous_abs_delay_ns; static uint8_t previous_abs_delay_valid;
static struct {uint32_t arrival_difference_change_ns;uint8_t arrival_difference_change_valid;}ready_result;
void update(int64_t delta){uint64_t magnitude=(delta<0)?(uint64_t)-delta:(uint64_t)delta;
  ready_result.arrival_difference_change_valid=previous_abs_delay_valid;
  ready_result.arrival_difference_change_ns=previous_abs_delay_valid ?
      ((uint32_t)magnitude>=previous_abs_delay_ns ?
       (uint32_t)magnitude-previous_abs_delay_ns : previous_abs_delay_ns-(uint32_t)magnitude) : 0U;
  previous_abs_delay_ns=(uint32_t)magnitude;
  previous_abs_delay_valid=1U;
}

int main(void){
 update(800000);assert(!ready_result.arrival_difference_change_valid);
 update(-805500);assert(ready_result.arrival_difference_change_valid && ready_result.arrival_difference_change_ns==5500);
 update(-805500);assert(ready_result.arrival_difference_change_ns==0);
 update(790000);assert(ready_result.arrival_difference_change_ns==15500);
 previous_abs_delay_valid=0;update(900000);assert(!ready_result.arrival_difference_change_valid);
 puts("PASS first-event placeholder, absolute-before-subtraction, zero change, rounding-independent ns, reset");return 0;
}
