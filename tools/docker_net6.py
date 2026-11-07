#!/usr/bin/env python3
"""docker_net6.py - create an IPv6-capable bridge network and attach containers.

usage: docker_net6.py create <name> <v4_cidr> <v6_cidr>
       docker_net6.py attach <name> <container> [<container> ...]
       docker_net6.py addrs <container>
"""
import json
import sys
import urllib.request

API = "http://192.168.1.43:2375"


def call(path: str, body=None, method="GET"):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(API + path, data=data, method=method,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=60) as resp:
        raw = resp.read()
        return resp.status, (json.loads(raw) if raw else {})


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    action = sys.argv[1]
    if action == "create":
        name, v4, v6 = sys.argv[2], sys.argv[3], sys.argv[4]
        body = {
            "Name": name,
            "Driver": "bridge",
            "EnableIPv6": True,
            "IPAM": {"Driver": "default", "Config": [
                {"Subnet": v4},
                {"Subnet": v6},
            ]},
        }
        print(call("/networks/create", body, "POST"))
    elif action == "attach":
        name = sys.argv[2]
        for container in sys.argv[3:]:
            try:
                print(container, call(f"/networks/{name}/connect",
                                      {"Container": container}, "POST"))
            except Exception as exc:  # noqa: BLE001
                print(container, "error", exc)
    elif action == "addrs":
        _, info = call(f"/containers/{sys.argv[2]}/json")
        for net, cfg in info["NetworkSettings"]["Networks"].items():
            print(net, "v4=", cfg.get("IPAddress"), "v6=", cfg.get("GlobalIPv6Address"))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
