#!/usr/bin/env python3
"""try_key_variants.py - find the adb_keys line format adbd accepts on a test device.

Writes each variant, restarts adbd, runs the client and greps adbd's log.
usage: try_key_variants.py <container> <pub_b64_string>
"""
import json
import struct
import sys
import urllib.request

API = "http://192.168.1.43:2375"


def demux(raw: bytes) -> str:
    out, i = [], 0
    while i + 8 <= len(raw):
        length = struct.unpack(">I", raw[i + 4:i + 8])[0]
        out.append(raw[i + 8:i + 8 + length].decode("utf-8", "replace"))
        i += 8 + length
    return "".join(out) if i else raw.decode("utf-8", "replace")


def run(container: str, cmd: str) -> tuple[int, str]:
    body = {"AttachStdout": True, "AttachStderr": True, "Cmd": ["sh", "-c", cmd]}
    req = urllib.request.Request(f"{API}/containers/{container}/exec",
                                 data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"}, method="POST")
    exec_id = json.load(urllib.request.urlopen(req, timeout=60))["Id"]
    start = urllib.request.Request(f"{API}/exec/{exec_id}/start",
                                   data=json.dumps({"Detach": False, "Tty": False}).encode(),
                                   headers={"Content-Type": "application/json"}, method="POST")
    raw = urllib.request.urlopen(start, timeout=120).read()
    info = json.load(urllib.request.urlopen(f"{API}/exec/{exec_id}/json", timeout=60))
    return info.get("ExitCode", -1), demux(raw)


def main() -> int:
    container, pub = sys.argv[1], sys.argv[2].strip()
    variants = {
        "newline": f"{pub}\\n",
        "no-newline": pub,
        "no-padding": f"{pub.rstrip('=')}",
        "nul-terminated": f"{pub}\\0",
    }
    for label, content in variants.items():
        run(container, f"printf '%s' '{content}' > /data/misc/adb/adb_keys")
        run(container, "setprop ctl.restart adbd; sleep 2")
        run(container, "logcat -c")
        run(container, "cd /dev && ./anytv-cli ::1 5555 /dev/anytv.key info >/dev/null 2>&1")
        code, out = run(container, "logcat -d -s adbd 2>/dev/null | grep -E "
                                  "'authorized|Invalid base64|Failed to parse|loading keys' "
                                  "| cut -c1-70 | tail -4")
        print(f"--- {label} (len={len(content)})")
        for line in out.strip().splitlines():
            print("   ", line.strip())
    return 0


if __name__ == "__main__":
    sys.exit(main())
