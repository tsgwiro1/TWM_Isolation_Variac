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

**D) Messe-Demo** – unbeaufsichtigte Vorführung in Endlosschleife:

| Datei | Plattform | Voraussetzung |
|-------|-----------|---------------|
| [`variac_demo.py`](variac_demo.py) | plattformübergreifend | Python 3 (nur Standardbibliothek) |

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
   2. **Strombegrenzung** ein- oder ausschalten (wählbar, Standard: **ein**)
   3. **Spannungsregelung** einschalten
   4. **Ausgang** einschalten
2. **Spannungsschritte** nacheinander anfahren. Je Schritt wird gewartet, bis die
   Ist-Spannung den Sollwert erreicht hat **und stabil ist** (±2 V, Änderung ≤ 0,5 V
   zwischen zwei Messungen, max. 30 s) – ausgegeben wird also der ausgeregelte Endwert.
   Die Weiterschaltung erfolgt wahlweise:
   - **automatisch** – mit einem einstellbaren Zeitintervall (Sekunden) **ab Erreichen
     der Spannung**, oder
   - **manuell** – durch Drücken der **Enter**-Taste.
3. Nach dem letzten Schritt (**230 V**): Abfrage, ob die **Strombegrenzung
   ausgeschaltet** werden soll (entfällt, wenn sie bereits aus ist).
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
  per Knopf). Optionen: Sequenz mit Strombegrenzung fahren (Standard: ein);
  Strombegrenzung nach letzter Spannung ausschalten; am Ende Ausgang ausschalten + 0 V.
- **Soll-Spannungen**: Anzahl der Felder einstellen (**1–10**), *Felder anwenden*, Werte
  eintragen (die ersten Felder sind mit der Standardreihe vorbelegt).
- **Steuerung**: *Sequenz starten* führt das Skript aus, die Live-Ausgabe erscheint
  unten. Im manuellen Modus wird *Weiter ▶* aktiv, sobald das Skript wartet.
  *Not-Aus* schaltet **sofort und ohne Rückfrage** den Ausgang aus und stellt 0 V ein.

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
| `--limit-off` | Sequenz ohne Strombegrenzung fahren (Begrenzung beim Start ausschalten) | aus |
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
Sequenz mit Strombegrenzung fahren? [J/n]

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

## Messe-Demo (`variac_demo.py`)

Vorführung für den Messestand: läuft **ohne Rückfragen in Endlosschleife**, bis
**Strg+C** gedrückt wird. Jeder Durchlauf besteht aus vier Phasen:

1. **Hochlauf in Stufen** – 15 %, 35 %, 60 %, 85 %, 100 % der gewählten Obergrenze,
   jede Stufe wird gehalten, damit das Publikum die Anzeige ablesen kann.
2. **Presets per Direktwahl** – P1/P2/P3 werden über `recall_p*` angesprungen.
   Presets **oberhalb der Obergrenze werden übersprungen**.
3. **Feinregelung** – kleine Sollwertsprünge (±8 V) um 60 % der Obergrenze; zeigt,
   wie die Regelung nachführt.
4. **Rückfahrt** auf 0 V, danach eine Ruhepause bis zum nächsten Durchlauf.

```bash
python variac_demo.py --host 192.168.0.155
```

| Parameter | Bedeutung | Standard |
|-----------|-----------|----------|
| `--host` | IP oder Hostname des Controllers | `192.168.0.155` |
| `--max` | Obergrenze der Demo in Volt (Skript lässt höchstens 230 zu) | `200` |
| `--hold` | Haltezeit je Stufe in Sekunden | `6` |
| `--pause` | Ruhepause zwischen zwei Durchläufen | `15` |
| `--cycles` | Anzahl Durchläufe, `0` = endlos | `0` |
| `--limit-off` | ohne Strombegrenzung fahren | aus (Limit **ein**) |
| `--no-presets` | Preset-Phase auslassen | aus |
| `--dry-run` | komplette Choreografie **ohne eingeschalteten Ausgang** | aus |

Beispiel für einen ruhigeren Ablauf mit niedrigerer Spannung:

```bash
python variac_demo.py --host 192.168.0.155 --max 150 --hold 10 --pause 30
```

### Sicherheit

- **Harte Obergrenze** im Skript: 230 V. `--max` kann nur darunter liegen.
- **Temperaturwächter**: Ab 50 °C geht der Sollwert auf 0 und die Demo wartet, bis
  das Gerät auf 45 °C abgekühlt ist; ab 58 °C bricht sie ganz ab. Gedacht für den
  Dauerbetrieb über einen Messetag.
- **Sicherer Zustand in jeder Lage**: Bei Strg+C, Fehler oder Abbruch werden Sollwert
  auf 0 V, Ausgang **AUS** und Strombegrenzung **EIN** gesetzt – auch wenn der Abbruch
  mitten in einer Anfahrt kommt.
- **Bediensperre respektiert**: Läuft am Gerät gerade ein Update (HTTP 503 seit
  V4.7.0), wartet die Demo, statt Befehle ins Leere zu schicken.
- **Trockenlauf**: `--dry-run` führt den ganzen Ablauf ohne eingeschalteten Ausgang
  vor – gut zum Prüfen der Choreografie vor dem Publikum.
- **Grenze der Fernabschaltung**: Reißt die Netzwerkverbindung ab, kann das Skript
  nichts mehr schalten. Es meldet das deutlich, aber **das Abschalten muss dann am
  Gerät erfolgen** – der Variac bleibt bis dahin unter Spannung.

---

## Verwendete API-Endpunkte

Die Skripte nutzen die REST-API des Controllers **ab Firmware V4.0.0** (interaktive Doku:
`http://<controller>/doc_api.html`, Spec: `openapi.yaml` in `../data/`). Für ältere
Firmware (< V4.0.0, Aktionen noch per GET) eine frühere Version dieser Tools verwenden.

| Zweck | Endpunkt |
|-------|----------|
| Zustand lesen | `GET /api/status` |
| Soll-Spannung setzen | `POST /api/setpoint?voltage=<V>` |
| Ausgang umschalten | `POST /api/command?action=toggle_output` |
| Strombegrenzung umschalten | `POST /api/command?action=toggle_limit` |
| Spannungsregelung umschalten | `POST /api/command?action=toggle_regulation` |
| Preset abrufen (nur `variac_demo.py`) | `POST /api/command?action=recall_p1` … `recall_p3` |
