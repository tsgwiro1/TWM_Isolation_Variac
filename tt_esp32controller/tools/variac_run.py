#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Parametrierter, nicht-interaktiver Lauf der Spannungs-Sequenz.

Alle Einstellungen werden ueber Kommandozeilen-Parameter uebergeben. Dieses Skript
wird sowohl direkt im Terminal als auch vom lokalen Webserver (variac_server.py)
gestartet.

Ablauf (wie variac_sequence.py, aber ohne interaktive Modus-/Wertabfragen):
  1. 0 V -> Strombegrenzung EIN (bzw. AUS mit --limit-off) -> Regelung EIN -> Ausgang EIN
  2. Spannungsschritte anfahren (auto = Zeitintervall, manual = auf Eingabe warten);
     je Schritt wird gewartet, bis die Ist-Spannung den Sollwert erreicht hat
  3. optional Strombegrenzung nach letzter Spannung ausschalten
  4. optional Ausgang aus + 0 V

Im Modus "manual" wartet das Skript vor jedem weiteren Schritt auf eine Zeile von
stdin (im Terminal = Enter-Taste, im Web = Button "Weiter"). Es gibt vorher eine
Markerzeile "[WAIT] <Beschriftung>" aus, damit die Weboberflaeche den Button
freischalten kann.

Beispiel:
    python variac_run.py --host 192.168.0.116 --mode auto --interval 10 \
        --voltages 20 50 75 100 150 200 230 --limit-off-after-last --shutdown
"""

import argparse
import os
import sys
import time

# Damit der Import auch funktioniert, wenn das Skript aus einem anderen
# Arbeitsverzeichnis gestartet wird.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from variac_sequence import Variac, VariacError, REACH_TIMEOUT_S  # noqa: E402

MAX_STEPS = 10


def log(msg=""):
    """Gibt eine Zeile aus und leert den Puffer sofort (fuer Live-Streaming)."""
    print(msg, flush=True)


def wait_for_input(label):
    """
    Wartet im manuellen Modus auf eine Zeile von stdin.

    Gibt zuerst einen maschinenlesbaren Marker aus. EOF (geschlossenes stdin)
    wird wie ein Bestaetigen behandelt, damit der Lauf nicht haengen bleibt.
    """
    log("[WAIT] {}".format(label))
    line = sys.stdin.readline()
    if line == "":
        log("  (stdin geschlossen - fahre fort)")


def show_states(variac, prefix):
    st = variac.get_status()
    states = st.get("states", {})

    def onoff(b):
        return "EIN" if b else "AUS"

    log(prefix)
    log("  Spannung Ist/Soll : {} V / {} V".format(
        st.get("voltage_actual", "?"), st.get("voltage_setpoint", "?")))
    log("  Ausgang           : {}".format(onoff(states.get("output_on"))))
    log("  Strombegrenzung   : {}".format(onoff(states.get("limit_on"))))
    log("  Regelung          : {}".format(onoff(states.get("regulation_on"))))


def safe_shutdown(variac):
    log("")
    log("Sicherer Zustand wird hergestellt (0 V, Ausgang AUS) ...")
    try:
        variac.set_voltage(0)
        time.sleep(0.3)
        variac.ensure_state("output_on", "toggle_output", False, "Ausgang")
    except VariacError as exc:
        log("  WARNUNG: Sicheres Abschalten fehlgeschlagen: {}".format(exc))


def run(args):
    variac = Variac(args.host, timeout=args.timeout)

    log("TWM Isolation Variac - Spannungs-Sequenz (parametriert)")
    log("Controller: {}".format(variac.base))
    log("Modus: {}{}".format(
        args.mode,
        "  Intervall: {:g} s".format(args.interval) if args.mode == "auto" else ""))
    log("Schrittfolge: " + " -> ".join("{:g} V".format(v) for v in args.voltages))

    # Verbindung pruefen.
    show_states(variac, "\nVerbindung OK. Aktueller Status:")

    # --- Vorbereitung: 0 V -> Strombegrenzung -> Regelung -> Ausgang ---
    log("\n--- Vorbereitung ---")
    log("Spannung auf 0 V setzen ...")
    variac.set_voltage(0)
    time.sleep(0.3)

    # GitHub-#7: Sequenz wahlweise mit oder ohne Strombegrenzung fahren.
    with_limit = not args.limit_off
    log("Strombegrenzung {} ...".format("aktivieren" if with_limit else "ausschalten"))
    variac.ensure_state("limit_on", "toggle_limit", with_limit, "Strombegrenzung")

    log("Spannungsregelung einschalten ...")
    variac.ensure_state("regulation_on", "toggle_regulation", True, "Regelung")

    log("Ausgang einschalten ...")
    variac.ensure_state("output_on", "toggle_output", True, "Ausgang")

    # --- Spannungsschritte ---
    log("\n--- Spannungs-Sequenz ---")
    n = len(args.voltages)
    for i, voltage in enumerate(args.voltages):
        log("\n[{}/{}] Soll-Spannung {:g} V".format(i + 1, n, voltage))
        variac.set_voltage(voltage)
        # GitHub-#6: warten, bis die Regelung den Sollwert erreicht hat,
        # statt nach fixer Pause einen zu fruehen Messwert auszugeben.
        reached, actual = variac.wait_until_reached(voltage)
        if reached:
            log("    Ist-Spannung: {} V".format(actual))
        else:
            log("    WARNUNG: {:g} V nach {:g} s nicht erreicht (Ist: {} V)".format(
                voltage, REACH_TIMEOUT_S, actual if actual is not None else "?"))

        if i < n - 1:
            next_label = "{:g} V".format(args.voltages[i + 1])
            if args.mode == "manual":
                wait_for_input(next_label)
            else:
                log("    >> warte {:g} s bis {} ...".format(args.interval, next_label))
                time.sleep(args.interval)

    # --- Letzte Spannung erreicht: in beiden Modi auf Bestaetigung warten ---
    log("\n--- {:g} V erreicht ---".format(args.voltages[-1]))
    wait_for_input("Ablauf abschliessen (Strombegrenzung/Ausschalten wie eingestellt)")

    # --- Abschluss wie eingestellt ---
    if args.limit_off_after_last:
        log("Strombegrenzung ausschalten ...")
        variac.ensure_state("limit_on", "toggle_limit", False, "Strombegrenzung")
    else:
        log("Strombegrenzung bleibt {}.".format("ausgeschaltet" if args.limit_off else "eingeschaltet"))

    if args.shutdown:
        safe_shutdown(variac)
        show_states(variac, "\nEndzustand:")
    else:
        log("\nAusgang bleibt eingeschaltet (kein Abschalten angefordert).")

    log("\nFertig.")


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description="Parametrierter Lauf der Variac-Spannungs-Sequenz.")
    parser.add_argument("--host", default="192.168.0.116",
                        help="IP-Adresse/Hostname des Controllers.")
    parser.add_argument("--timeout", type=float, default=5.0,
                        help="HTTP-Timeout pro Anfrage in Sekunden.")
    parser.add_argument("--voltages", type=float, nargs="+", required=True,
                        help="Liste der Soll-Spannungen in Volt (max. {}).".format(MAX_STEPS))
    parser.add_argument("--mode", choices=["auto", "manual"], default="auto",
                        help="auto = Zeitintervall, manual = auf Eingabe warten.")
    parser.add_argument("--interval", type=float, default=5.0,
                        help="Wartezeit zwischen Schritten in Sekunden (nur auto).")
    parser.add_argument("--limit-off", action="store_true",
                        help="Sequenz ohne Strombegrenzung fahren "
                             "(Begrenzung wird beim Start ausgeschaltet).")
    parser.add_argument("--limit-off-after-last", action="store_true",
                        help="Strombegrenzung nach der letzten Spannung ausschalten.")
    parser.add_argument("--shutdown", action="store_true",
                        help="Am Ende Ausgang ausschalten und Spannung auf 0 V.")
    args = parser.parse_args(argv)

    if not args.voltages:
        parser.error("Mindestens eine Spannung angeben.")
    if len(args.voltages) > MAX_STEPS:
        parser.error("Maximal {} Spannungen erlaubt.".format(MAX_STEPS))
    if args.interval < 0:
        parser.error("Intervall muss >= 0 sein.")
    return args


def main(argv=None):
    args = parse_args(argv)
    variac = Variac(args.host, timeout=args.timeout)
    try:
        run(args)
    except KeyboardInterrupt:
        log("\n\nAbbruch (KeyboardInterrupt).")
        safe_shutdown(variac)
        return 130
    except VariacError as exc:
        log("\nFEHLER: {}".format(exc))
        safe_shutdown(variac)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
