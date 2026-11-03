"""Generate repeatable acoustic pulses for bench testing (not the course reference)."""

import math
import struct
import wave
from pathlib import Path


RATE = 48_000
SECONDS = 30
PERIOD = 2 * RATE
BURST = RATE // 10
RAMP = RATE // 1000
OUTPUT = Path(__file__).resolve().parent / "ranging_test_pulses.wav"


with wave.open(str(OUTPUT), "wb") as wav:
    wav.setnchannels(1)
    wav.setsampwidth(2)
    wav.setframerate(RATE)
    for block in range(SECONDS * RATE // 4096 + 1):
        data = bytearray()
        first = block * 4096
        for index in range(first, min(first + 4096, SECONDS * RATE)):
            phase = index % PERIOD
            if phase < BURST:
                envelope = min(phase / RAMP, 1.0, (BURST - phase) / RAMP)
                sample = int(26000 * envelope * math.sin(2 * math.pi * 2000 * phase / RATE))
            else:
                sample = 0
            data.extend(struct.pack("<h", sample))
        wav.writeframesraw(data)

print(OUTPUT)
