import re
from pathlib import Path
root=Path(__file__).resolve().parents[1];m=(root/'EWARM/LEDtest/List/LEDtest.map').read_text()
lines=['execUserAttach()','{']
for n,w in [('uwTick',32),('correlation_runtime_ms',32),('correlation_runtime_max_ms',32),('measure_result_acks',32),('audio_mode',8),('audio_capture_mode',8),('measurement_audio_mode',8),('command_pending',8),('peer_online',8),('clock_state',8),('trigger_threshold_permille',16),('trigger_event_id',32),('trigger_accepted_count',32),('measure_history_count',32),('correlation_reject_count',32),('last_reject_reason',8),('measure_rx_filtered',32)]:
 a=int(re.search(r'^'+n+r"\s+(0x[0-9a-fA-F']+)",m,re.M)[1].replace("'",''),16)
 lines.append(f' __message "{n}=", __readMemory{w}(0x{a:x}, "Memory"), "\\n";')
lines.append('}');(root/'tools/inspect_mode.mac').write_text('\n'.join(lines))
