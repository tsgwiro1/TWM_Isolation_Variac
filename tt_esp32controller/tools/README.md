# Variac Spannungs-Sequenz – PC-Tools

Werkzeuge zum automatisierten Anfahren einer Spannungsreihe am **TWM Isolation
Variac Controller** über dessen REST-API. Es gibt drei Bedienwege:

**A) Kommandozeile, interaktiv** – feste Reihe (20/50/75/100/150/200/230 V), Abfragen im Dialog:

| Datei | Plattform | Voraussetzung |
|-------|-----------|---------------|
| [`variac_sequence.py`](variac_sequence.py) | plattformübergreifend | Python 3 (nur Standardbibliothek) |
| [`variac_sequence.ps1`](variac_sequence.ps1) | Windows | PowerShell (Bordmittel, keine Installation) |

**B) Lokale Weboberfläche** – frei einstellbare Werte (bis zu 10 Spannungen), Live-Ausgabe:

| Datei | Rolle |
|-------|-------|
| [`variac_server.py`](variac_server.py) | Lokaler Webserver, der die Seite ausliefert und das Lauf-Skript startet |
| [`index.html`](index.html) | Bedienoberfläche (Vanilla-JavaScript) |

**C) Kommandozeile, parametriert** – alle Werte als CLI-Parameter, nicht-interaktiv
(wird auch von der Weboberfläche genutzt):

| Datei | Plattform | Voraussetzung |
|-------|-----------|---------------|
| [`variac_run.py`](variac_run.py) | plattformübergreifend | Python 3 (nur Standardbibliothek) |

Alle Werkzeuge benötigen nur Python 3 (Standardbibliothek) bzw. PowerShell – keine
zusätzlichen Pakete.

---

## Was die Skripte tun

Sie fahren nacheinander folgende Soll-Spannungen an:

```
20 V → 50 V → 75 V → 100 V → 150 V → 200 V → 230 V
```

Ablauf:

1. **Vorbereitung** (in dieser Reihenfolge):
   1. Spannung auf **0 V** setzen
   2. **Strombegrenzung** einschalten
   3. **Spannungsregelung** einschalten
   4. **Ausgang** einschalten
2. **Spannungsschritte** nacheinander anfahren. Die Weiterschaltung erfolgt wahlweise:
   - **automatisch** – mit einem einstellbaren Zeitintervall (Sekunden), oder
   - **manuell** – durch Drücken der **Enter**-Taste.
3. Nach dem letzten Schritt (**230 V**): Abfrage, ob die **Strombegrenzung
   ausgeschaltet** werden soll.
4. **Abschluss**: Auf Enter wird der **Ausgang ausgeschaltet** und die Spannung
   wieder auf **0 V** gestellt.

### Sicherheit

- Die Umschaltbefehle der API sind **Toggles** (Umschalter). Die Skripte lesen
  deshalb zuerst den Ist-Zustand und schalten nur dann um, wenn er vom Ziel abweicht
  – anschließend wird der neue Zustand verifiziert. So wird der Ausgang nie
  versehentlich aus- statt eingeschaltet.
- Bei **Abbruch** (Strg+C) oder einem **Fehler** wird automatisch ein sicherer
  Zustand hergestellt: Spannung **0 V** und Ausgang **AUS**.

---

## Voraussetzungen

- Der Controller ist im Netzwerk erreichbar (gleiches LAN/WLAN).
- Die IP-Adresse des Controllers ist bekannt (Standardannahme: `192.168.0.116`).
- **Python-Variante:** Python 3 installiert (`python --version`).
- **PowerShell-Variante:** Windows mit PowerShell (vorinstalliert).

---

## A) Kommandozeile (interaktiv)

Feste Spannungsreihe, alle Entscheidungen werden im Dialog abgefragt.

### Python

```bash
python variac_sequence.py --host 192.168.0.116
```

Optionen:

| Option | Beschreibung | Standard |
|--------|--------------|----------|
| `--host` | IP-Adresse oder Hostname des Controllers | `192.168.0.116` |
| `--timeout` | HTTP-Timeout pro Anfrage in Sekunden | `5.0` |

### PowerShell

```powershell
.\variac_sequence.ps1 -Address 192.168.0.116
```

Optionen:

| Option | Beschreibung | Standard |
|--------|--------------|----------|
| `-Address` | IP-Adresse oder Hostname des Controllers | `192.168.0.116` |
| `-TimeoutSec` | HTTP-Timeout pro Anfrage in Sekunden | `5` |

Falls die Ausführung wegen der **Execution Policy** blockiert wird:

```powershell
powershell -ExecutionPolicy Bypass -File .\variac_sequence.ps1 -Address 192.168.0.116
```

---

## B) Weboberfläche (lokal)

Komfortablere Variante mit Eingabemaske im Browser. Eine reine HTML-Seite darf aus
Sicherheitsgründen kein lokales Programm starten – deshalb läuft ein **kleiner lokaler
Python-Webserver** ([`variac_server.py`](variac_server.py)), der die Seite ausliefert
und das parametrierte Skript [`variac_run.py`](variac_run.py) (siehe Abschnitt C) mit
den eingegebenen Werten ausführt.

### Starten

```bash
python variac_server.py
```

Der Browser öffnet automatisch `http://127.0.0.1:8765`. Optionen:
`--port <n>`, `--no-browser`, `--host <bind>` (Standard: nur lokal `127.0.0.1`).

### Bedienung

- **Verbindung**: Controller-IP eingeben, mit *Status abfragen* prüfen.
- **Ablauf**: Modus *automatisch* (Intervall in Sekunden) oder *manuell* (Weiterschalten
  per Knopf). Optionen: Strombegrenzung nach letzter Spannung ausschalten; am Ende
  Ausgang ausschalten + 0 V.
- **Soll-Spannungen**: Anzahl der Felder einstellen (**1–10**), *Felder anwenden*, Werte
  eintragen (die ersten Felder sind mit der Standardreihe vorbelegt).
- **Steuerung**: *Sequenz starten* führt das Skript aus, die Live-Ausgabe erscheint
  unten. Im manuellen Modus wird *Weiter ▶* aktiv, sobald das Skript wartet.
  *Not-Aus* schaltet jederzeit den Ausgang aus und stellt 0 V ein.

---

## C) Parametriertes Skript (`variac_run.py`)

Nicht-interaktiver Lauf: **alle** Einstellungen und Spannungen werden als
Kommandozeilen-Parameter übergeben. Eignet sich für Automatisierung/Skripting und ist
zugleich das Skript, das die Weboberfläche (Abschnitt B) im Hintergrund startet.

```bash
python variac_run.py --host 192.168.0.116 --mode auto --interval 10 \
    --voltages 20 50 75 100 150 200 230 --limit-off-after-last --shutdown
```

| Option | Beschreibung | Standard |
|--------|--------------|----------|
| `--host` | IP-Adresse/Hostname des Controllers | `192.168.0.116` |
| `--timeout` | HTTP-Timeout pro Anfrage (s) | `5.0` |
| `--voltages` | Liste der Soll-Spannungen (max. 10) | – (erforderlich) |
| `--mode` | `auto` (Zeitintervall) oder `manual` (auf Eingabe warten) | `auto` |
| `--interval` | Wartezeit zwischen Schritten in Sekunden (nur `auto`) | `5.0` |
| `--limit-off-after-last` | Strombegrenzung nach letzter Spannung ausschalten | aus |
| `--shutdown` | Am Ende Ausgang ausschalten und auf 0 V | aus |

Im Modus `manual` wartet das Skript vor jedem weiteren Schritt auf eine Zeile von
*stdin* (Terminal = Enter, Web = Button *Weiter*). **Nach der letzten Spannung wird in
beiden Modi gewartet**, bis *Weiter* bestätigt wird – erst danach erfolgt der Abschluss
(Strombegrenzung ausschalten / Ausgang aus + 0 V, je nach Einstellung).

> Hinweis: Bei der Weboberfläche (Abschnitt B) müssen diese Parameter **nicht** von Hand
> angegeben werden – sie werden aus den Formularfeldern erzeugt.

---

## Beispiel-Sitzung (Abschnitt A)

```
TWM Isolation Variac - Spannungs-Sequenz
Controller: http://192.168.0.116

Verbindung OK. Aktueller Status:
  Spannung Ist/Soll : 0 V / 0 V
  Ausgang           : AUS
  Strombegrenzung   : AUS
  Regelung          : AUS

Schrittfolge: 20 V -> 50 V -> 75 V -> 100 V -> 150 V -> 200 V -> 230 V

Sequenz jetzt starten? [J/n]
Anfahrt der Spannungen automatisch oder per Enter?
  [a] automatisch (Zeitintervall)
  [e] manuell per Enter-Taste
Auswahl [a/e]: a
Zeit bis zur naechsten Spannung in Sekunden: 10

--- Vorbereitung ---
Spannung auf 0 V setzen ...
Strombegrenzung aktivieren ...
  Strombegrenzung -> EIN.
Spannungsregelung einschalten ...
  Regelung -> EIN.
Ausgang einschalten ...
  Ausgang -> EIN.

--- Spannungs-Sequenz ---

[1/7] Soll-Spannung 20 V
    Ist-Spannung: 19.8 V
    >> warte 10 s bis 50 V ...
...
```

---

## Verwendete API-Endpunkte

Die Skripte nutzen die REST-API des Controllers (siehe
[`../documentation/REST API Dokumentation_IsolationVariac.docx`](../documentation/)):

| Zweck | Endpunkt |
|-------|----------|
| Zustand lesen | `GET /api/status` |
| Soll-Spannung setzen | `GET /api/setpoint?voltage=<V>` |
| Ausgang umschalten | `GET /api/command?action=toggle_output` |
| Strombegrenzung umschalten | `GET /api/command?action=toggle_limit` |
| Spannungsregelung umschalten | `GET /api/command?action=toggle_regulation` |
