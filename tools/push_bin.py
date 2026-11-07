#!/usr/bin/env python3
"""push_bin.py - push a binary into a container that refuses archive uploads.

Encodes the file as a gzipped base64 stream, appends it in chunks through
`docker exec`, then decodes it inside the container.

usage: push_bin.py <container> <local_file> <remote_path> [chunk_size]
"""
import base64
import gzip
import json
import struct
import sys
import urllib.request

API = "http://192.168.1.43:2375"


def demux(raw: bytes) -> str:
    out = []
    i = 0
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
    raw = urllib.request.urlopen(start, timeout=300).read()
    info = json.load(urllib.request.urlopen(f"{API}/exec/{exec_id}/json", timeout=60))
    return info.get("ExitCode", -1), demux(raw)


def main() -> int:
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    container, local, remote = sys.argv[1], sys.argv[2], sys.argv[3]
    chunk = int(sys.argv[4]) if len(sys.argv) > 4 else 48000
    payload = base64.b64encode(gzip.compress(open(local, "rb").read(), 9)).decode()
    tmp = remote + ".b64"
    run(container, f"rm -f {tmp}")
    total = 0
    for i in range(0, len(payload), chunk):
        part = payload[i:i + chunk]
        code, out = run(container, f"printf %s '{part}' >> {tmp}")
        if code != 0:
            print("chunk failed", code, out[:200])
            return 1
        total += len(part)
    print(f"pushed {total} base64 chars")
    code, out = run(container, f"base64 -d {tmp} | gzip -d > {remote} && chmod 755 {remote} && ls -l {remote}")
    print(code, out.strip())
    run(container, f"rm -f {tmp}")
    return code


if __name__ == "__main__":
    sys.exit(main())
