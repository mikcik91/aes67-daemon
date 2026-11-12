import socket
import time

MULTICAST_GROUP = '239.255.255.255'
PORT = 9875

sdp_payload = (
    "v=0\r\n"
    "o=- 123456 111111 IN IP4 192.168.1.100\r\n"
    "s=Virtual Q-SYS Core\r\n"
    "c=IN IP4 239.69.10.20/32\r\n"
    "t=0 0\r\n"
    "m=audio 5004 RTP/AVP 97\r\n"
)

sap_packet = b'\x20\x00\x00\x00\x7f\x00\x00\x01application/sdp\x00' + sdp_payload.encode('utf-8')

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)

print(f"Starting stream simulation on {MULTICAST_GROUP}:{PORT}...")

try:
    while True:
        sock.sendto(sap_packet, (MULTICAST_GROUP, PORT))
        print("Sent SAP packet (Virtual Q-SYS Core).")
        time.sleep(3)
except KeyboardInterrupt:
    print("\nSimulator stopped.")