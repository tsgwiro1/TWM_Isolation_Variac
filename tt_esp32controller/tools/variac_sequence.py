#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Spannungs-Sequenz fuer den TWM Isolation Variac Controller.

Faehrt eine feste Reihe von Soll-Spannungen an (20, 50, 75, 100, 150, 200, 230 V).
Die Weiterschaltung erfolgt wahlweise automatisch (Zeit in Sekunden) oder manuell
per Enter-Taste.

Ablauf:
  1. Spannung auf 0 V setzen
  2. Strombegrenzung ein- oder ausschalten (Abfrage, Standard: ein)
  3. Spannungsregelung einschalten
  4. Ausgang einschalten
  5. Spannungsschritte nacheinander anfahren (je Schritt wird gewartet,
     bis die Ist-Spannung den Sollwert erreicht hat)
  6. Nach 230 V: Abfrage, ob die Strombegrenzung ausgeschaltet werden soll
  7. Abschlussabfrage: Ausgang aus + Spannung zurueck auf 0 V

Verwendet ausschliesslich die Python-Standardbibliothek (kein 'pip install' noetig).

Beispiel:
    python variac_sequence.py --host 192.168.0.116
"""

import argparse
import json
import sys
import time
import urllib.parse
import urllib.request

# Anzufahrende Soll-Spannungen in Volt (in Reihenfolge).
VOLTAGE_STEPS = [20, 50, 75, 100, 150, 200, 230]

DEFAULT_HOST = "192.168.0.116"
HTTP_TIMEOUT = 5.0  # Sekunden pro Anfrage

# "Spannung erreicht"-Kriterium (GitHub-#6): Die Regelung braucht je nach
# Sprunghoehe mehrere Sekunden - deshalb pollen statt fixer Pause.
REACH_TOLERANCE_V = 2.0   # |Ist - Soll| <= Toleranz gilt als erreicht
REACH_TIMEOUT_S = 30.0    # danach mit Warnung weitermachen
REACH_POLL_S = 0.5        # Abfrageintervall


class VariacError(Exception):
    """Fehler bei der Kommunikation mit dem Variac-Controller."""


class Variac:
    """Duenner REST-Client fuer den Isolation-Variac-Controller."""

    def __init__(self, host, timeout=HTTP_TIMEOUT):
        # Falls der Nutzer schon "http://" mitgibt, nicht doppelt voranstellen.
        if host.startswith("http://") or host.startswith("https://"):
            self.base = host.rstrip("/")
        else:
            self.base = "http://" + host.rstrip("/")
        self.timeout = timeout

    def _request(self, path, params=None, method="GET"):
        url = self.base + "/api" + path
        if params:
            url += "?" + urllib.parse.urlencode(params)
        try:
            req = urllib.request.Request(url, method=method)
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                data = resp.read().decode("utf-8", errors="replace")
        except Exception as exc:  # urllib wirft je nach Fehler verschiedene Typen
            raise VariacError("Anfrage fehlgeschlagen ({}): {}".format(url, exc))
        return data

    def _get(self, path, params=None):
        return self._request(path, params, method="GET")

    def _post(self, path, params=None):
        # Ab Controller-FW V4.0.0 sind zustandsaendernde Aufrufe POST
        # (Parameter weiterhin im Query-String).
        return self._request(path, params, method="POST")

    def get_status(self):
        """Liest den Gesamtzustand des Geraets als dict."""
        raw = self._get("/status")
        try:
            return json.loads(raw)
        except json.JSONDecodeError as exc:
            raise VariacError("Status-Antwort ist kein gueltiges JSON: {}".format(exc))

    def set_voltage(self, voltage):
        """Setzt den Soll-Spannungswert."""
        self._post("/setpoint", {"voltage": voltage})

    def wait_until_reached(self, target, tolerance=REACH_TOLERANCE_V,
                           timeout=REACH_TIMEOUT_S, poll=REACH_POLL_S):
        """
        Wartet, bis die Ist-Spannung den Sollwert erreicht hat (GitHub-#6).

        Pollt den Status, bis |Ist - Soll| <= tolerance oder timeout (Sekunden)
        abgelaufen ist. Rueckgabe: (erreicht, letzte Ist-Spannung oder None).
        """
        deadline = time.monotonic() + timeout
        actual = None
        while time.monotonic() < deadline:
            time.sleep(poll)
            actual = self.get_status().get("voltage_actual")
            if actual is None:
                continue
            if abs(float(actual) - float(target)) <= tolerance:
                return True, actual
        return False, actual

    def _command(self, action):
        self._post("/command", {"action": action})

    def _state(self, key):
        """Liefert den aktuellen Bool-Zustand aus dem 'states'-Objekt."""
        states = self.get_status().get("states", {})
        if key not in states:
            raise VariacError("Statusfeld '{}' nicht vorhanden.".format(key))
        return bool(states[key])

    def ensure_state(self, key, action, desired, label):
        """
        Stellt sicher, dass states[key] == desired ist.

        Die /command-Aktionen sind Umschalter (Toggle), daher wird zuerst der
        Ist-Zustand gelesen und nur bei Bedarf umgeschaltet.
        """
        current = self._state(key)
        if current == desired:
            print("  {} ist bereits {}.".format(label, "EIN" if desired else "AUS"))
            return
        self._command(action)
        time.sleep(0.3)  # kurze Pause, damit das Geraet den Zustand uebernimmt
        new = self._state(key)
        if new != desired:
            raise VariacError(
                "{} konnte nicht auf {} gesetzt werden (ist weiterhin {}).".format(
                    label, "EIN" if desired else "AUS", "EIN" if new else "AUS"
                )
            )
        print("  {} -> {}.".format(label, "EIN" if desired else "AUS"))


def ask_yes_no(question, default=False):
    """Ja/Nein-Abfrage. Leere Eingabe = default."""
    suffix = " [J/n] " if default else " [j/N] "
    while True:
        ans = input(question + suffix).strip().lower()
        if ans == "":
            return default
        if ans in ("j", "ja", "y", "yes"):
            return True
        if ans in ("n", "nein", "no"):
            return False
        print("Bitte 'j' oder 'n' eingeben.")


def ask_mode():
    """Fragt ab, ob automatisch oder manuell weitergeschaltet wird."""
    while True:
        ans = input(
            "Anfahrt der Spannungen automatisch oder per Enter?\n"
            "  [a] automatisch (Zeitintervall)\n"
            "  [e] manuell per Enter-Taste\n"
            "Auswahl [a/e]: "
        ).strip().lower()
        if ans in ("a", "auto", "automatisch"):
            return "auto"
        if ans in ("e", "enter", "m", "manuell"):
            return "manual"
        print("Bitte 'a' oder 'e' eingeben.")


def ask_interval():
    """Fragt die Wartezeit (Sekunden) bis zur naechsten Spannung ab."""
    while True:
        ans = input("Zeit bis zur naechsten Spannung in Sekunden: ").strip().replace(",", ".")
        try:
            value = float(ans)
        except ValueError:
            print("Bitte eine Zahl eingeben (z.B. 10 oder 5.5).")
            continue
        if value < 0:
            print("Bitte einen Wert >= 0 eingeben.")
            continue
        return value


def show_status(variac, prefix="Aktueller Status:"):
    """Gibt die wichtigsten Statuswerte aus."""
    st = variac.get_status()
    states = st.get("states", {})
    print(prefix)
    print("  Spannung Ist/Soll : {} V / {} V".format(
        st.get("voltage_actual", "?"), st.get("voltage_setpoint", "?")))
    print("  Ausgang           : {}".format("EIN" if states.get("output_on") else "AUS"))
    print("  Strombegrenzung   : {}".format("EIN" if states.get("limit_on") else "AUS"))
    print("  Regelung          : {}".format("EIN" if states.get("regulation_on") else "AUS"))


def wait_step(mode, interval, next_label):
    """Wartet je nach Modus auf Enter oder eine feste Zeit."""
    if mode == "manual":
        input("    >> Enter druecken fuer {} ...".format(next_label))
    else:
        print("    >> warte {:g} s bis {} ...".format(interval, next_label))
        time.sleep(interval)


def safe_shutdown(variac):
    """Bringt das Geraet in einen sicheren Zustand: 0 V und Ausgang aus."""
    print("\nSicherer Zustand wird hergestellt (0 V, Ausgang AUS) ...")
    try:
        variac.set_voltage(0)
        time.sleep(0.3)
        variac.ensure_state("output_on", "toggle_output", False, "Ausgang")
    except VariacError as exc:
        print("  WARNUNG: Sicheres Abschalten fehlgeschlagen: {}".format(exc))


def run_sequence(variac, mode, interval, with_limit=True):
    # --- Vorbereitung: erst 0 V, dann Strombegrenzung, dann Ausgang ---
    print("\n--- Vorbereitung ---")
    print("Spannung auf 0 V setzen ...")
    variac.set_voltage(0)
    time.sleep(0.3)

    # GitHub-#7: Sequenz wahlweise mit oder ohne Strombegrenzung fahren.
    print("Strombegrenzung {} ...".format("aktivieren" if with_limit else "ausschalten"))
    variac.ensure_state("limit_on", "toggle_limit", with_limit, "Strombegrenzung")

    print("Spannungsregelung einschalten ...")
    variac.ensure_state("regulation_on", "toggle_regulation", True, "Regelung")

    print("Ausgang einschalten ...")
    variac.ensure_state("output_on", "toggle_output", True, "Ausgang")

    # --- Spannungsschritte ---
    print("\n--- Spannungs-Sequenz ---")
    for i, voltage in enumerate(VOLTAGE_STEPS):
        print("\n[{}/{}] Soll-Spannung {} V".format(i + 1, len(VOLTAGE_STEPS), voltage))
        variac.set_voltage(voltage)
        # GitHub-#6: warten, bis die Regelung den Sollwert erreicht hat,
        # statt nach fixer Pause einen zu fruehen Messwert auszugeben.
        reached, actual = variac.wait_until_reached(voltage)
        if reached:
            print("    Ist-Spannung: {} V".format(actual))
        else:
            print("    WARNUNG: {} V nach {:g} s nicht erreicht (Ist: {} V)".format(
                voltage, REACH_TIMEOUT_S, actual if actual is not None else "?"))

        # Vor dem naechsten Schritt warten (nach dem letzten Schritt nicht).
        if i < len(VOLTAGE_STEPS) - 1:
            next_label = "{} V".format(VOLTAGE_STEPS[i + 1])
            wait_step(mode, interval, next_label)

    # --- Nach 230 V: Strombegrenzung abschalten? ---
    print("\n--- {} V erreicht ---".format(VOLTAGE_STEPS[-1]))
    if ask_yes_no("Strombegrenzung jetzt ausschalten?", default=False):
        variac.ensure_state("limit_on", "toggle_limit", False, "Strombegrenzung")
        show_status(variac, prefix="\nStatus nach Abschalten der Strombegrenzung:")

    # --- Abschluss ---
    print()
    input("Test abgeschlossen? Enter druecken, um Ausgang auszuschalten und "
          "die Spannung auf 0 V zu stellen ...")
    safe_shutdown(variac)
    show_status(variac, prefix="\nEndzustand:")


def main():
    parser = argparse.ArgumentParser(
        description="Spannungs-Sequenz fuer den TWM Isolation Variac Controller.")
    parser.add_argument("--host", default=DEFAULT_HOST,
                        help="IP-Adresse oder Hostname des Controllers "
                             "(Standard: {})".format(DEFAULT_HOST))
    parser.add_argument("--timeout", type=float, default=HTTP_TIMEOUT,
                        help="HTTP-Timeout pro Anfrage in Sekunden "
                             "(Standard: {})".format(HTTP_TIMEOUT))
    args = parser.parse_args()

    variac = Variac(args.host, timeout=args.timeout)

    print("TWM Isolation Variac - Spannungs-Sequenz")
    print("Controller: {}".format(variac.base))

    # Verbindung pruefen.
    try:
        show_status(variac, prefix="\nVerbindung OK. Aktueller Status:")
    except VariacError as exc:
        print("\nFEHLER: Keine Verbindung zum Controller: {}".format(exc))
        print("Pruefe IP-Adresse (--host) und Netzwerkverbindung.")
        return 1

    print("\nSchrittfolge: " + " -> ".join("{} V".format(v) for v in VOLTAGE_STEPS))
    if not ask_yes_no("\nSequenz jetzt starten?", default=True):
        print("Abgebrochen.")
        return 0

    mode = ask_mode()
    interval = ask_interval() if mode == "auto" else 0.0
    # GitHub-#7: Strombegrenzung fuer die Sequenz waehlbar (sicherer Default: mit).
    with_limit = ask_yes_no("Sequenz mit Strombegrenzung fahren?", default=True)

    try:
        run_sequence(variac, mode, interval, with_limit)
    except KeyboardInterrupt:
        print("\n\nAbbruch durch Benutzer (Strg+C).")
        safe_shutdown(variac)
        return 130
    except VariacError as exc:
        print("\nFEHLER: {}".format(exc))
        safe_shutdown(variac)
        return 1

    print("\nFertig.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
