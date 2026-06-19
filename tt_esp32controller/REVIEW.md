# Code-Review & Optimierungsplan – `tt_esp32controller`

**Stand:** 2026-06-18
**Umfang:** Firmware (`src/tt_esp32controller.ino`, `src/Action.{h,cpp}`), Web-Frontend (`data/`),
Build-Konfiguration (`platformio.ini`, `partitions.csv`), Doku (`documentation/`).
**Kontext:** Portierung der STM32-Version (`../tt_controller`) auf ESP32-S3 mit zusätzlicher
REST-/WebSocket-API und Web-Oberfläche. Code läuft grundsätzlich; es geht um Optimierung.

Dieses Dokument ist die Befundaufnahme + priorisierter Plan. Bug-Fixes sind hier **nur geplant**,
noch nicht umgesetzt.

---

## 1. Befund

### 1.1 Konkrete Bugs (im Code verifiziert)

| # | Ort | Problem |
|---|-----|---------|
| B1 | `src/tt_esp32controller.ino:425` | Preset-Validierung P3 prüft `presets["p1"].is<int>()` statt `p3` (Copy-Paste). P3 wird faktisch nicht korrekt validiert. |
| B2 | `src/tt_esp32controller.ino:852` | PID Anti-Windup: `integral_error = -max_integral_contribution / -KI;` ergibt durch doppeltes Minus einen **positiven** Wert im **negativen** Clamp-Zweig → Vorzeichen des I-Anteils kippt am unteren Anschlag. Soll: `-max_integral_contribution / KI`. |
| B3 | `src/tt_esp32controller.ino:834` | **Bestätigt.** `estimatePositionForVoltage()` liefert die Spanne `(maxWhiperPos − minWhiperPos)`, addiert aber `minWhiperPos` nicht als Offset zurück. Da `minWhiperPos` aus der Kalibrierung ein **negativer** Wert ist (Endschalter löst vor dem Bahnende aus, Min-Position liegt dahinter), liefert die Formel bei `target = minVoltageAtMinPos` 0 statt `minWhiperPos` und überschätzt jede Zielposition um `|minWhiperPos|`. Fix: `minWhiperPos + (target − minV)*(maxWhiperPos − minWhiperPos)/(maxV − minV)`. |
| B4 | `src/tt_esp32controller.ino:1895` vs. Kommentar | Kommentar spricht von „>200ms", Code nutzt `< 1000` ms Encoder-Ruhezeit. Nur Doku/Code-Drift, kein Funktionsfehler. |

### 1.2 Nebenläufigkeit (RTOS) — größtes technisches Risiko

- Geteilte Zustände (`setpoint_voltage`, `received_rms_value`, `whiperPos`, `minWhiperPos`, …)
  werden nur per `volatile` zwischen mehreren Tasks geteilt. `volatile` garantiert **weder
  Atomizität** für `float`/Mehrbyte-Zugriffe **noch Speicherbarrieren**.
- `logHistory` (Arduino-`String`) wird in `logMessage()` aus mehreren Tasks gleichzeitig
  konkateniert und via `ws.textAll()` gesendet. `String` ist nicht thread-safe →
  Heap-Fragmentierung und mögliche Korruption.
- `logMessage()` schreibt zusätzlich aus beliebigem Task-Kontext ins LittleFS (Flash) — blockierend.

### 1.3 Sicherheit / Safety (Hochspannungsgerät)

- **Keine Authentifizierung** auf der Web-API. Jeder im Netz kann u. a. den Motor verfahren,
  Kalibrierung überschreiben (`/api/calibration/save`), Reboot auslösen (`/api/reboot`),
  Dateien löschen (`/api/files/delete`).
- **OTA-Passwort auskommentiert** (`src/tt_esp32controller.ino:2202`).
- Zustandsändernde Aktionen laufen über **HTTP GET** (CSRF-anfällig, nicht REST-konform).
- *Einordnung laut Absprache: Gerät läuft im vertrauenswürdigen Heimnetz → Security ist
  „optional / später" (Abschnitt 3).*

### 1.4 Architektur / Wartbarkeit

- Monolithische `.ino` mit ~2466 Zeilen. Vermischt Config, Webserver, Display, Motor/PID,
  Kommunikation, Logging, Actions.
- `.ino` nutzt Arduinos Auto-Prototypen (z. B. `cb_ValueAction` wird im Webserver vor seiner
  Definition verwendet). Bei sauberer `.cpp`-Migration sind Forward-Declarations nötig.
- ArduinoJson v6 (`StaticJsonDocument`) — funktioniert; v7 wäre eine optionale Modernisierung.

### 1.5 Build / Repo / Portierung

- **PlatformIO bereits weitgehend aufgesetzt**: `platformio.ini` vollständig (Board, TFT_eSPI per
  `build_flags`, `lib_deps`, OTA-Upload), `partitions.csv` vorhanden, Build funktioniert.
- **Upload-Workflow ist der offene Punkt**: In `platformio.ini` ist USB-Upload auskommentiert und
  `upload_protocol = espota` aktiv. Das Umschalten USB ↔ OTA passiert per Kommentieren —
  fehleranfällig. Der **Filesystem-Upload** (`data/` → LittleFS) ist nirgends dokumentiert.
- 16 MB Flash, aber Partitionen enden bei ~8,4 MB → ~7,5 MB ungenutzt.
- `partitions.csv` nennt die FS-Partition `spiffs`, gemountet wird LittleFS darauf
  (`LittleFS.begin(true,"/littlefs",10,"spiffs")`) — funktioniert, ist aber irreführend benannt.
- Top-Level `../libraries/` (alte Arduino-IDE-Libs) ist für das PIO-Projekt redundant
  (Libs kommen via `lib_deps`).
- Kosmetik: durchgängige Typos `Whiper` (→ `Wiper`), `CORSE`/`corse` (→ `coarse`).

### 1.6 Web-Frontend / Doku

- API-Doku liegt doppelt vor: als `.docx` (`documentation/`) und als HTML (`data/doc_api.html`).
  Pflegeaufwand/Drift-Risiko.
- `script.js` pollt `/data` alle 2 s; Live-Log läuft bereits über WebSocket — Polling könnte
  perspektivisch ebenfalls über WS laufen (weniger Last). Optional.

---

## 2. Plan — Pflicht

> Ziel: stabiler, korrekter, sicher flashbarer Stand. Reihenfolge = empfohlene Bearbeitung.

- [ ] **P-1 Upload-/Flash-Workflow absichern** *(Portierung)*
  - Zwei getrennte PIO-Environments statt Kommentar-Umschaltung, z. B.
    `[env:esp32s3_usb]` (USB, erster Flash) und `[env:esp32s3_ota]` (espota).
  - Filesystem-Upload dokumentieren/testen: `pio run -e … -t buildfs` + `-t uploadfs`
    (LittleFS-Image aus `data/`), inkl. OTA-Variante.
  - Verifizieren: Erst-Flash via USB, danach App **und** Filesystem via OTA.
- [ ] **P-2 Bug B1** — P3-Validierung auf `p3` korrigieren.
- [ ] **P-3 Bug B2** — Anti-Windup-Vorzeichen korrigieren (`-max / KI`).
- [ ] **P-V1 Bug B3** — `estimatePositionForVoltage()` um `minWhiperPos`-Offset ergänzen
      (bestätigt; siehe Tabelle B3).
- [ ] **P-4 Nebenläufigkeit Logging entschärfen** — `logMessage()` thread-safe machen:
      Log-Einträge über eine FreeRTOS-Queue an einen einzelnen Logger-Task serialisieren
      (RAM-Historie, WS-Versand und Flash-Write nur dort).
- [ ] **P-5 Geteilte Zustände absichern** — kritische Variablen (`setpoint_voltage`,
      `received_rms_value`, `whiperPos`, Kalibrierwerte) über `portMUX`/Mutex bzw. Queues
      schützen statt nur `volatile`.

## 3. Plan — Optional (Verbesserungen)

- [ ] **O-1 Security (vertrauenswürdiges Netz → später)**: HTTP-Basic/Token-Auth für `/api/*`,
      OTA-Passwort aktivieren, zustandsändernde Endpunkte von GET auf POST/PUT umstellen,
      `delete`/`reboot`/`calibration` zusätzlich absichern.
- [ ] **O-2 Modularisierung** — `.ino` in Module aufteilen
      (`config`, `web`, `display`, `motor`, `comm`, `actions`, `logging`),
      ggf. Migration `.ino` → `.cpp` + Header mit Forward-Declarations.
- [ ] **O-3 Partitionierung** — 16 MB nutzen: größeres LittleFS und/oder OTA-Reserve;
      FS-Partition konsistent `littlefs`/`spiffs` benennen + Code-Kommentar.
- [ ] **O-4 Doku-Single-Source** — API-Doku nur einmal pflegen (HTML in `data/` als Quelle,
      `.docx` daraus generieren oder umgekehrt); Drift vermeiden.
- [ ] **O-5 Repo-Aufräumen** — redundantes `../libraries/` für das PIO-Projekt prüfen/entfernen,
      Top-Level-`README` mit Build-/Flash-Anleitung.
- [ ] **O-6 Kosmetik** — Typos `Whiper`→`Wiper`, `corse`→`coarse` (konsistente Umbenennung).
- [ ] **O-7 ArduinoJson v7** — Migration von `StaticJsonDocument` (optional, wenn Libs aktualisiert).
- [ ] **O-8 Frontend** — `/data`-Polling perspektivisch über WebSocket statt 2-s-Poll.

## 4. Plan — Portierung VSCode / PlatformIO (Detail)

- [x] PlatformIO-Projektstruktur, Board, `lib_deps`, TFT_eSPI-Flags — **vorhanden**.
- [ ] **P-1** (siehe oben): saubere USB-/OTA-Environments + Filesystem-Upload-Doku — **offen**.
- [ ] Kurzanleitung „Build, USB-Erstflash, OTA-Update, Filesystem-Upload" ins Repo
      (Teil von O-5 README), da `platformio.ini` bereits Team-Hinweise enthält.

---

## 5. Offene Rückfragen

- *(keine offen)* — R-1 (B3) geklärt: `minWhiperPos` ist kalibrierungsbedingt negativ,
  Offset muss addiert werden.

---

> **Hinweis:** Die fortlaufende, priorisierbare Umsetzungsliste wird in
> [`BACKLOG.md`](BACKLOG.md) geführt. Dieses Dokument bleibt die Detail-Analyse.
