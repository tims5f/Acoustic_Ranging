from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
s=(root/'Core/Src/app_measure.c').read_text()
a=s.index('  ready_result.arrival_difference_change_valid=previous_abs_delay_valid;')
b=s.index('  ready_result.sound_speed_mm_s=AppTemperature_GetSoundSpeed();',a)
block=s[a:b]
out=root/'tmp/arrival_change_test';out.mkdir(parents=True,exist_ok=True)
code='#include <stdint.h>\n#include <assert.h>\n#include <stdio.h>\nstatic uint32_t previous_abs_delay_ns; static uint8_t previous_abs_delay_valid;\nstatic struct {uint32_t arrival_difference_change_ns;uint8_t arrival_difference_change_valid;}ready_result;\nvoid update(int64_t delta){uint64_t magnitude=(delta<0)?(uint64_t)-delta:(uint64_t)delta;\n'+block+'}\n'
code+=r"""
int main(void){
 update(800000);assert(!ready_result.arrival_difference_change_valid);
 update(-805500);assert(ready_result.arrival_difference_change_valid && ready_result.arrival_difference_change_ns==5500);
 update(-805500);assert(ready_result.arrival_difference_change_ns==0);
 update(790000);assert(ready_result.arrival_difference_change_ns==15500);
 previous_abs_delay_valid=0;update(900000);assert(!ready_result.arrival_difference_change_valid);
 puts("PASS first-event placeholder, absolute-before-subtraction, zero change, rounding-independent ns, reset");return 0;
}
"""
(out/'test.c').write_text(code)
subprocess.run(['D:/mingw64/bin/gcc.exe','-std=c99','-Wall','-Wextra','-Werror',str(out/'test.c'),'-o',str(out/'test.exe')],check=True)
subprocess.run([str(out/'test.exe')],check=True)
