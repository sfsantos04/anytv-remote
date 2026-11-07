#!/usr/bin/env python3
"""docker_get.py - read a file out of a container (no exec, no demux).

usage: docker_get.py <container> <absolute_path> <local_out>
"""
import io
import sys
import tarfile
import urllib.parse
import urllib.request

API = "http://192.168.1.43:2375"


def main() -> int:
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    container, remote, local = sys.argv[1], sys.argv[2], sys.argv[3]
    url = f"{API}/containers/{container}/archive?path={urllib.parse.quote(remote)}"
    with urllib.request.urlopen(url, timeout=60) as resp:
        raw = resp.read()
    with tarfile.open(fileobj=io.BytesIO(raw)) as tar:
        members = [m for m in tar.getmembers() if m.isfile()]
        if not members:
            print("no regular file in archive")
            return 1
        data = tar.extractfile(members[0]).read()
    open(local, "wb").write(data)
    print(f"{len(data)} bytes -> {local}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
