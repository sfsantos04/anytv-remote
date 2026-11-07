#!/usr/bin/env python3
"""test_protocols.py - prove the C transports against independent mock TVs.

Each mock implements the server side from the published protocol (HTTP for Roku
ECP, WebSocket for webOS SSAP and Tizen remote) with its own handshake code, so
the client's framing is checked by a second implementation, not by itself.

usage: test_protocols.py [path/to/anytv-cli]
"""
import base64
import hashlib
import json
import os
import socket
import subprocess
import sys
import threading
import time

CLI = sys.argv[1] if len(sys.argv) > 1 else "/tmp/anytv-cli2"
GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
RECEIVED: dict[str, list[str]] = {"roku": [], "webos": [], "tizen": [], "pointer": []}
FAILS: list[str] = []


def check(cond, msg):
    print(("ok   " if cond else "FALHA") + " " + msg)
    if not cond:
        FAILS.append(msg)


def read_until(sock, marker):
    data = b""
    while marker not in data:
        chunk = sock.recv(1)
        if not chunk:
            break
        data += chunk
    return data


def ws_handshake(sock, request):
    key = ""
    for line in request.decode().split("\r\n"):
        if line.lower().startswith("sec-websocket-key:"):
            key = line.split(":", 1)[1].strip()
    accept = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
    resp = ("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
            "Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n")
    sock.sendall(resp.encode())
    return key


def ws_read(sock):
    head = sock.recv(2)
    if len(head) < 2:
        return None
    opcode = head[0] & 0x0F
    masked = head[1] & 0x80
    length = head[1] & 0x7F
    if length == 126:
        length = int.from_bytes(sock.recv(2), "big")
    elif length == 127:
        length = int.from_bytes(sock.recv(8), "big")
    mask = sock.recv(4) if masked else b"\x00\x00\x00\x00"
    data = b""
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            break
        data += chunk
    if masked:
        data = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
    return opcode, data.decode("utf-8", "replace")


def ws_send(sock, text):
    payload = text.encode()
    header = bytes([0x81])
    if len(payload) < 126:
        header += bytes([len(payload)])
    else:
        header += bytes([126]) + len(payload).to_bytes(2, "big")
    sock.sendall(header + payload)


# ---------------------------------------------------------------- Roku ECP
def roku_server(port):
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", port))
    srv.listen(5)
    while True:
        conn, _ = srv.accept()
        try:
            request = read_until(conn, b"\r\n\r\n").decode()
            path = request.split(" ")[1]
            if path == "/query/device-info":
                body = ("<device-info><friendly-device-name>Cozinha Roku</friendly-device-name>"
                        "<model-name>Roku Ultra</model-name><serial-number>X00400ABC123</serial-number>"
                        "<wifi-mac>d8:31:34:aa:bb:cc</wifi-mac></device-info>")
            elif path == "/query/apps":
                body = ('<apps><app id="12" type="appl" version="2.0.0">Netflix</app>'
                        '<app id="2285" type="appl" version="5.0.0">Hulu</app></apps>')
            elif path.startswith("/keypress/") or path.startswith("/launch/"):
                RECEIVED["roku"].append(path)
                body = "<ok/>"
            else:
                body = "?"
            payload = body.encode()
            conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\n"
                         b"Content-Length: " + str(len(payload)).encode() +
                         b"\r\nConnection: close\r\n\r\n" + payload)
        finally:
            conn.close()


# ---------------------------------------------------------------- webOS SSAP
def webos_server(port):
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", port))
    srv.listen(5)
    while True:
        conn, _ = srv.accept()
        threading.Thread(target=webos_conn, args=(conn, port), daemon=True).start()


def webos_conn(conn, port):
    try:
        request = read_until(conn, b"\r\n\r\n")
        ws_handshake(conn, request)
        path = request.decode().split(" ")[1]
        if path.startswith("/resources"):
            while True:
                frame = ws_read(conn)
                if not frame:
                    break
                RECEIVED["pointer"].append(frame[1])
            return
        while True:
            frame = ws_read(conn)
            if not frame:
                break
            text = frame[1]
            try:
                msg = json.loads(text)
            except json.JSONDecodeError:
                continue
            if msg.get("type") == "register":
                client_key = msg.get("payload", {}).get("client-key") or "CLIENTKEY123"
                ws_send(conn, json.dumps({"id": "reg0", "type": "registered",
                                          "payload": {"client-key": client_key}}))
            elif msg.get("uri", "").endswith("getPointerInputSocket"):
                ws_send(conn, json.dumps({"id": msg["id"], "type": "response", "payload": {
                    "socketPath": f"ws://127.0.0.1:{port}/resources/pointer"}}))
            elif msg.get("uri", "").endswith("listApps"):
                ws_send(conn, json.dumps({"id": msg["id"], "type": "response", "payload": {
                    "apps": [{"id": "netflix", "title": "Netflix",
                              "icon": "/resources/icon/1.png"},
                             {"id": "youtube.leanback.v4", "title": "YouTube",
                              "icon": f"http://127.0.0.1:{port}/resources/icon/2.png"}],
                    "returnValue": True}}))
            elif msg.get("uri", "").endswith("insertText"):
                RECEIVED["webos"].append("TEXT:" + msg["payload"].get("text", ""))
                ws_send(conn, json.dumps({"id": msg["id"], "type": "response",
                                          "payload": {"returnValue": True}}))
            else:
                RECEIVED["webos"].append(msg.get("uri", "?"))
                ws_send(conn, json.dumps({"id": msg["id"], "type": "response",
                                          "payload": {"returnValue": True}}))
    except Exception as exc:  # noqa: BLE001 - mock
        print("webos mock error:", exc)
    finally:
        conn.close()


# ---------------------------------------------------------------- Tizen
def tizen_server(port):
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", port))
    srv.listen(5)
    while True:
        conn, _ = srv.accept()
        threading.Thread(target=tizen_conn, args=(conn,), daemon=True).start()


def tizen_conn(conn):
    try:
        request = read_until(conn, b"\r\n\r\n")
        path = request.decode().split(" ")[1]
        RECEIVED["tizen"].append("PATH:" + path)
        ws_handshake(conn, request)
        ws_send(conn, json.dumps({"data": {"clients": [], "id": "x", "token": "12345678"},
                                  "event": "ms.channel.connect"}))
        while True:
            frame = ws_read(conn)
            if not frame:
                break
            try:
                msg = json.loads(frame[1])
            except json.JSONDecodeError:
                continue
            params = msg.get("params", {})
            RECEIVED["tizen"].append("CMD:" + params.get("DataOfCmd", "?"))
    except Exception as exc:  # noqa: BLE001 - mock
        print("tizen mock error:", exc)
    finally:
        conn.close()


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def run(args, env_extra):
    env = dict(os.environ)
    env.update(env_extra)
    return subprocess.run([CLI] + args, capture_output=True, text=True, env=env, timeout=60)


def main():
    rport, wport, tport = free_port(), free_port(), free_port()
    threading.Thread(target=roku_server, args=(rport,), daemon=True).start()
    threading.Thread(target=webos_server, args=(wport,), daemon=True).start()
    threading.Thread(target=tizen_server, args=(tport,), daemon=True).start()
    time.sleep(0.4)

    # Roku
    out = run([f"127.0.0.1", str(rport), "x", "info"], {"ANYTV_PROTO": "roku"})
    check("Cozinha Roku" in out.stdout, f"roku info nome ({out.stdout.strip()[:60]})")
    out = run(["127.0.0.1", str(rport), "x", "apps"], {"ANYTV_PROTO": "roku"})
    check("12\tNetflix" in out.stdout and "2285\tHulu" in out.stdout, "roku lista de apps")
    check(f"http://127.0.0.1:{rport}/query/icon/12" in out.stdout, "roku icone servido pela propria TV")
    out = run(["127.0.0.1", str(rport), "x", "cmd", "OK"], {"ANYTV_PROTO": "roku"})
    check("/keypress/Select" in RECEIVED["roku"], "roku OK -> Select")
    out = run(["127.0.0.1", str(rport), "x", "cmd", "UP", "3"], {"ANYTV_PROTO": "roku"})
    check(RECEIVED["roku"].count("/keypress/Up") == 3, "roku repeticao x3")
    out = run(["127.0.0.1", str(rport), "x", "launch", "12"], {"ANYTV_PROTO": "roku"})
    check("/launch/12" in RECEIVED["roku"], "roku abrir app")
    out = run(["127.0.0.1", str(rport), "x", "cmd", "HOME"], {"ANYTV_PROTO": "roku"})
    check("token=" in out.stdout, "roku conecta e reporta")
    # MAC descoberto no device-info permite Wake-on-LAN
    check("d8:31:34:aa:bb:cc" in out.stdout, "roku MAC capturado para WoL")

    # webOS
    out = run(["127.0.0.1", str(wport), "x", "cmd", "DOWN"], {"ANYTV_PROTO": "webos"})
    check("connected proto=webos" in out.stdout, "webos registra e abre pointer")
    check("CLIENTKEY123" in out.stdout, "webos devolve client-key para persistir")
    check(any("name:DOWN" in item for item in RECEIVED["pointer"]), "webos botao no pointer socket")
    out = run(["127.0.0.1", str(wport), "x", "apps"], {"ANYTV_PROTO": "webos"})
    check("netflix\tNetflix" in out.stdout, "webos lista de apps")
    check(f"http://127.0.0.1:{wport}/resources/icon/1.png" in out.stdout, "webos icone relativo vira URL")
    check(f"http://127.0.0.1:{wport}/resources/icon/2.png" in out.stdout, "webos icone absoluto preservado")
    out = run(["127.0.0.1", str(wport), "x", "text", "oi"], {"ANYTV_PROTO": "webos"})
    check("TEXT:oi" in RECEIVED["webos"], "webos insertText")
    out = run(["127.0.0.1", str(wport), "x", "launch", "netflix"], {"ANYTV_PROTO": "webos"})
    check(any("system.launcher/launch" in item for item in RECEIVED["webos"]), "webos abrir app")

    # Tizen
    out = run(["127.0.0.1", str(tport), "x", "cmd", "LEFT"], {"ANYTV_PROTO": "tizen"})
    check("12345678" in out.stdout, "tizen captura token")
    check(any("CMD:KEY_LEFT" in item for item in RECEIVED["tizen"]), "tizen KEY_LEFT")
    check(any("PATH:/api/v2/channels/samsung.remote.control" in item for item in RECEIVED["tizen"]),
          "tizen canal correto")
    out = run(["127.0.0.1", str(tport), "x", "apps"], {"ANYTV_PROTO": "tizen"})
    # a lista vem do canal remoto (ed.installedApp.get); num mock sem esse evento
    # o app tem de falhar limpo, sem travar e dizendo o motivo
    check("apps rc=" in out.stdout and "installedApp" in out.stdout,
          f"tizen lista pelo canal remoto ({out.stdout.strip()[:120]})")

    print(f"\nfalhas={len(FAILS)}")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
