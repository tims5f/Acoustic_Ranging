"""Exercise the firmware's actual live history reader with synthetic PCM."""
from pathlib import Path
import ctypes as C
import _ctypes
import random
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'Core/Src/app_audio.c').read_text()
function = source.split('uint8_t AppAudio_CopyLiveWaveform(', 1)[1]
function = 'uint8_t AppAudio_CopyLiveWaveform(' + function.split(
    '\nvoid BSP_AUDIO_IN_HalfTransfer_CallBack', 1)[0]
prefix = '''#include <stdint.h>
#include <stddef.h>
#define APP_AUDIO_SAMPLE_RATE_HZ 48000U
#define APP_AUDIO_WAVEFORM_SAMPLES 160U
#define AUDIO_LIVE_RING_SAMPLES 4096U
#define APP_AUDIO_RUNNING 1U
uint8_t audio_state = 1;
uint16_t live_head, live_count;
uint64_t live_end_ns;
int16_t live_ring[4096];
uint32_t __get_PRIMASK(void) { return 0; }
void __disable_irq(void) {}
void __set_PRIMASK(uint32_t mask) {}
'''
with tempfile.TemporaryDirectory(prefix='live-wave-') as temp:
    cpath = Path(temp) / 'reader.c'
    dllpath = Path(temp) / 'reader.dll'
    cpath.write_text(prefix + function)
    subprocess.run(['D:/mingw64/bin/gcc.exe', '-shared', '-O2', str(cpath),
                    '-o', str(dllpath)], check=True)
    lib = C.CDLL(str(dllpath))
    read = lib.AppAudio_CopyLiveWaveform
    read.argtypes = [C.POINTER(C.c_int16), C.c_uint16, C.c_uint64]
    read.restype = C.c_uint8
    ring = (C.c_int16 * 4096).in_dll(lib, 'live_ring')
    head = C.c_uint16.in_dll(lib, 'live_head')
    count = C.c_uint16.in_dll(lib, 'live_count')
    end = C.c_uint64.in_dll(lib, 'live_end_ns')
    output = (C.c_int16 * 160)()
    rng = random.Random(42)
    # Different callback phases and ring wrap positions must reproduce the
    # same common-clock window, including the physical inter-board delay.
    for _ in range(200):
        head.value = rng.randrange(4096)
        count.value = 4096
        age = rng.randrange(960, 3360)
        end.value = 1000000000 + round(age * 1e9 / 48000)
        for i in range(4096):
            ring[(head.value - 4096 + i) % 4096] = i - 2048
        assert read(output, 160, 1000000000) == 1
        assert list(output) == list(range(2048 - age, 2208 - age))
    assert read(output, 160, end.value) == 0  # Future/incomplete window.
    assert read(output, 160, 0) == 0  # Expired history.
    assert read(output, 159, 1000000000) == 0
    count.value = 100
    assert read(output, 160, 1000000000) == 0  # Startup history unavailable.
    print('PASS: 200 PCM windows, ring wrap, callback phase, invalid history.')
    _ctypes.FreeLibrary(lib._handle)
