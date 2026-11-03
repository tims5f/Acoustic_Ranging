"""Create a repeatable broadband pulse with a sharp onset for timing tests."""

import math
import struct
import wave
from pathlib import Path


rate = 48000
period = 2 * rate
duration = rate // 20
total = 30 * rate
output = Path(__file__).resolve().parent / "ranging_test_chirp.wav"

with wave.open(str(output), "wb") as wav:
    wav.setnchannels(1)
    wav.setsampwidth(2)
    wav.setframerate(rate)
    for start in range(0, total, 4096):
        frames = bytearray()
        for index in range(start, min(start + 4096, total)):
            pos = index % period
            if pos < duration:
                t = pos / rate
                f0 = 1200.0
                slope = (6000.0 - f0) / (duration / rate)
                phase = 2.0 * math.pi * (f0 * t + 0.5 * slope * t * t)
                envelope = min(1.0, pos / 5.0, (duration - pos) / 240.0)
                value = int(26000 * envelope * math.sin(phase))
            else:
                value = 0
            frames.extend(struct.pack("<h", value))
        wav.writeframesraw(frames)

print(output)
