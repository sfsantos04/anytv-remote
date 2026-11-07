#!/usr/bin/env python3
"""setup_adb_keys.py - pre-authorize a key on a test device (docker exec).

usage: setup_adb_keys.py <container> <local_keyfile> [<cli_binary>]
Writes the client public key into /data/misc/adb/adb_keys and restarts adbd.
"""
import struct
import subprocess
import sys
import urllib.request
import json

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
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    container, keyfile = sys.argv[1], sys.argv[2]
    cli = sys.argv[3] if len(sys.argv) > 3 else "/tmp/anytv-cli"
    pub = subprocess.run([cli, "127.0.0.1", "1", keyfile, "pub"],
                         capture_output=True, text=True, check=True).stdout.strip().splitlines()[-1]
    print("pubkey chars:", len(pub))
    cmds = [
        "mkdir -p /data/misc/adb",
        f"printf '%s\\n' '{pub}' > /data/misc/adb/adb_keys",
        "chmod 640 /data/misc/adb/adb_keys",
        "chown system:shell /data/misc/adb/adb_keys 2>/dev/null || true",
        "restorecon /data/misc/adb/adb_keys 2>/dev/null || true",
        "wc -l /data/misc/adb/adb_keys",
        "setprop ctl.restart adbd",
        "sleep 2",
        "getprop init.svc.adbd",
    ]
    for cmd in cmds:
        code, out = run(container, cmd)
        print(f"[{code}] {cmd[:60]} -> {out.strip()[:120]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
