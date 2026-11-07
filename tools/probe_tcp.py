#!/usr/bin/env python3
"""probe_tcp.py - check TCP reachability without shell nesting."""
import socket
import sys


def probe(host: str, port: int, timeout: float = 3.0) -> str:
    s = socket.socket()
    s.settimeout(timeout)
    try:
        s.connect((host, port))
        return "OPEN"
    except Exception as exc:  # noqa: BLE001 - diagnostic tool
        return f"fail: {exc}"
    finally:
        s.close()


if __name__ == "__main__":
    targets = sys.argv[1:] or ["172.17.0.8:5555", "172.18.0.26:5555"]
    for t in targets:
        host, _, port = t.partition(":")
        print(f"{t} {probe(host, int(port))}")
