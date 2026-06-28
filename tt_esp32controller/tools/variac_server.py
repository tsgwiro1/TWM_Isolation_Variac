#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Lokaler Webserver fuer die Variac-Spannungs-Sequenz.

Liefert die Weboberflaeche (index.html) aus und startet auf Knopfdruck das
parametrierte Skript variac_run.py mit den im Formular eingegebenen Werten.
Die Live-Ausgabe des Skripts wird an den Browser durchgereicht; im manuellen
Modus kann ueber die Oberflaeche "Weiter" gesendet und jederzeit ein "Not-Aus"
ausgeloest werden.

Nur Python-Standardbibliothek - keine Installation noetig.

Start:
    python variac_server.py            # oeffnet http://127.0.0.1:8765 im Browser
    python variac_server.py --port 9000 --no-browser
"""

import argparse
import json
import os
import sys
import threading
import uuid
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from variac_sequence import Variac, VariacError  # noqa: E402

MAX_STEPS = 10

# Aktive Laeufe: id -> Session
_sessions = {}
_sessions_lock = threading.Lock()


class Session:
    """Ein laufender variac_run.py-Prozess samt gesammelter Ausgabe."""

    def __init__(self, proc):
        self.proc = proc
        self.lines = []
        self.lock = threading.Lock()
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        for line in self.proc.stdout:
            with self.lock:
                self.lines.append(line.rstrip("\n"))
        self.proc.stdout.close()

    def snapshot(self, since):
        with self.lock:
            new = self.lines[since:]
            total = len(self.lines)
        code = self.proc.poll()
        return {"lines": new, "next": total, "running": code is None, "code": code}

    def send_line(self, text=""):
        if self.proc.poll() is None and self.proc.stdin:
            try:
                self.proc.stdin.write(text + "\n")
                self.proc.stdin.flush()
            except (BrokenPipeError, OSError):
                pass

    def stop(self):
        if self.proc.poll() is None:
            try:
                self.proc.terminate()
            except OSError:
                pass


def _build_argv(cfg):
    """Baut die Kommandozeile fuer variac_run.py aus dem Formular-JSON."""
    voltages = cfg.get("voltages") or []
    if not voltages:
        raise ValueError("Keine Spannungen angegeben.")
    if len(voltages) > MAX_STEPS:
        raise ValueError("Maximal {} Spannungen erlaubt.".format(MAX_STEPS))
    for v in voltages:
        float(v)  # wirft ValueError bei ungueltigem Wert

    mode = "manual" if cfg.get("mode") == "manual" else "auto"
    argv = [
        sys.executable, "-u", os.path.join(HERE, "variac_run.py"),
        "--host", str(cfg.get("host", "192.168.0.116")),
        "--timeout", str(float(cfg.get("timeout", 5.0))),
        "--mode", mode,
        "--interval", str(float(cfg.get("interval", 5.0))),
        "--voltages",
    ] + [str(float(v)) for v in voltages]
    if cfg.get("limitOffAfterLast"):
        argv.append("--limit-off-after-last")
    if cfg.get("shutdown", True):
        argv.append("--shutdown")
    return argv


class Handler(BaseHTTPRequestHandler):
    # Ruhigeres Log.
    def log_message(self, fmt, *args):
        pass

    # ---- Hilfsfunktionen ----
    def _send_json(self, obj, status=200):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self):
        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length) if length else b"{}"
        return json.loads(raw.decode("utf-8") or "{}")

    def _serve_file(self, filename, content_type):
        path = os.path.join(HERE, filename)
        try:
            with open(path, "rb") as f:
                body = f.read()
        except OSError:
            self.send_error(404, "Not found")
            return
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    # ---- Routing ----
    def do_GET(self):
        parsed = urlparse(self.path)
        route = parsed.path
        if route in ("/", "/index.html"):
            self._serve_file("index.html", "text/html; charset=utf-8")
        elif route == "/api/status":
            self._api_status(parse_qs(parsed.query))
        elif route == "/api/poll":
            self._api_poll(parse_qs(parsed.query))
        else:
            self.send_error(404, "Not found")

    def do_POST(self):
        route = urlparse(self.path).path
        if route == "/api/start":
            self._api_start()
        elif route == "/api/next":
            self._api_next()
        elif route == "/api/estop":
            self._api_estop()
        else:
            self.send_error(404, "Not found")

    # ---- API-Endpunkte ----
    def _api_status(self, q):
        host = (q.get("host") or ["192.168.0.116"])[0]
        timeout = float((q.get("timeout") or ["5"])[0])
        try:
            st = Variac(host, timeout=timeout).get_status()
            self._send_json({"ok": True, "status": st})
        except VariacError as exc:
            self._send_json({"ok": False, "error": str(exc)})

    def _api_start(self):
        import subprocess
        try:
            cfg = self._read_json()
            argv = _build_argv(cfg)
        except (ValueError, json.JSONDecodeError) as exc:
            self._send_json({"ok": False, "error": str(exc)}, status=400)
            return
        env = dict(os.environ, PYTHONUNBUFFERED="1")
        proc = subprocess.Popen(
            argv, cwd=HERE, env=env,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, bufsize=1)
        sid = uuid.uuid4().hex
        with _sessions_lock:
            _sessions[sid] = Session(proc)
        self._send_json({"ok": True, "id": sid})

    def _api_poll(self, q):
        sid = (q.get("id") or [""])[0]
        since = int((q.get("since") or ["0"])[0])
        with _sessions_lock:
            sess = _sessions.get(sid)
        if not sess:
            self._send_json({"ok": False, "error": "Unbekannte Session."}, status=404)
            return
        snap = sess.snapshot(since)
        snap["ok"] = True
        self._send_json(snap)

    def _api_next(self):
        try:
            sid = self._read_json().get("id", "")
        except json.JSONDecodeError:
            sid = ""
        with _sessions_lock:
            sess = _sessions.get(sid)
        if not sess:
            self._send_json({"ok": False, "error": "Unbekannte Session."}, status=404)
            return
        sess.send_line("")
        self._send_json({"ok": True})

    def _api_estop(self):
        try:
            data = self._read_json()
        except json.JSONDecodeError:
            data = {}
        sid = data.get("id", "")
        host = data.get("host", "192.168.0.116")
        timeout = float(data.get("timeout", 5.0))

        # Laufenden Prozess beenden ...
        with _sessions_lock:
            sess = _sessions.get(sid)
        if sess:
            sess.stop()

        # ... und unabhaengig davon sofort sicheren Zustand herstellen.
        try:
            v = Variac(host, timeout=timeout)
            v.set_voltage(0)
            v.ensure_state("output_on", "toggle_output", False, "Ausgang")
            self._send_json({"ok": True, "message": "Not-Aus ausgefuehrt (0 V, Ausgang AUS)."})
        except VariacError as exc:
            self._send_json({"ok": False, "error": str(exc)})


def main():
    parser = argparse.ArgumentParser(description="Lokaler Webserver fuer die Variac-Sequenz.")
    parser.add_argument("--host", default="127.0.0.1", help="Bind-Adresse (Standard: lokal).")
    parser.add_argument("--port", type=int, default=8765, help="Port (Standard: 8765).")
    parser.add_argument("--no-browser", action="store_true", help="Browser nicht automatisch oeffnen.")
    args = parser.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    url = "http://{}:{}/".format(args.host, args.port)
    print("Variac-Weboberflaeche laeuft auf {}".format(url))
    print("Zum Beenden Strg+C druecken.")
    if not args.no_browser:
        try:
            webbrowser.open(url)
        except Exception:
            pass
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nServer wird beendet.")
        server.shutdown()


if __name__ == "__main__":
    main()
