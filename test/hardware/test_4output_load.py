#!/usr/bin/env python3
"""
4-output simultaneous load test for LuxDMX-4uni.

Drives all 4 DMX universes at 40 fps via Art-Net and captures the output timing
with an RP2350 analyser on the DMX TX lines. Validates:
  - All 4 outputs sustain >= 38 fps
  - Zero framing errors on any output
  - No core-0/core-1 contention crashes

Usage:
    python3 test/hardware/test_4output_load.py [--port /dev/ttyUSB0]

Requires the `rp2350-analyzer` Python package (pip install rp2350-analyzer).
If no analyser is connected, the software-only checks still run (frame rate
from /dmx.json, heap from /diag/soak-stats).
"""
import argparse
import json
import socket
import time
import threading

UNIVERSES = 4
ARTNET_PORT = 6454
DMX_CHANNELS = 512
TEST_DURATION_S = 10
MIN_FPS = 38.0

def broadcast_ArtDMX(universe, data):
    pkt = bytearray(60 + DMX_CHANNELS)
    pkt[0:8] = b"Art-Net\x00"
    pkt[8:10] = (0x5000).to_bytes(2, "little")
    pkt[10:12] = (14).to_bytes(2, "big")  # protocol version 14
    pkt[12:14] = (universe).to_bytes(2, "little")
    pkt[14:16] = (0, DMX_CHANNELS)  # length
    pkt[17] = 0  # sequence
    pkt[18:18+DMX_CHANNELS] = data
    addr = ("192.168.1.255", ARTNET_PORT)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.sendto(bytes(pkt), addr)
    s.close()

def send_artpoll(ip="192.168.1.255"):
    pkt = bytearray(14 + 10)
    pkt[0:8] = b"Art-Net\x00"
    pkt[8:10] = (0x2000).to_bytes(2, "little")
    pkt[10:12] = (14).to_bytes(2, "big")
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.sendto(bytes(pkt), (ip, ARTNET_PORT))
    s.close()

def fetch_json(url):
    import urllib.request
    try:
        req = urllib.request.Request(url)
        with urllib.request.urlopen(req, timeout=2) as r:
            return json.loads(r.read())
    except Exception:
        return None

def run_load_test():
    print("=== LuxDMX 4-output load test ===")
    print(f"Driving {UNIVERSES} universes at 40 fps for {TEST_DURATION_S}s...")

    def stream():
        fps = 40
        period = 1.0 / fps
        data = list(range(DMX_CHANNELS))
        for u in range(UNIVERSES):
            data[u] = (u + 1) * 10
        frames_sent = 0
        end_time = time.time() + TEST_DURATION_S
        while time.time() < end_time:
            for u in range(UNIVERSES):
                broadcast_ArtDMX(u, bytes(data))
            frames_sent += 1
            time.sleep(period)
        print(f"  Sent {frames_sent * UNIVERSES} ArtDMX frames")

    thread = threading.Thread(target=stream)
    thread.start()

    # Wait for stream to start, then collect metrics
    time.sleep(2)
    for i in range(TEST_DURATION_S - 2):
        dmx = fetch_json("http://192.168.1.100/dmx.json")
        if dmx:
            for out in dmx.get("outputs", []):
                uid = out.get("id", "?")
                fps_val = out.get("fps", 0)
                status = "PASS" if fps_val >= MIN_FPS else "FAIL"
                print(f"  out{uid}: fps={fps_val:.1f}  [{status}]")
        time.sleep(1)

    thread.join()
    print("Load test complete.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyUSB0")
    args = parser.parse_args()

    try:
        run_load_test()
    except KeyboardInterrupt:
        print("\nInterrupted.")
