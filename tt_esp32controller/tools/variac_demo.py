#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Messe-Demo fuer den TWM Isolation Variac Controller.

Faehrt eine Vorfuehr-Choreografie in Endlosschleife: sanfter Hochlauf in Stufen,
Preset-Spruenge, Feinregelung, Rueckfahrt auf 0 V. Gedacht fuer den Dauerbetrieb
am Messestand - laeuft ohne Rueckfragen und bringt das Geraet bei Strg+C, Fehler
oder Abbruch immer in den sicheren Zustand (0 V, Ausgang AUS).

Unterschied zu variac_sequence.py: Das ist der interaktive Testlauf mit Abfragen.
Dieses Skript hier ist die unbeaufsichtigte Vorfuehrung.

Sicherheitsnetze:
  - harte Obergrenze im Skript (HARD_MAX_V), --max kann nur darunter bleiben
  - Temperaturwaechter: ueber TEMP_PAUSE_C wird pausiert und auf TEMP_RESUME_C
    abgekuehlt, bevor es weitergeht (Dauerbetrieb ueber Stunden)
  - Presets oberhalb der gewaehlten Obergrenze werden uebersprungen
  - laeuft das Geraet gerade ein Update (HTTP 503, Bediensperre ab V4.7.0),
    wartet die Demo, statt Befehle zu verwerfen
  - --dry-run fuehrt die komplette Choreografie ohne eingeschalteten Ausgang vor

Verwendet ausschliesslich die Python-Standardbibliothek.

Beispiele:
    python variac_demo.py --host 192.168.0.155
    python variac_demo.py --host 192.168.0.155 --max 150 --hold 8 --pause 20
    python variac_demo.py --host 192.168.0.155 --cycles 1 --dry-run
"""

import argparse
import os
import signal
import sys
import time

# Damit der Import auch aus einem anderen Arbeitsverzeichnis funktioniert.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from variac_sequence import Variac, VariacError  # noqa: E402

# --- Sicherheitsgrenzen (bewusst im Code, nicht als Parameter) ---------------
HARD_MAX_V = 230.0        # hoeher laesst dieses Skript die Demo nicht fahren
TEMP_PAUSE_C = 50.0       # ab hier Demo pausieren (Geraet: Luefter voll bei 60 C)
TEMP_RESUME_C = 45.0      # ... und erst darunter weitermachen
TEMP_ABORT_C = 58.0       # darueber Demo beenden statt nur pausieren

# --- Voreinstellungen --------------------------------------------------------
DEFAULT_HOST = "192.168.0.155"
DEFAULT_MAX_V = 200.0
DEFAULT_HOLD_S = 6.0      # Haltezeit je Stufe
DEFAULT_PAUSE_S = 15.0    # Ruhepause zwischen zwei Durchlaeufen
STEP_FRACTIONS = (0.15, 0.35, 0.60, 0.85, 1.00)   # Hochlauf, relativ zur Obergrenze
FINE_DELTA_V = 8.0        # Sollwertsprung fuer die Feinregelungs-Vorfuehrung

REACH_TIMEOUT_S = 35.0
RETRY_MAX = 5             # Wiederholungen bei Netz-/Geraetefehlern
RETRY_WAIT_S = 2.0

_stop = False             # wird vom Signal-Handler gesetzt


def _on_signal(signum, frame):
    """SIGINT/SIGTERM nur vormerken - abgeschaltet wird geordnet in der Schleife."""
    global _stop
    _stop = True
    print("\n  >> Abbruch angefordert, fahre geordnet herunter ...", flush=True)


def log(msg=""):
    print(msg, flush=True)


def stamp():
    return time.strftime("%H:%M:%S")


def bar(value, maximum, width=32):
    """Kleiner ASCII-Balken - macht die Vorfuehrung auf dem Laptop anschaulich."""
    if maximum <= 0:
        return ""
    filled = int(round(max(0.0, min(1.0, value / maximum)) * width))
    return "[" + "#" * filled + "-" * (width - filled) + "]"


class DemoAborted(Exception):
    """Geordneter Abbruch der Vorfuehrung."""


class Demo:
    def __init__(self, variac, args):
        self.v = variac
        self.max_v = args.max
        self.hold = args.hold
        self.pause = args.pause
        self.dry_run = args.dry_run
        self.with_limit = not args.limit_off
        self.use_presets = not args.no_presets

    # --- Robuste Aufrufe -----------------------------------------------------

    def call(self, fn, *fargs, **fkwargs):
        """
        Fuehrt einen API-Aufruf aus und wiederholt ihn bei transienten Fehlern.

        Waehrend eines Firmware-/Filesystem-Updates antwortet der Controller seit
        V4.7.0 mit HTTP 503 (Bediensperre) - das ist kein Grund abzubrechen,
        sondern zu warten.
        """
        last = None
        for attempt in range(1, RETRY_MAX + 1):
            self.check_stop()
            try:
                return fn(*fargs, **fkwargs)
            except VariacError as exc:
                last = exc
                text = str(exc)
                if "503" in text:
                    log("  {}  Geraet gesperrt (Update laeuft) - warte ...".format(stamp()))
                else:
                    log("  {}  Kommunikationsfehler ({}/{}): {}".format(
                        stamp(), attempt, RETRY_MAX, text))
                self.sleep(RETRY_WAIT_S)
        raise DemoAborted("Geraet nicht erreichbar: {}".format(last))

    def status(self):
        return self.call(self.v.get_status)

    def check_stop(self):
        if _stop:
            raise DemoAborted("Abbruch durch Benutzer")

    def sleep(self, seconds):
        """Unterbrechbares Warten - reagiert sofort auf Strg+C."""
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            self.check_stop()
            time.sleep(min(0.2, max(0.0, deadline - time.monotonic())))

    # --- Sicherheitswaechter -------------------------------------------------

    def guard_temperature(self):
        """
        Haelt die Demo an, wenn es dem Geraet zu warm wird.

        Am Messestand laeuft die Vorfuehrung stundenlang; die Regelung faehrt den
        Stellmotor dabei staendig. Ueber TEMP_ABORT_C wird ganz beendet.
        """
        st = self.status()
        temp = st.get("temperature")
        if temp is None:
            return
        temp = float(temp)
        if temp >= TEMP_ABORT_C:
            raise DemoAborted("Temperatur {:.1f} C - Demo beendet".format(temp))
        if temp < TEMP_PAUSE_C:
            return

        log("\n  {}  Temperatur {:.1f} C - Demo pausiert, kuehle auf {:.0f} C ab ..."
            .format(stamp(), temp, TEMP_RESUME_C))
        self.call(self.v.set_voltage, 0)
        while True:
            self.sleep(10.0)
            temp = float(self.status().get("temperature", 0.0))
            log("      Temperatur {:.1f} C".format(temp))
            if temp <= TEMP_RESUME_C:
                log("  {}  abgekuehlt - Demo laeuft weiter\n".format(stamp()))
                return

    # --- Bausteine der Vorfuehrung -------------------------------------------

    def goto(self, target, label):
        """Faehrt eine Sollspannung an und meldet den ausgeregelten Istwert."""
        self.check_stop()
        target = min(float(target), self.max_v)
        log("  {}  {:<22s} Soll {:6.1f} V".format(stamp(), label, target))
        self.call(self.v.set_voltage, target)

        if self.dry_run:
            self.sleep(1.5)
            return

        reached, actual = self.call(self.v.wait_until_reached, target,
                                    timeout=REACH_TIMEOUT_S)
        actual = actual if actual is not None else 0.0
        marker = "" if reached else "   (Zeit ueberschritten)"
        log("             {}  Ist {:6.1f} V{}".format(
            bar(actual, self.max_v), actual, marker))

    def hold_step(self):
        """Haltezeit, damit das Publikum den Wert am Geraet ablesen kann."""
        self.sleep(self.hold)

    def phase_rampup(self):
        log("\n  -- Hochlauf in Stufen --")
        for fraction in STEP_FRACTIONS:
            self.goto(self.max_v * fraction, "Stufe {:3.0f} %".format(fraction * 100))
            self.hold_step()

    def phase_presets(self, presets):
        """Presets als Direktwahl vorfuehren - das kann die Handbedienung nicht schneller."""
        usable = [(name, value) for name, value in presets if value <= self.max_v]
        if not usable:
            log("\n  -- Presets uebersprungen (alle ueber {:.0f} V) --".format(self.max_v))
            return
        log("\n  -- Presets per Direktwahl --")
        for name, value in usable:
            self.check_stop()
            log("  {}  {:<22s} Soll {:6.1f} V".format(
                stamp(), "Preset {}".format(name.upper()), value))
            self.call(self.v._command, "recall_{}".format(name))
            if not self.dry_run:
                reached, actual = self.call(self.v.wait_until_reached, value,
                                            timeout=REACH_TIMEOUT_S)
                actual = actual if actual is not None else 0.0
                log("             {}  Ist {:6.1f} V{}".format(
                    bar(actual, self.max_v), actual,
                    "" if reached else "   (Zeit ueberschritten)"))
            self.hold_step()

    def phase_fine(self):
        """Kleine Sollwertspruenge - zeigt, wie die Regelung nachfuehrt."""
        base = self.max_v * 0.6
        log("\n  -- Feinregelung um {:.0f} V --".format(base))
        for delta in (+FINE_DELTA_V, -FINE_DELTA_V, 0.0):
            self.goto(base + delta, "Sollwert {:+.0f} V".format(delta))
            self.sleep(max(2.0, self.hold * 0.5))

    def phase_down(self):
        log("\n  -- Rueckfahrt --")
        self.goto(0, "zurueck auf 0 V")

    # --- Ablauf --------------------------------------------------------------

    def prepare(self):
        log("\n=== Vorbereitung ===")
        self.call(self.v.set_voltage, 0)
        self.sleep(0.3)
        self.call(self.v.ensure_state, "limit_on", "toggle_limit",
                  self.with_limit, "Strombegrenzung")
        self.call(self.v.ensure_state, "regulation_on", "toggle_regulation",
                  True, "Regelung")
        if self.dry_run:
            log("  Trockenlauf: Ausgang bleibt AUS.")
            self.call(self.v.ensure_state, "output_on", "toggle_output", False, "Ausgang")
        else:
            self.call(self.v.ensure_state, "output_on", "toggle_output", True, "Ausgang")

    def cycle(self, number, total, presets):
        header = "Durchlauf {}".format(number) if total == 0 \
            else "Durchlauf {}/{}".format(number, total)
        log("\n" + "=" * 60)
        log("=== {} {}===".format(header, "(Trockenlauf) " if self.dry_run else ""))
        log("=" * 60)
        self.guard_temperature()
        self.phase_rampup()
        if self.use_presets:
            self.phase_presets(presets)
        self.phase_fine()
        self.phase_down()

    def shutdown(self):
        """Sicherer Zustand - wird in jedem Fall durchlaufen."""
        log("\n=== Sicherer Zustand wird hergestellt ===")
        for step, fn in (("Sollwert 0 V", lambda: self.v.set_voltage(0)),
                         ("Ausgang AUS", lambda: self.v.ensure_state(
                             "output_on", "toggle_output", False, "Ausgang")),
                         ("Strombegrenzung EIN", lambda: self.v.ensure_state(
                             "limit_on", "toggle_limit", True, "Strombegrenzung"))):
            try:
                fn()
                time.sleep(0.3)
            except VariacError as exc:
                log("  WARNUNG: '{}' fehlgeschlagen: {}".format(step, exc))
        try:
            st = self.v.get_status()
            states = st.get("states", {})
            log("  Endzustand: {:.1f} V  Ausgang {}  Limit {}".format(
                float(st.get("voltage_actual", 0.0)),
                "EIN" if states.get("output_on") else "AUS",
                "EIN" if states.get("limit_on") else "AUS"))
        except VariacError:
            log("  Endzustand konnte nicht gelesen werden - bitte am Geraet pruefen.")


def read_presets(status):
    """Presets aus dem Status lesen (p1/p2/p3), unbekannte still ueberspringen."""
    presets = status.get("presets", {}) or {}
    result = []
    for name in ("p1", "p2", "p3"):
        value = presets.get(name)
        if value is not None:
            result.append((name, float(value)))
    return result


EPILOG = """\
Ablauf je Durchlauf:
  1. Hochlauf in Stufen   15 / 35 / 60 / 85 / 100 % der Obergrenze, je gehalten
  2. Presets              P1/P2/P3 per Direktwahl (ueber der Obergrenze: uebersprungen)
  3. Feinregelung         kleine Sollwertspruenge (+/- 8 V), zeigt das Nachfuehren
  4. Rueckfahrt           zurueck auf 0 V, danach Ruhepause

Beispiele:
  python variac_demo.py --host 192.168.0.155
      Endlos-Demo bis 200 V, Strombegrenzung ein, Beenden mit Strg+C.

  python variac_demo.py --host 192.168.0.155 --max 150 --hold 10 --pause 30
      Ruhigerer Ablauf mit niedrigerer Spannung.

  python variac_demo.py --host 192.168.0.155 --cycles 1 --dry-run
      Ein Durchlauf zur Probe, Ausgang bleibt dabei AUS.

Sicherheit:
  - Obergrenze im Skript: {hard:.0f} V; --max kann nur darunter liegen.
  - Temperaturwaechter: ab {pause:.0f} C pausieren bis {resume:.0f} C, ab {abort:.0f} C abbrechen.
  - Bei Strg+C, Fehler oder Abbruch: 0 V, Ausgang AUS, Strombegrenzung EIN.
  - Reisst die Netzwerkverbindung ab, kann das Skript nicht mehr abschalten -
    dann muss am Geraet abgeschaltet werden.

ACHTUNG: Mit --host startet die Vorfuehrung sofort und schaltet den Ausgang EIN.
""".format(hard=HARD_MAX_V, pause=TEMP_PAUSE_C, resume=TEMP_RESUME_C, abort=TEMP_ABORT_C)


def main():
    parser = argparse.ArgumentParser(
        description="Messe-Demo fuer den TWM Isolation Variac Controller: faehrt eine "
                    "Vorfuehr-Choreografie in Endlosschleife, bis Strg+C gedrueckt wird.",
        epilog=EPILOG,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default=DEFAULT_HOST,
                        help="IP oder Hostname (Standard: {})".format(DEFAULT_HOST))
    parser.add_argument("--max", type=float, default=DEFAULT_MAX_V,
                        help="Obergrenze der Demo in Volt, hoechstens {:.0f} "
                             "(Standard: {:.0f})".format(HARD_MAX_V, DEFAULT_MAX_V))
    parser.add_argument("--hold", type=float, default=DEFAULT_HOLD_S,
                        help="Haltezeit je Stufe in Sekunden (Standard: {:.0f})".format(
                            DEFAULT_HOLD_S))
    parser.add_argument("--pause", type=float, default=DEFAULT_PAUSE_S,
                        help="Ruhepause zwischen zwei Durchlaeufen in Sekunden "
                             "(Standard: {:.0f})".format(DEFAULT_PAUSE_S))
    parser.add_argument("--cycles", type=int, default=0,
                        help="Anzahl Durchlaeufe, 0 = endlos bis Strg+C (Standard: 0)")
    parser.add_argument("--limit-off", action="store_true",
                        help="ohne Strombegrenzung fahren (Standard: mit)")
    parser.add_argument("--no-presets", action="store_true",
                        help="Preset-Phase auslassen")
    parser.add_argument("--dry-run", action="store_true",
                        help="Choreografie ohne eingeschalteten Ausgang vorfuehren")
    parser.add_argument("--timeout", type=float, default=5.0,
                        help="HTTP-Timeout pro Anfrage in Sekunden (Standard: 5)")

    # Ohne Argumente nur die Hilfe zeigen: Ein versehentlicher Aufruf soll nicht
    # sofort den Ausgang einschalten - der Start verlangt bewusst mindestens --host.
    if len(sys.argv) == 1:
        parser.print_help()
        return 0

    args = parser.parse_args()

    if args.max <= 0 or args.max > HARD_MAX_V:
        parser.error("--max muss zwischen 0 und {:.0f} V liegen.".format(HARD_MAX_V))
    if args.hold < 0 or args.pause < 0:
        parser.error("--hold und --pause duerfen nicht negativ sein.")

    signal.signal(signal.SIGINT, _on_signal)
    signal.signal(signal.SIGTERM, _on_signal)

    variac = Variac(args.host, timeout=args.timeout)
    demo = Demo(variac, args)

    log("TWM Isolation Variac - Messe-Demo")
    log("Controller : {}".format(variac.base))
    log("Obergrenze : {:.0f} V   Haltezeit: {:g} s   Pause: {:g} s   Durchlaeufe: {}".format(
        args.max, args.hold, args.pause, "endlos" if args.cycles == 0 else args.cycles))
    log("Strombegr. : {}{}".format("EIN" if not args.limit_off else "AUS",
                                   "   TROCKENLAUF" if args.dry_run else ""))
    log("Beenden mit Strg+C - das Geraet wird dabei sicher abgeschaltet.")

    try:
        st = variac.get_status()
    except VariacError as exc:
        log("\nFEHLER: Keine Verbindung zum Controller: {}".format(exc))
        log("IP-Adresse (--host) und Netzwerk pruefen.")
        return 1

    if not st.get("is_hardware_ok", True):
        log("\nFEHLER: Das Geraet meldet einen Hardwarefehler - Demo nicht gestartet.")
        return 1

    presets = read_presets(st)
    if presets:
        log("Presets    : " + "  ".join("{}={:.0f} V".format(n.upper(), v)
                                        for n, v in presets))

    exit_code = 0
    try:
        demo.prepare()
        number = 0
        while True:
            number += 1
            demo.cycle(number, args.cycles, presets)
            if args.cycles and number >= args.cycles:
                break
            log("\n  {}  Pause {:g} s bis zum naechsten Durchlauf ...".format(
                stamp(), demo.pause))
            demo.sleep(demo.pause)
    except DemoAborted as exc:
        log("\n{}.".format(exc))
        exit_code = 130 if "Benutzer" in str(exc) else 1
    except VariacError as exc:
        log("\nFEHLER: {}".format(exc))
        exit_code = 1
    finally:
        demo.shutdown()

    log("\nFertig.")
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
