"""A controller's end of IS-12, for the lane: reads a tile's BCP-008-01
receiver monitor over the NMOS Control Protocol, a WebSocket carrying JSON
commands, written against the standard library alone.

    ncp.py HOST PORT TILE status DOMAIN VALUE [SECONDS]
        waits until the status domain (link, connection, synchronization,
        stream, overall) is VALUE, and exits 1 with what it saw otherwise
    ncp.py HOST PORT TILE lost
        prints the monitor's lost packet counters as JSON
"""

import base64
import json
import os
import socket
import struct
import sys
import time

# NcReceiverMonitor property and method ids.
STATUS = {"overall": (3, 1), "link": (4, 1), "connection": (4, 4), "synchronization": (4, 7), "stream": (4, 11)}
GET_LOST = (4, 1)


class Ncp:
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=5)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall((f"GET /x-nmos/ncp/v1.0 HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\n"
                           f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
        head = b""
        while b"\r\n\r\n" not in head:
            chunk = self.sock.recv(1)
            if not chunk:
                raise OSError("IS-12 closed during the handshake")
            head += chunk
        if not head.startswith(b"HTTP/1.1 101"):
            raise OSError(f"IS-12 refused the WebSocket: {head!r}")
        self.handle = 0

    def exactly(self, n):
        data = b""
        while len(data) < n:
            chunk = self.sock.recv(n - len(data))
            if not chunk:
                raise OSError("IS-12 closed")
            data += chunk
        return data

    def send(self, opcode, payload):
        mask = os.urandom(4)
        n = len(payload)
        size = bytes([0x80 | n]) if n < 126 else bytes([0x80 | 126]) + struct.pack("!H", n)
        self.sock.sendall(bytes([0x80 | opcode]) + size + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))

    def message(self):
        data = b""
        while True:
            b0, b1 = self.exactly(2)
            n = b1 & 0x7F
            n = struct.unpack("!H", self.exactly(2))[0] if n == 126 else struct.unpack("!Q", self.exactly(8))[0] if n == 127 else n
            payload = self.exactly(n)
            opcode = b0 & 0x0F
            if opcode == 9:
                self.send(10, payload)
            elif opcode == 8:
                raise OSError("IS-12 closed")
            elif opcode in (0, 1):
                data += payload
                if b0 & 0x80:
                    return json.loads(data)

    def call(self, oid, level, index, arguments):
        self.handle += 1
        self.send(1, json.dumps({"messageType": 0, "commands": [
            {"handle": self.handle, "oid": oid, "methodId": {"level": level, "index": index}, "arguments": arguments}]}).encode())
        while True:
            m = self.message()
            if m.get("messageType") == 1:
                return next(r for r in m["responses"] if r["handle"] == self.handle)["result"]

    def get(self, oid, level, index):
        return self.call(oid, 1, 1, {"id": {"level": level, "index": index}})["value"]

    def monitor(self, tile):
        members = self.call(1, 2, 1, {"recurse": True})["value"]
        return next(m["oid"] for m in members if m["role"] == f"receiver-monitor-{tile}")


def main(host, port, tile, what, *args):
    ncp = Ncp(host, int(port))
    oid = ncp.monitor(tile)
    if what == "lost":
        print(json.dumps({c["name"]: c["value"] for c in ncp.call(oid, *GET_LOST, {})["value"]}, sort_keys=True))
        return 0
    domain, want = args[0], int(args[1])
    deadline = time.monotonic() + (float(args[2]) if len(args) > 2 else 15)
    while True:
        seen = ncp.get(oid, *STATUS[domain])
        if seen == want:
            return 0
        if time.monotonic() > deadline:
            print(f"{tile} {domain} status {seen}, not {want}")
            return 1
        time.sleep(0.5)


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
