#!/usr/bin/env python3
"""Teste do canal de controle do scrcpy 1.25 contra um Android real.

Objetivo: provar que dá para injetar teclas sem root, com baixa latência,
e descobrir em qual socket o controle viaja.
"""
import socket, struct, sys, time, hashlib

HOST, PORT = "127.0.0.1", 5601

CNXN, AUTH, OPEN, OKAY, CLSE, WRTE, STLS = 0x4e584e43, 0x48545541, 0x4e504f, 0x59414b4f, 0x45534c43, 0x45545257, 0x534c5453


class Adb:
    def __init__(self, host, port):
        self.s = socket.create_connection((host, port), timeout=6)
        self.s.settimeout(8)
        self.send(CNXN, 0x01000001, 4096, b"host::probe\x00")
        cmd, a0, a1, data = self.recv()
        if cmd != CNXN:
            raise RuntimeError(f"esperava CNXN, veio {cmd:08x}")
        self.banner = data

    def send(self, cmd, a0, a1, data=b""):
        h = struct.pack("<6I", cmd, a0, a1, len(data), sum(data) & 0xffffffff, cmd ^ 0xffffffff)
        self.s.sendall(h + data)

    def recv(self):
        hdr = b""
        while len(hdr) < 24:
            c = self.s.recv(24 - len(hdr))
            if not c:
                raise EOFError("conexao fechada")
            hdr += c
        cmd, a0, a1, ln, chk, mag = struct.unpack("<6I", hdr)
        data = b""
        while len(data) < ln:
            c = self.s.recv(ln - len(data))
            if not c:
                break
            data += c
        return cmd, a0, a1, data

    def open_service(self, name):
        self.send(OPEN, 0, 0, name.encode() + b"\x00")
        while True:
            cmd, a0, a1, data = self.recv()
            if cmd == OKAY and a0:
                return a0, a1
            if cmd == CLSE:
                raise RuntimeError("servico recusado: " + name)

    def read_until_close(self, lid, rid, timeout=15):
        self.s.settimeout(timeout)
        out = b""
        while True:
            try:
                cmd, a0, a1, data = self.recv()
            except (socket.timeout, EOFError):
                break
            if cmd == WRTE and a0 == rid:
                out += data
                self.send(OKAY, lid, rid)
            elif cmd == CLSE:
                break
        return out

    def shell(self, cmd, timeout=20):
        lid, rid = self.open_service("shell:" + cmd)
        return self.read_until_close(lid, rid, timeout).decode("utf-8", "replace")

    def push_stream(self, local, remote, timeout=90):
        """Envia o arquivo pelo stdin de um cat no aparelho."""
        data = open(local, "rb").read()
        lid, rid = self.open_service("shell:cat > " + remote)
        self.s.settimeout(timeout)
        chunk = 32 * 1024
        off = 0
        while off < len(data):
            part = data[off:off + chunk]
            self.send(WRTE, lid, rid, part)
            # espera o OKAY do aparelho antes do proximo bloco
            cmd, a0, a1, _ = self.recv()
            if cmd != OKAY:
                raise RuntimeError("fluxo interrompido ao enviar")
            off += len(part)
        self.send(CLSE, lid, rid)
        return len(data)

    def push(self, local, remote, timeout=60):
        return self.push_stream(local, remote, timeout)

    def _old_push(self, local, remote, timeout=60):
        data = open(local, "rb").read()
        lid, rid = self.open_service("sync:")
        def wrte(payload):
            self.send(WRTE, lid, rid, payload)
        path = remote.encode()
        wrte(b"SEND" + struct.pack("<I", len(path)) + path)
        off = 0
        chunk = 60 * 1024
        while off < len(data):
            part = data[off:off + chunk]
            wrte(b"DATA" + struct.pack("<I", len(part)) + part)
            off += len(part)
        wrte(b"DONE" + struct.pack("<I", 0))
        self.read_until_close(lid, rid, timeout)
        return len(data)


def main():
    jar = sys.argv[1] if len(sys.argv) > 1 else "/tmp/scrcpy-server-1.25"
    adb = Adb(HOST, PORT)
    print("conectado:", adb.banner[:60])

    size = adb.push(jar, "/data/local/tmp/scrcpy-server.jar")
    print("jar enviado:", size, "bytes")

    before = adb.shell("dumpsys window 2>/dev/null | grep -m1 mCurrentFocus")
    print("foco antes:", before.strip()[:80])

    start = ("CLASSPATH=/data/local/tmp/scrcpy-server.jar app_process /system/bin "
             "com.genymobile.scrcpy.Server 1.25 log_level=info max_size=1024 "
             "bit_rate=2000000 max_fps=15 lock_video_orientation=-1 tunnel_forward=false "
             "crop=- send_frame_meta=true control=true display_id=0 show_touches=false "
             "stay_awake=false codec_options=- encoder_name=-")
    lid, rid = adb.open_service("shell:" + start)
    time.sleep(2)

    # conecta no socket de video (o mesmo nome que o servidor usa como base)
    for name in ("localabstract:scrcpy", "localabstract:scrcpy_control"):
        try:
            vid, vrid = adb.open_service(name)
            print("socket", name, "abriu", vid)
            adb.s.settimeout(4)
            try:
                head = adb.s.recv(80)
                print("  bytes iniciais:", len(head), head[:24])
            except socket.timeout:
                print("  sem dados em 4s")
            adb.send(CLSE, vid, vrid)
        except Exception as e:
            print("socket", name, "falhou:", e)

    print("--- shell final do foco:")
    print(adb.shell("dumpsys window 2>/dev/null | grep -m1 mCurrentFocus").strip()[:80])


if __name__ == "__main__":
    main()
