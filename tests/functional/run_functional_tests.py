#!/usr/bin/env python3
"""Functional (smoke) tests for aes67_daemon.

Spawns the daemon binary with short timeouts on ephemeral ports, sends real
SAP packets over multicast from a loopback alias, and asserts on the REST API.

Run:  python3 tests/functional/run_functional_tests.py [path/to/aes67_daemon]
Exit code 0 = all scenarios passed.
"""
import json
import os
import signal
import socket
import subprocess
import sys
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
SENDER = os.path.join(HERE, "sap_sender.py")

DAEMON = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "build", "aes67_daemon")

SAP_PORT = 19875
HTTP_PORT = 18080
GROUP = "239.255.255.255"
# Loopback alias used as a distinct "sender" source IP.
SENDER_IP = "127.0.0.2"
TIMEOUT = 3   # device timeout in seconds (short so eviction is fast)
EVICT = 1     # eviction sweep interval

failures = []


def check(label, cond, extra=""):
    status = "PASS" if cond else "FAIL"
    print(f"  [{status}] {label}" + (f"  ({extra})" if extra and not cond else ""))
    if not cond:
        failures.append(label)


def api(path):
    with urllib.request.urlopen(f"http://127.0.0.1:{HTTP_PORT}{path}", timeout=3) as r:
        return json.loads(r.read().decode())


def send(action, name, stream="239.69.10.20", port=5004, src_ip=SENDER_IP):
    subprocess.run(
        [sys.executable, SENDER, action, name, stream, str(port),
         "--dest", "127.0.0.1", "--port", str(SAP_PORT), "--src-ip", src_ip],
        check=True, capture_output=True)


def wait_for(predicate, timeout=5.0, desc="condition"):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            if predicate():
                return True
        except Exception:
            pass
        time.sleep(0.2)
    print(f"  timeout waiting for {desc}")
    return False


def main():
    env = dict(os.environ)
    env.update({
        "AES67_SAP_PORT": str(SAP_PORT),
        "AES67_HTTP_PORT": str(HTTP_PORT),
        "AES67_HTTP_HOST": "127.0.0.1",
        "AES67_DEVICE_TIMEOUT": str(TIMEOUT),
        "AES67_EVICT_INTERVAL": str(EVICT),
    })

    print(f"Starting daemon: {DAEMON}")
    proc = subprocess.Popen([DAEMON], env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        # wait for HTTP to come up
        ok = wait_for(lambda: api("/health").get("status") == "ok", timeout=10, desc="HTTP /health")
        check("daemon starts and /health responds", ok)

        # Scenario 1: announce -> device visible in API
        send("announce", "Func Test Sender", port=5004)
        ok = wait_for(lambda: any(d["name"] == "Func Test Sender" for d in api("/api/devices")),
                      desc="device in /api/devices")
        check("SAP announce makes device appear in /api/devices", ok)
        if ok:
            dev = next(d for d in api("/api/devices") if d["name"] == "Func Test Sender")
            check("device fields parsed (ip/stream/port)",
                  dev["ip"] == SENDER_IP and dev["stream_address"] == "239.69.10.20"
                  and dev["stream_port"] == 5004 and dev["stream_type"] == "audio",
                  extra=json.dumps(dev))

        # Scenario 2: second sender -> both visible
        send("announce", "Second Sender", stream="239.69.10.21", port=6000, src_ip="127.0.0.3")
        ok = wait_for(lambda: len(api("/api/devices")) >= 2, desc="two devices")
        check("second sender appears alongside the first", ok)

        # Scenario 3: SAP delete removes the device immediately
        send("delete", "Func Test Sender")
        ok = wait_for(lambda: all(d["name"] != "Func Test Sender" for d in api("/api/devices")),
                      desc="device removed after delete")
        check("SAP delete removes device from /api/devices", ok)

        # Scenario 4: stale device evicted after timeout (no more announces)
        ok = wait_for(lambda: all(d["name"] != "Second Sender" for d in api("/api/devices")),
                      timeout=TIMEOUT + EVICT + 6, desc="eviction after timeout")
        check(f"device evicted after {TIMEOUT}s without announces", ok)

        check("API empty after eviction", api("/api/devices") == [])
    finally:
        proc.send_signal(signal.SIGTERM)
        try:
            out, _ = proc.communicate(timeout=5)
            print("--- daemon log tail ---")
            print("\n".join(out.splitlines()[-8:]))
        except subprocess.TimeoutExpired:
            proc.kill()

    print()
    if failures:
        print(f"FUNCTIONAL TESTS FAILED: {len(failures)} scenario(s): {failures}")
        return 1
    print("ALL FUNCTIONAL TESTS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
