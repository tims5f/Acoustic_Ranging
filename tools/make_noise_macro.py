import re
from pathlib import Path
root=Path(__file__).resolve().parents[1]
m=(root/'EWARM/LEDtest/List/LEDtest.map').read_text()
ls=['execUserAttach()','{']
fields=[('uwTick',32),('trigger_event_id',32),('trigger_accepted_count',32),('measure_history_count',32),('correlation_reject_count',32),('measure_impulse_pairs',32),('measure_kind_mismatch',32),('measure_impulse_rejects',32),('audio_noise_level',16),('audio_adaptive_gate',16),('last_reject_reason',8),('last_correlation_permille',16),('clock_state',8),('peer_online',8),('ethernet_tx_failures',32)]
for n,w in fields:
    a=int(re.search(r'^'+n+r"\s+(0x[0-9a-fA-F']+)",m,re.M)[1].replace("'",''),16)
    ls.append(f' __message "{n}=", __readMemory{w}(0x{a:x}, "Memory"), "\\n";')
ls.append('}')
(root/'tools/inspect_noise.mac').write_text('\n'.join(ls))
