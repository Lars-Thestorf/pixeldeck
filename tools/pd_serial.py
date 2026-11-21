#!/usr/bin/env python3
"""Dev-Kanal zur pixeldeck-Konsole (ESP32 über die xmega-USB-Brücke).

Jedes Öffnen des Ports startet den ESP neu (Linux setzt DTR/RTS, der xmega zieht dann EN).
Deshalb hält "serve" den Port dauerhaft offen, schreibt das Log in eine Datei und nimmt
Befehle über eine FIFO an. "cmd" und "fb" nutzen den Server automatisch, wenn er läuft.
Vor "idf.py flash" den Server beenden.

Beispiele:
  pd_serial.py serve &                        Port offen halten (Log: ~/.cache/pixeldeck/serial.log)
  pd_serial.py log --seconds 10 [--reset]     Log mitlesen (ohne Server; startet den ESP neu)
  pd_serial.py cmd stat                       Befehl senden, Antworten ausgeben
  pd_serial.py cmd "press start,select 1500"
  pd_serial.py fb bild.png [--scale 8]        aktuelles Bild als PNG

Befehle (nur Dev-Build, PD_DEV=ON): fb, press <tasten> [ms], stick <lx> <ly> <rx> <ry> [ms],
stat, raw on|off, hang, crash, reset. Tasten: a,b,x,y,l,r,start,select,up,down,left,right,l3,r3.

Braucht pyserial (im ESP-IDF-Python enthalten); "fb" braucht zusätzlich Pillow.
"""
import argparse
import base64
import os
import struct
import sys
import time

import serial


def open_port(port, reset=False):
    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.1
    # DTR/RTS steuern über den xmega EN/BOOT des ESP (wie bei esptool). Beide aus = normal.
    s.dtr = False
    s.rts = False
    s.open()
    if reset:
        s.rts = True   # EN low
        time.sleep(0.1)
        s.rts = False
    return s


def read_lines(s, seconds, until=None):
    end = time.time() + seconds
    buf = b""
    while time.time() < end:
        buf += s.read(4096)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", "replace").rstrip("\r")
            yield text
            if until and until(text):
                return


def send(s, command):
    s.write(("!" + command + "\n").encode())
    s.flush()


STATE_DIR = os.path.expanduser("~/.cache/pixeldeck")
LOG = os.path.join(STATE_DIR, "serial.log")
FIFO = os.path.join(STATE_DIR, "serial.fifo")
PID = os.path.join(STATE_DIR, "serial.pid")


def server_running():
    try:
        pid = int(open(PID).read())
        os.kill(pid, 0)
        return os.path.exists(FIFO)
    except (OSError, ValueError):
        return False


def serve(port, reset):
    os.makedirs(STATE_DIR, exist_ok=True)
    if not os.path.exists(FIFO):
        os.mkfifo(FIFO)
    open(PID, "w").write(str(os.getpid()))
    fifo = os.open(FIFO, os.O_RDONLY | os.O_NONBLOCK)
    s = open_port(port, reset)
    with open(LOG, "ab", buffering=0) as log:
        try:
            while True:
                data = s.read(4096)
                if data:
                    log.write(data)
                try:
                    cmd = os.read(fifo, 512)
                except BlockingIOError:
                    cmd = b""
                for line in cmd.decode(errors="replace").splitlines():
                    if line.strip():
                        send(s, line.strip())
        finally:
            os.remove(PID)


def via_server(command, seconds, until=None):
    """Befehl an den Server, Antwort aus dem Log ab der aktuellen Position lesen."""
    with open(LOG, "rb") as log:
        log.seek(0, os.SEEK_END)
        with open(FIFO, "w") as f:
            f.write(command + "\n")
        end = time.time() + seconds
        buf = b""
        while time.time() < end:
            chunk = log.read()
            if not chunk:
                time.sleep(0.05)
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode("utf-8", "replace").rstrip("\r")
                yield text
                if until and until(text):
                    return


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="/dev/ttyACM0")
    sub = ap.add_subparsers(dest="mode", required=True)
    p = sub.add_parser("serve")
    p.add_argument("--reset", action="store_true")
    p = sub.add_parser("log")
    p.add_argument("--seconds", type=float, default=10)
    p.add_argument("--reset", action="store_true")
    p = sub.add_parser("cmd")
    p.add_argument("command")
    p.add_argument("--wait", type=float, default=1.5)
    p = sub.add_parser("fb")
    p.add_argument("out")
    p.add_argument("--scale", type=int, default=8)
    a = ap.parse_args()

    if a.mode == "serve":
        serve(a.port, a.reset)
        return
    use_server = a.mode in ("cmd", "fb") and server_running()

    def run(command, seconds, until=None):
        if use_server:
            yield from via_server(command, seconds, until)
        else:
            s = open_port(a.port)
            time.sleep(1.5)  # Öffnen hat den ESP neu gestartet
            s.reset_input_buffer()
            send(s, command)
            yield from read_lines(s, seconds, until)

    if a.mode == "log":
        s = open_port(a.port, reset=a.reset)
        for line in read_lines(s, a.seconds):
            print(line, flush=True)
    elif a.mode == "cmd":
        for line in run(a.command, a.wait):
            if line.startswith("@@") or not use_server:
                print(line, flush=True)
    elif a.mode == "fb":
        from PIL import Image
        data = None
        for line in run("fb", 3.0, until=lambda t: "@@FB " in t):
            if "@@FB " in line:
                data = base64.b64decode(line.split("@@FB ", 1)[1])
        if not data or len(data) != 64 * 32 * 2:
            sys.exit("keine gültige Antwort auf !fb")
        px = struct.unpack("<2048H", data)
        img = Image.new("RGB", (64, 32))
        img.putdata([(((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31) for c in px])
        if a.scale > 1:
            img = img.resize((64 * a.scale, 32 * a.scale), Image.NEAREST)
        img.save(a.out)
        print(a.out)

if __name__ == "__main__":
    main()
