"""Resolve DHT/temperature diagnostics from the current IAR map."""
import re
from pathlib import Path
root=Path(__file__).resolve().parents[1]
m=(root/'EWARM/LEDtest/List/LEDtest.map').read_text()
fields=[('uwTick',32),('dht_attempts',32),('dht_successes',32),('dht_checksum_errors',32),('dht_timeout_errors',32),('dht_state',8),('dht_bits',8),('dht_temperature_deci_c',16),('dht_humidity_deci_percent',16),('local_valid',8),('peer_valid',8),('local_temperature',16),('peer_temperature',16),('selected_temperature',16),('ready',8),('source',8),('automatic',8),('local_role',8),('clock_state',8),('measure_history_count',32)]
lines=['execUserAttach()','{']
for name,width in fields:
    matches=re.findall(r'^'+name+r"\s+(0x[0-9a-fA-F']+)",m,re.M)
    if len(matches)!=1: raise ValueError((name,matches))
    address=int(matches[0].replace("'",''),16)
    lines.append(f'  __message "{name}=", __readMemory{width}(0x{address:x}, "Memory"), "\\n";')
for i in range(3):
    lines.append(f'  __message "UID{i}=", __readMemory32(0x{0x1ff0f420+i*4:x}, "Memory"), "\\n";')
lines.append('}')
(root/'tools/inspect_temperature.mac').write_text('\n'.join(lines)+'\n')
