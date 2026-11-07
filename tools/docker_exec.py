#!/usr/bin/env python3
"""docker_exec.py - run a command in a homelab container over the Docker API.

Uses Tty=true so the daemon returns a raw stream (no 8-byte frame headers).

usage: docker_exec.py <container> <shell-command...>
"""
import json
import sys
import urllib.request

API = "http://192.168.1.43:2375"


def run(container: str, cmd: str) -> tuple[int, str]:
    body = {"AttachStdout": True, "AttachStderr": True, "Tty": True, "Cmd": ["sh", "-c", cmd]}
    req = urllib.request.Request(f"{API}/containers/{container}/exec",
                                 data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"}, method="POST")
    exec_id = json.load(urllib.request.urlopen(req, timeout=60))["Id"]
    start = urllib.request.Request(f"{API}/exec/{exec_id}/start",
                                   data=json.dumps({"Detach": False, "Tty": True}).encode(),
                                   headers={"Content-Type": "application/json"}, method="POST")
    raw = urllib.request.urlopen(start, timeout=300).read()
    info = json.load(urllib.request.urlopen(f"{API}/exec/{exec_id}/json", timeout=60))
    text = raw.decode("utf-8", "replace").replace("\r\n", "\n").replace("\r", "")
    return info.get("ExitCode", -1), text


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    container = sys.argv[1]
    cmd = " ".join(sys.argv[2:])
    code, out = run(container, cmd)
    sys.stdout.write(out)
    if code not in (0, None):
        sys.stdout.write(f"\n[exit {code}]\n")
    return code if isinstance(code, int) else 0


if __name__ == "__main__":
    sys.exit(main())
