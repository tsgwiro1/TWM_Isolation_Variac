# Variac Spannungs-Sequenz – PC-Tools

Skripte zum automatisierten Anfahren einer festen Spannungsreihe am **TWM Isolation
Variac Controller** über dessen REST-API. Es gibt zwei gleichwertige Varianten:

| Datei | Plattform | Voraussetzung |
|-------|-----------|---------------|
| [`variac_sequence.py`](variac_sequence.py) | plattformübergreifend | Python 3 (nur Standardbibliothek) |
| [`variac_sequence.ps1`](variac_sequence.ps1) | Windows | PowerShell (Bordmittel, keine Installation) |

Beide Skripte verhalten sich identisch und benötigen keine zusätzlichen Pakete.

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

## Verwendung

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

## Beispiel-Sitzung

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
