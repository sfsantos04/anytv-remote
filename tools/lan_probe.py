#!/usr/bin/env python3
"""lan_probe.py - detect TV control surfaces on the local /24.

5555 = ADB (Android TV), 8001/8002 = Tizen remote, 3000/3001 = webOS,
26101 = Tizen sdb (install), 9922 = webOS dev SSH (install), 6466 = Android TV
remote v2 (Google), 8009 = DIAL/cast.
"""
import concurrent.futures
import socket
import sys

PORTS = {
    5555: "ADB/Android TV",
    6466: "AndroidTV remote v2",
    8001: "Tizen ws",
    8002: "Tizen wss",
    3000: "webOS ws",
    3001: "webOS wss",
    26101: "Tizen sdb (install)",
    9922: "webOS ssh (install)",
    8009: "DIAL/cast",
}


def probe(host: str, port: int, timeout: float = 0.7) -> bool:
    s = socket.socket()
    s.settimeout(timeout)
    try:
        s.connect((host, port))
        return True
    except Exception:  # noqa: BLE001 - probe
        return False
    finally:
        s.close()


def main() -> int:
    prefix = sys.argv[1] if len(sys.argv) > 1 else "192.168.1"
    hosts = [f"{prefix}.{i}" for i in range(1, 255)]
    jobs = [(h, p) for h in hosts for p in PORTS]
    found = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=160) as pool:
        futures = {pool.submit(probe, h, p): (h, p) for h, p in jobs}
        for future in concurrent.futures.as_completed(futures):
            host, port = futures[future]
            if future.result():
                found.append((host, port))
    found.sort(key=lambda item: (item[0], item[1]))
    if not found:
        print("nada encontrado")
        return 0
    for host, port in found:
        print(f"{host}:{port} {PORTS[port]}")
    by_host: dict[str, list[int]] = {}
    for host, port in found:
        by_host.setdefault(host, []).append(port)
    print("---")
    for host, ports in sorted(by_host.items()):
        names = ", ".join(PORTS[p] for p in ports)
        print(f"{host} -> {names}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
