#!/usr/bin/env python3
"""Minimal SAP sender for functional tests.

Usage:
    sap_sender.py announce <name> <stream_addr> <port> [--group G] [--port P] [--src-ip IP]
    sap_sender.py delete  <name> <stream_addr> <port> [--group G] [--port P] [--src-ip IP]
    sap_sender.py loop    <name> <stream_addr> <port> [--group G] [--port P] [--src-ip IP] [--interval S]

The source IP is controlled by binding the socket to --src-ip, so a single
host can simulate multiple distinct senders (the registry keys devices by
source IP).
"""
import argparse
import socket
import sys
import time

DEFAULT_GROUP = "239.255.255.255"
DEFAULT_PORT = 9875


def build_sap_packet(name: str, stream_addr: str, port: int, delete: bool) -> bytes:
    flags = 0x24 if delete else 0x20  # V=1 + T(delete) or V=1 announce
    header = bytes([flags, 0x00, 0x00, 0x00]) + b"\x7f\x00\x00\x01" + b"application/sdp\x00"
    sdp = (
        "v=0\r\n"
        "o=- 123456 111111 IN IP4 0.0.0.0\r\n"
        f"s={name}\r\n"
        f"c=IN IP4 {stream_addr}/32\r\n"
        "t=0 0\r\n"
        f"m=audio {port} RTP/AVP 97\r\n"
    )
    return header + sdp.encode("utf-8")


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("action", choices=["announce", "delete", "loop"])
    p.add_argument("name")
    p.add_argument("stream_addr")
    p.add_argument("port", type=int)
    p.add_argument("--group", default=DEFAULT_GROUP,
                   help="multicast group (used with --multicast)")
    p.add_argument("--multicast", action="store_true",
                   help="send to the multicast group instead of unicast --dest")
    p.add_argument("--dest", default="127.0.0.1",
                   help="unicast destination (default: 127.0.0.1, works in CI containers)")
    p.add_argument("--port", dest="sap_port", type=int, default=DEFAULT_PORT)
    p.add_argument("--src-ip", default="0.0.0.0")
    p.add_argument("--interval", type=float, default=1.0)
    args = p.parse_args()

    packet = build_sap_packet(args.name, args.stream_addr, args.port, delete=(args.action == "delete"))

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    if args.multicast:
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
    if args.src_ip != "0.0.0.0":
        sock.bind((args.src_ip, 0))

    dest = (args.group if args.multicast else args.dest, args.sap_port)
    if args.action == "loop":
        while True:
            sock.sendto(packet, dest)
            time.sleep(args.interval)
    else:
        sock.sendto(packet, dest)
        print(f"Sent SAP {args.action}: {args.name} -> {dest[0]}:{dest[1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
