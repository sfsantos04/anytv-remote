#!/usr/bin/env python3
"""verify_ipa.py - prove the IPA is what it claims: hash, plist, Mach-O, symbols.

usage: verify_ipa.py <ipa> [<published .sha256 file>]
"""
import hashlib
import plistlib
import struct
import sys
import zipfile

LC_BUILD_VERSION = 0x32
CPU_TYPE_ARM64 = 0x0100000C
MH_PIE = 0x200000


def macho_report(data: bytes) -> dict:
    magic, cputype, cpusub, filetype, ncmds, sizeofcmds, flags = struct.unpack(
        "<IiiIIII", data[:28])
    info = {
        "magic": hex(magic),
        "cputype": hex(cputype & 0xFFFFFFFF),
        "arm64": (cputype & 0xFFFFFFFF) == CPU_TYPE_ARM64,
        "filetype": filetype,  # 2 = MH_EXECUTE
        "pie": bool(flags & MH_PIE),
        "platform": None,
        "minos": None,
        "sdk": None,
    }
    off = 32
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack("<II", data[off:off + 8])
        if cmd == LC_BUILD_VERSION:
            plat, minos, sdk = struct.unpack("<III", data[off + 8:off + 20])
            info["platform"] = plat
            info["minos"] = f"{minos >> 16}.{(minos >> 8) & 0xff}.{minos & 0xff}"
            info["sdk"] = f"{sdk >> 16}.{(sdk >> 8) & 0xff}.{sdk & 0xff}"
        off += cmdsize
    return info


def main() -> int:
    ipa = sys.argv[1]
    raw = open(ipa, "rb").read()
    digest = hashlib.sha256(raw).hexdigest()
    print(f"sha256={digest}")
    if len(sys.argv) > 2:
        published = open(sys.argv[2], encoding="utf-8").read().split()[0]
        print(f"publicado={published} confere={published == digest}")

    zf = zipfile.ZipFile(ipa)
    names = zf.namelist()
    print(f"entradas={len(names)} __MACOSX={'__MACOSX' in ' '.join(names)}")
    app_plist = [n for n in names if n.endswith(".app/Info.plist")]
    exes = [n for n in names if n.count("/") == 2 and n.startswith("Payload/")
            and not n.endswith("/")]
    print("plist:", app_plist[0] if app_plist else "AUSENTE")
    info = plistlib.loads(zf.read(app_plist[0])) if app_plist else {}
    for key in ("CFBundleIdentifier", "CFBundleShortVersionString", "CFBundleVersion",
                "MinimumOSVersion", "CFBundleSupportedPlatforms", "CFBundleExecutable"):
        print(f"  {key}={info.get(key)}")
    icons = info.get("CFBundleIcons") or {}
    print("  CFBundleIcons:", "sim" if icons else "nao")

    exe_name = "Payload/" + info.get("CFBundleExecutable", "AnyTV") + ".app/" + \
               info.get("CFBundleExecutable", "AnyTV")
    mode = zf.getinfo(exe_name).external_attr >> 16
    print(f"executavel={exe_name} modo={oct(mode)}")

    data = zf.read(exe_name)
    rep = macho_report(data)
    print("macho:", rep)
    print("assets.car:", [i.file_size for i in zf.infolist() if i.filename.endswith("Assets.car")])
    print("appicon png:", [i.filename for i in zf.infolist() if "AppIcon60x60" in i.filename])

    for symbol in (b"anytv_client_connect", b"anytv_client_screenshot", b"TVClient",
                   b"AnyTVApp", b"adb_connect_ex"):
        print(f"simbolo {symbol.decode()}: {data.count(symbol)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
