"""Minimal WebSocket client for watching Corridor route alerts. Standard library only.

    python3 tools/ws_listen.py "ws://127.0.0.1:8080/alerts?from=KPHX&to=KIXD&alt=13500&gs=140"

Prints each message as it arrives. --count N exits after N messages, --timeout S
gives up after S seconds of silence.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import socket
import struct
import sys
from urllib.parse import urlsplit

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
OP_TEXT, OP_CLOSE, OP_PING, OP_PONG = 0x1, 0x8, 0x9, 0xA


def read_exact(sock: socket.socket, count: int) -> bytes:
    data = b""
    while len(data) < count:
        chunk = sock.recv(count - len(data))
        if not chunk:
            raise ConnectionError("server closed the connection")
        data += chunk
    return data


def send_frame(sock: socket.socket, opcode: int, payload: bytes = b"") -> None:
    """Client frames must be masked (RFC 6455 section 5.3)."""
    mask = os.urandom(4)
    head = bytes([0x80 | opcode])
    if len(payload) < 126:
        head += bytes([0x80 | len(payload)])
    else:
        head += bytes([0x80 | 126]) + struct.pack(">H", len(payload))
    sock.sendall(head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))


def read_frame(sock: socket.socket) -> tuple[int, bytes]:
    b0, b1 = read_exact(sock, 2)
    length = b1 & 0x7F
    if length == 126:
        (length,) = struct.unpack(">H", read_exact(sock, 2))
    elif length == 127:
        (length,) = struct.unpack(">Q", read_exact(sock, 8))
    return b0 & 0x0F, read_exact(sock, length)


def connect(url: str, timeout: float) -> socket.socket:
    parts = urlsplit(url)
    if parts.scheme != "ws":
        sys.exit("only ws:// URLs are supported")
    sock = socket.create_connection((parts.hostname, parts.port or 80), timeout=timeout)
    key = base64.b64encode(os.urandom(16)).decode()
    target = parts.path + ("?" + parts.query if parts.query else "")
    sock.sendall(
        (f"GET {target} HTTP/1.1\r\nHost: {parts.netloc}\r\nUpgrade: websocket\r\n"
         f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
         "Sec-WebSocket-Version: 13\r\n\r\n").encode()
    )

    head = b""
    while b"\r\n\r\n" not in head:
        chunk = sock.recv(1)
        if not chunk:
            break
        head += chunk
    status_line = head.split(b"\r\n", 1)[0].decode(errors="replace")
    if " 101 " not in status_line:
        length = 0
        for line in head.split(b"\r\n"):
            if line.lower().startswith(b"content-length:"):
                length = int(line.split(b":", 1)[1])
        body = read_exact(sock, length).decode(errors="replace").strip() if length else ""
        sys.exit(f"upgrade refused: {status_line} {body}")

    expected = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
    if f"sec-websocket-accept: {expected}".lower() not in head.decode(errors="replace").lower():
        sys.exit("server sent the wrong Sec-WebSocket-Accept value")
    return sock


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("url")
    parser.add_argument("--count", type=int, default=0, help="exit after this many messages")
    parser.add_argument("--timeout", type=float, default=0, help="seconds of silence to allow")
    parser.add_argument("--full", action="store_true", help="print the whole JSON message")
    args = parser.parse_args()

    sock = connect(args.url, args.timeout or 10)
    sock.settimeout(args.timeout or None)
    received = 0
    try:
        while not args.count or received < args.count:
            opcode, payload = read_frame(sock)
            if opcode == OP_PING:
                send_frame(sock, OP_PONG, payload)
            elif opcode == OP_CLOSE:
                code = struct.unpack(">H", payload[:2])[0] if len(payload) >= 2 else None
                print(f"server closed the connection (code {code})", flush=True)
                return
            elif opcode == OP_TEXT:
                received += 1
                message = json.loads(payload)
                if args.full:
                    print(json.dumps(message, indent=2), flush=True)
                elif message.get("type") == "alert":
                    for change in message["changes"]:
                        print(f"ALERT   {change}", flush=True)
                else:
                    status = message["status"]
                    print(f"STATUS  {status['from']} {status['departure_category']} -> "
                          f"{status['to']} {status['destination_category']}, "
                          f"{len(status['hazards'])} hazards on route, "
                          f"crew oxygen required: {status['crew_oxygen_required']}", flush=True)
        send_frame(sock, OP_CLOSE, struct.pack(">H", 1000))
    except socket.timeout:
        sys.exit(f"no message for {args.timeout:g} s (received {received})")
    finally:
        sock.close()


if __name__ == "__main__":
    main()
