"""Probe a board's UDP discovery reply from a PC on the same IPv4 subnet."""

import argparse
import socket
import struct
import time
import zlib


PORT = 50000
MAGIC = 0x52414E47
PACKET_SIZE = 36
PROBE_ID = (0x50435052, 0x4F424500, 0x00000001)


def make_discover(sequence: int) -> bytes:
    body = struct.pack(
        "!IBBHIIIII",
        MAGIC,
        1,
        1,
        PACKET_SIZE,
        sequence,
        *PROBE_ID,
        int(time.monotonic() * 1000) & 0xFFFFFFFF,
    ) + bytes(4)
    return body + struct.pack("!I", zlib.crc32(body))


def decode_packet(packet: bytes):
    if len(packet) != PACKET_SIZE:
        return None
    if zlib.crc32(packet[:32]) != struct.unpack("!I", packet[32:])[0]:
        return None
    fields = struct.unpack("!IBBHIIIII", packet[:28])
    if (fields[0], fields[1], fields[3]) != (MAGIC, 1, PACKET_SIZE):
        return None
    return fields[2], fields[4], fields[5:8], packet[28]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("board_ip", help="IPv4 address displayed by the board")
    parser.add_argument("--local-ip", default="0.0.0.0", help="PC IPv4 address")
    parser.add_argument("--broadcast", action="store_true", help="send to 169.254.255.255")
    args = parser.parse_args()

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        if args.broadcast:
            udp.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        udp.bind((args.local_ip, PORT))
        udp.settimeout(1.0)
        destination = "169.254.255.255" if args.broadcast else args.board_ip
        for sequence in range(1, 4):
            udp.sendto(make_discover(sequence), (destination, PORT))
            print(f"sent DISCOVER {sequence} to {destination}:{PORT}")
            deadline = time.monotonic() + 1.0
            while time.monotonic() < deadline:
                udp.settimeout(deadline - time.monotonic())
                try:
                    packet, sender = udp.recvfrom(512)
                except socket.timeout:
                    break
                decoded = decode_packet(packet)
                if decoded is None:
                    continue
                kind, reply_sequence, uid, role = decoded
                if uid == PROBE_ID:
                    continue
                print(
                    f"received type={kind} sequence={reply_sequence} "
                    f"uid={uid} role={role} from {sender}"
                )
                if sender[0] == args.board_ip and kind == 2:
                    return 0

    print("no valid ANNOUNCE reply")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
