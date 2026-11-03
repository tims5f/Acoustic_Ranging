"""Resolve diagnostics from the current link map before attaching via C-SPY."""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
link_map = (root / "EWARM/LEDtest/List/LEDtest.map").read_text(errors="replace")

def address(name):
    match = re.search(r"^" + re.escape(name) + r"\s+(0x[0-9a-fA-F']+)", link_map, re.M)
    if not match:
        raise ValueError(name)
    return int(match[1].replace("'", ""), 16)

lines = ["execUserAttach()", "{", "  __var i;", "  __var p;"]
for name in ["uwTick", "measure_history_count", "trigger_event_id", "trigger_accepted_count", "measure_tx_packets", "measure_tx_alloc_fail", "measure_tx_send_fail", "measure_rx_packets", "measure_rx_crc_fail", "measure_rx_filtered", "ethernet_tx_failures", "measure_result_acks"]:
    lines.append(f'  __message "{name}=", __readMemory32(0x{address(name):x}, "Memory"), "\\n";')
for name, width in [("clock_state", 8), ("last_correlation_permille", 16), ("correlation_reject_count", 32), ("last_alignment_lag", 32), ("last_peak_gap_permille", 32), ("last_alignment_delta_ns", 32), ("last_reject_reason", 8)]:
    lines.append(f'  __message "{name}=", __readMemory{width}(0x{address(name):x}, "Memory"), "\\n";')
for name, width in [("last_envelope_lag", 32), ("last_envelope_correlation", 16)]:
    lines.append(f'  __message "{name}=", __readMemory{width}(0x{address(name):x}, "Memory"), "\\n";')
lines.extend([
    "  for (i = 0; i < 32; i++)",
    "  {",
    f"    p = 0x{address('measure_history'):x} + i * 20;",
    '    __message "H,", i, ",", __readMemory32(p, "Memory"), ",", __readMemory32(p+4, "Memory"), ",", __readMemory32(p+8, "Memory"), ",", __readMemory32(p+12, "Memory"), ",", __readMemory16(p+16, "Memory"), ",", __readMemory16(p+18, "Memory"), "\\n";',
    "  }",
    "}",
])
extra = []
for name in ["local_event", "peer_event"]:
    base = address(name)
    for label, offset, width in [("id",16,32),("valid",26,8),("mask",27,8)]:
        extra.append(f'  __message "{name}_{label}=", __readMemory{width}(0x{base+offset:x}, "Memory"), "\\n";')
extra.append(f'  __message "start_delta_ns=", __readMemory64(0x{address("local_event"):x}, "Memory") - __readMemory64(0x{address("peer_event"):x}, "Memory"), "\\n";')
lines[-1:-1] = extra
output = root / "tools/inspect_history.mac"
output.write_text("\n".join(lines) + "\n")
compact = ["execUserAttach()", "{"] + [
    line for line in lines if line.strip().startswith("__message") and '"H,"' not in line
] + ["}"]
(root / "tools/inspect_transport.mac").write_text("\n".join(compact) + "\n")
print(output)
