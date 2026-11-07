#!/usr/bin/env python3
"""docker_cp.py - copy local files into a container over the Docker API.

usage: docker_cp.py <container> <dest_dir> <local_file> [local_file ...]
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
    container, dest = sys.argv[1], sys.argv[2]
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w") as tar:
        for path in sys.argv[3:]:
            name = path.rsplit("/", 1)[-1]
            info = tar.gettarinfo(path, arcname=name)
            info.uid = info.gid = 0
            info.mode = 0o755
            with open(path, "rb") as fh:
                tar.addfile(info, fh)
    buf.seek(0)
    url = f"{API}/containers/{container}/archive?path={urllib.parse.quote(dest)}"
    req = urllib.request.Request(url, data=buf.read(), method="PUT",
                                 headers={"Content-Type": "application/x-tar"})
    with urllib.request.urlopen(req, timeout=60) as resp:
        print(f"copied {len(sys.argv) - 3} file(s) to {container}:{dest} http={resp.status}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
