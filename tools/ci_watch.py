#!/usr/bin/env python3
"""ci_watch.py - wait for the GitHub Actions run of a commit, then fetch the IPA.

No token: uses the public API sparingly (one request per poll) and the public
release download URLs for the artifacts.

usage: ci_watch.py <owner/repo> <head_sha> [max_minutes]
prints progress lines; exits 0 when the IPA is downloaded and listed.
"""
import json
import sys
import time
import urllib.error
import urllib.request

API = "https://api.github.com"
OUT = "/tmp"


def api(path: str):
    req = urllib.request.Request(API + path, headers={
        "Accept": "application/vnd.github+json",
        "User-Agent": "anytv-ci-watch",
    })
    with urllib.request.urlopen(req, timeout=30) as resp:
        return json.load(resp)


def download(url: str, dest: str) -> tuple[int, int]:
    req = urllib.request.Request(url, headers={"User-Agent": "anytv-ci-watch"})
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            data = resp.read()
            open(dest, "wb").write(data)
            return resp.status, len(data)
    except urllib.error.HTTPError as exc:
        return exc.code, 0


def main() -> int:
    repo, sha = sys.argv[1], sys.argv[2]
    max_minutes = int(sys.argv[3]) if len(sys.argv) > 3 else 25
    deadline = time.time() + max_minutes * 60
    run = None
    while time.time() < deadline:
        try:
            runs = api(f"/repos/{repo}/actions/runs?per_page=10")["workflow_runs"]
        except urllib.error.HTTPError as exc:
            print(f"api {exc.code}; esperando 60s", flush=True)
            time.sleep(60)
            continue
        match = [r for r in runs if r.get("head_sha") == sha]
        if not match:
            print("run ainda nao apareceu; esperando 30s", flush=True)
            time.sleep(30)
            continue
        run = match[0]
        print(f"run {run['run_number']} status={run['status']} "
              f"conclusion={run['conclusion']} url={run['html_url']}", flush=True)
        if run["status"] == "completed":
            break
        time.sleep(45)

    if not run:
        print("SEM RUN")
        return 2

    number = run["run_number"]
    if run["conclusion"] != "success":
        code, size = download(
            f"https://github.com/{repo}/releases/download/log-{number}/log-{number}.txt",
            f"{OUT}/anytv-fail-log-{number}.txt")
        print(f"FALHOU conclusion={run['conclusion']} log:{code} {size}B")
        return 1

    code, size = download(
        f"https://github.com/{repo}/releases/download/build-{number}/AnyTV-unsigned.ipa",
        f"{OUT}/AnyTV-unsigned.ipa")
    print(f"IPA http={code} bytes={size}")
    code2, size2 = download(
        f"https://github.com/{repo}/releases/download/build-{number}/AnyTV-unsigned.ipa.sha256",
        f"{OUT}/AnyTV-unsigned.ipa.sha256")
    print(f"sha256 file http={code2} bytes={size2}")
    if code == 200 and size > 100000:
        return 0
    return 3


if __name__ == "__main__":
    sys.exit(main())
