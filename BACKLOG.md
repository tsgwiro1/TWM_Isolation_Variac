# Backlog – `tt_esp32controller` (+ `tt_voltmeter`)

Priorisierte Umsetzungsliste, gruppiert in Pakete. Detail-Analyse siehe [`REVIEW.md`](REVIEW.md).

**Legende**
- **Aufwand:** S = klein (Minuten–~1h), M = mittel, L = groß
- **Status:** `offen` · `in Arbeit` · `erledigt` · `verworfen`
- IDs sind stabil; neue Punkte hinten anhängen, IDs nicht wiederverwenden.

## Fortschritt

**Stand:** 2026-06-20 · **Gesamt: 11 / 29 Punkte erledigt**

| Paket | Status | Fortschritt |
|-------|--------|-------------|
| A — Projekt-Setup & Flash-Basis | ✅ erledigt | 5/5 |
| B — Schnelle Bugfixes & Konsistenz | ✅ erledigt | 3/3 |
| C — Regelung „snappy" | 🔄 in Arbeit | 3/5 |
| D — Robustheit / Nebenläufigkeit | ⬜ offen | 0/2 |
| E — API-Vereinfachung | ⬜ offen | 0/1 |
| F — Struktur & Modernisierung | ⬜ offen | 0/4 |
| G — Web-Oberfläche | ⬜ offen | 0/2 |
| H — Sicherheit (optional) | ⬜ offen | 0/1 |
| J — Voltmeter-Fernsteuerung via Controller | ⬜ offen | 0/4 |
| I — Dokumentation | ⬜ offen | 0/2 |

### Checkliste

- [x] **A — Projekt-Setup & Flash-Basis**
  - [x] #25 Migration ins Mono-Repo
  - [x] #6 USB-/OTA-Environments + `uploadfs`
  - [x] #7 README + Flash-Anleitung
  - [x] #26 PlatformIO-Konfig Voltmeter
  - [x] #8 alte `libraries/` (in #25 aufgegangen)
- [x] **B — Schnelle Bugfixes & Konsistenz**
  - [x] #1 P3-Validierung
  - [x] #16 Kommentar/Code-Drift
  - [x] #19 Spannungs-Limits vereinheitlichen
- [ ] **C — Regelung „snappy"**
  - [x] #20 Simulationsmodus
  - [x] #21 RMS-Glättung Voltmeter
  - [x] #18 Voltmeter-Datenfrische
  - [ ] #3 Positions-Offset
  - [ ] #17 Spannungsregelung neu  _(#2 Anti-Windup entfällt damit)_
- [ ] **D — Robustheit / Nebenläufigkeit**
  - [ ] #4 Logging thread-safe
  - [ ] #5 Geteilte Zustände schützen
- [ ] **E — API-Vereinfachung & -Bereinigung**
  - [ ] #22 API vereinfachen & bereinigen
- [ ] **F — Struktur & Modernisierung**
  - [ ] #10 Modularisierung
  - [ ] #15 Typos (`Whiper`→`Wiper`, `corse`→`coarse`)
  - [ ] #14 ArduinoJson v7
  - [ ] #9 Partitionierung 16 MB
- [ ] **G — Web-Oberfläche: neues Design**
  - [ ] #23 Web-Oberfläche neu gestalten
  - [ ] #13 Live-Daten über WebSocket
- [ ] **H — Sicherheit (optional)**
  - [ ] #11 Auth & REST-Hygiene
- [ ] **J — Voltmeter-Fernsteuerung via Controller**
  - [ ] #27 Bidirektionales Befehls-/Antwort-Protokoll (UART)
  - [ ] #28 Voltmeter: Menüfunktionen über den Link
  - [ ] #29 Controller: API + Web-UI fürs Voltmeter
  - [ ] #30 Kür: Voltmeter-FW-Update via Controller
- [ ] **I — Dokumentation aktualisieren**
  - [ ] #24 Doku aktualisieren & vervollständigen
  - [ ] #12 API-Doku Single-Source

> Diese Checkliste ist die schnelle Abhak-Übersicht. Die Detailtabellen je Paket (unten)
> tragen Beschreibung, Aufwand und denselben Status.

### Protokoll

- **2026-06-19 — Paket A abgeschlossen.** Migration ins saubere Mono-Repo;
  USB-/OTA-Environments + Filesystem-Upload dokumentiert; eigenes PlatformIO-Projekt fürs
  Voltmeter; **beide Firmwares compile-verifiziert** (Controller Flash 42,5 %, Voltmeter 32 %);
  Doku-Struktur (Plandateien ins Root) und Cleanup veralteter Arduino-IDE-Dokumente.
  Commits: `7805734`, `6e82224`, `eb4a691`, `ca28f80`, `825887b`, `f20b88b`, `9cafc73`.
- **2026-06-19 — Kleinfix:** `WiFi.setSleep(false)` im Controller (`setup()` nach WLAN-Connect)
  gegen langsames OTA / träges Web-UI (WiFi-Modem-Sleep). Kompiliert; Geschwindigkeit am Gerät zu testen.
- **2026-06-20 — Version V3.14 + Paket B abgeschlossen.** Controller-FW V3.13→V3.14, CHANGELOG je
  Firmware angelegt. Bugfixes: P3-Validierung (#1, inkl. `String`-Basis-Bug), Kommentar/Code-Drift (#16);
  Spannungs-Limits via zentraler `maxVoltageTarget()` vereinheitlicht (#19, „0 … kalibriertes Max").
  Kompiliert (Controller).
- **2026-06-20 — SemVer + Paket C gestartet (#20).** Umstellung auf Semantic Versioning
  (Controller V3.2.0, Voltmeter V1.1.0). Simulationsmodus umgesetzt: Env `esp32s3_sim` / `-D SIM`,
  Voltmeter-Quelle durch Streckenmodell ersetzt (linear + Lag + Abweichung/Rauschen). Normal- und
  Sim-Build kompilieren. SIM am Gerät getestet → bestätigt: Voltmeter-Latenz ist der dominante Faktor.
- **2026-06-20 — #21 umgesetzt + am Gerät verifiziert (Voltmeter V1.1.0).**
  `sendRMSValue(last_rms_value)` statt EMA-Wert (Variante A): Mess-Latenz ~0,4–0,9 s → ~40 ms;
  EMA nur noch fürs lokale `?live`-Display. Rohwert schwankt am Gerät nur ~±0,2 V.
  Zusätzlich (Dev-Tooling): Konsole/Menü von USART1 auf native **USB-CDC** umgestellt (PA9/PA10
  nicht herausgeführt) und Float-`printf` aktiviert (`%f` war unter `nano.specs` leer). Verifiziert.
- **2026-06-20 — #18 umgesetzt (Datenfrische).** `isVoltageDataFresh()` (Timeout 250 ms):
  Regelung pausiert ohne frische Werte (Position halten + Warnung), Kalibrier-/Preset-Übernahmen
  aus dem Messwert blockiert, API-Flags `ist_fresh`/`voltage_fresh`. Normal- und Sim-Build kompilieren.
- **2026-06-20 — Paket J aufgenommen + Link auf USART1 vorbereitet.** Neues Paket J
  (Voltmeter-Fernsteuerung via Controller: #27 Protokoll, #28 Menü über Link, #29 API/Web, #30 FW-Update
  via ROM-Bootloader). Routing-Entscheidung USART1 (ROM-Bootloader-Port). Voltmeter-Daten-Link
  firmwareseitig USART2→USART1 verlegt (TX); Hardware-Mod erledigt und **am Gerät verifiziert**
  (Controller empfängt RMS über PA9, Spannung wird angezeigt).

## Paket-Reihenfolge (festgelegt)

> Repo-Layout: **Mono-Repo** (`tt_esp32controller/` + `tt_voltmeter/`; projektspezifische Doku im jeweiligen Projektordner). Plandateien (`REVIEW.md`, `BACKLOG.md`) im Repo-Root. Migration (#25) zuerst.


| Reihenfolge | Paket | Ziel | enthält IDs |
|:-:|------|------|-------------|
| **A** | Projekt-Setup: Neues Repo & Flash-Basis | Sauberer Start, zuverlässig bauen/flashen | 25, 6, 7, 26, (8) |
| **B** | Schnelle Bugfixes & Konsistenz | Quick Wins, geringes Risiko | 1, 16, 19 |
| **C** | Regelung „snappy" (Kernanliegen) | Schnelles, präzises Anfahren ohne Pendeln | 20, 21, 18, 3, 17 (+2 erledigt sich) |
| **D** | Robustheit / Nebenläufigkeit | Stabilität unter RTOS | 4, 5 |
| **E** | API-Vereinfachung & -Bereinigung | Klare, schlanke, konsistente API | 22 |
| **F** | Struktur & Modernisierung | Wartbarkeit | 10, 15, 14, 9 |
| **G** | Web-Oberfläche: neues Design | Frisch, professionell, responsiv | 23, 13 |
| **H** | Sicherheit (optional) | Absicherung API/OTA | 11 |
| **J** | Voltmeter-Fernsteuerung via Controller | Menü/Kalibrierung/Status des Voltmeters über Web/API | 27, 28, 29, 30 |
| **I** | Dokumentation aktualisieren | Vollständig, auf finalem Stand | 24, 12 |

> Begründung: **A** zuerst — Migration ins saubere Repo + verlässliches Flash-/Test-Setup, danach passiert alle Arbeit dort. **B** räumt billige Fehler/Inkonsistenzen weg (inkl. #19, das in C gebraucht wird). **C** ist das Kernanliegen (Regelung) als zusammenhängender Block, mit Sim-Modus #20 als Enabler vorab. **D** härtet das Laufzeitverhalten. **E** schlankt die API, bevor **F** (Refactoring) und **G** (UI) darauf aufbauen — die UI wird gegen die *finale* API gebaut. **H** Sicherheit (laut Absprache optional). **J** (nachträglich aufgenommen) baut die Voltmeter-Fernsteuerung auf der finalen API/UI auf, kommt daher nach **G/H**. **I** Doku zuletzt, weil sie den endgültigen Stand von Code, API und UI beschreibt. *(Buchstabe J liegt vor I, weil später ergänzt — die Reihenfolge richtet sich nach der Tabelle, nicht nach dem Alphabet.)*

---

## Paket A — Projekt-Setup: Neues Repo & Flash-Basis

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 25 | Projekt | Migration in neues GitHub-Repo | **Mono-Repo** mit `tt_esp32controller/` + `tt_voltmeter/` + gemeinsamer Doku in ein sauberes, neues Repo überführen. Altlasten (CAD, Gehäuse, Messung, alte `libraries/` usw.) **nicht** mitnehmen; frische Struktur, `.gitignore`, Top-README. | M | **erledigt** |
| 6  | Build/Port | Upload-/Flash-Workflow | Getrennte Environments `esp32s3_usb` / `esp32s3_ota` statt Kommentar-Umschaltung; Filesystem-Upload (`buildfs`/`uploadfs`, auch via OTA) testen & dokumentieren. | M | **erledigt** *(Compile verifiziert: Flash 42,5 %/3 MB, RAM 14,8 %; Flash/OTA am Gerät noch offen)* |
| 7  | Build/Port | README + Flash-Anleitung | Build, USB-Erstflash, OTA-Update, Filesystem-Upload (im neuen Repo). | S | **erledigt** |
| 8  | Cleanup | Redundante `libraries/` | Alte Arduino-IDE-Libs nicht ins neue Repo übernehmen. **Geht in #25 auf** — nur relevant, falls Migration verschoben wird. | S | **erledigt** *(in #25 aufgegangen)* |
| 26 | Build/Port | PlatformIO-Konfig Voltmeter | Eigenes PIO-Projekt `tt_voltmeter/` (Plattform `ststm32`, `genericSTM32F103CB`, ST-Link). Quelle nach `src/`; Portierungsfix `extern "C"` für `DMA1_Channel1_IRQHandler`. Build erfolgreich (Flash 32 %/128 KB). | M | **erledigt** *(Compile verifiziert; Flash/ST-Link am Gerät noch offen)* |

## Paket B — Schnelle Bugfixes & Konsistenz

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 1  | Bug | P3-Validierung | `:425` prüft `p1` statt `p3`. Korrigieren. (Dabei auch `String((int)…, 1)`-Basis-Bug in den Validierungsmeldungen behoben.) | S | **erledigt** |
| 16 | Doc | Kommentar/Code-Drift | `communicationTask` Kommentar „>200ms" vs. Code `<1000ms` angleichen. | S | **erledigt** |
| 19 | Konsistenz | Spannungs-Limits vereinheitlichen | Zentrale `maxVoltageTarget()` = min(kalibriertes Max, `MAX_VOLTAGE_TARGET`); Sollwert/Presets/Tasten-Speichern überall `0 … kalibriertes Max`. | S | **erledigt** |

## Paket C — Regelung „snappy" (Kernanliegen)

> Reihenfolge im Paket: Sim-Modus als Enabler (#20) → Messquelle entschärfen (#21) → Datenfrische (#18) → Vorsteuer-Fix (#3) → neue Regellogik (#17). #2 erledigt sich dabei.

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 20 | Test | Simulationsmodus | Build-Flag `SIM` (Env `esp32s3_sim`): läuft auf dem **echten Board**, ersetzt nur die Voltmeter-Quelle durch ein Streckenmodell (linear + Lag + Abweichung/Rauschen, Spannung aus Stepper-Position). FW meldet „(SIM)". | M | **erledigt** |
| 21 | Voltmeter | RMS-Glättung reduzieren | `sendRMSValue(last_rms_value)` statt `smoothed_rms_value` → 2-Zyklen-Rohwert (~40 ms) an den Controller; EMA bleibt nur für die lokale `?live`-Anzeige. (Variante A) | S–M | **erledigt** *(Code fertig & kompiliert; FW V1.1.0 noch aufs Voltmeter zu flashen)* |
| 18 | Robustheit | Voltmeter-Datenfrische | `isVoltageDataFresh()` (Timeout 250 ms): Regelung pausiert bei veralteten/fehlenden Werten; Kalibrier-/Preset-Übernahmen aus dem Messwert blockiert; API-Flags `ist_fresh`/`voltage_fresh`. | M | **erledigt** *(kompiliert; am Gerät noch zu testen)* |
| 3  | Bug | Positions-Offset Grob-Anfahrt | `estimatePositionForVoltage()` `:834` um `minWhiperPos`-Offset ergänzen (bestätigt; `minWhiperPos` ist kalibrierungsbedingt negativ). Teil der neuen Vorsteuerung. | S | offen |
| 17 | Regelung | **Spannungsregelung neu** | Dauer-PID + `currenPresetState`-Automat ersetzen durch: modellbasierte Vorsteuerung (inverses lineares Modell, gain = V/Schritt aus Kalibrierung), **eine** beschleunigte Anfahrt, Settle-Wait auf frischen/stabilen Messwert, **gain-gerechte Einzelkorrektur** (`err/gain·0,9`), Deadband (±~1 V) + langsamer Drift-Trim. Behebt grobes Anfahren **und** Pendeln; enthält Timestep-Fix (40 ms vs. 100 ms). | L | offen |
| 2  | Bug | PID Anti-Windup Vorzeichen | `:852` `-max / -KI` → `-max / KI`. **Entfällt mit #17** (kein Integrator mehr) — nur fixen, falls #17 verschoben wird. | S | offen |

## Paket D — Robustheit / Nebenläufigkeit

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 4  | Concurrency | Logging thread-safe | `logMessage()` über FreeRTOS-Queue an einen Logger-Task serialisieren (RAM-Historie, WS-Versand, Flash-Write nur dort). `String logHistory` nicht mehr aus mehreren Tasks. | M | offen |
| 5  | Concurrency | Geteilte Zustände schützen | `setpoint_voltage`, `received_rms_value`, `whiperPos`, Kalibrierwerte per `portMUX`/Mutex/Queue statt nur `volatile`. | L | offen |

## Paket E — API-Vereinfachung & -Bereinigung

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 22 | API | API vereinfachen & bereinigen | Redundante/wenig sinnvolle Endpoints zusammenführen oder entfernen (z. B. Überschneidung `/data` ↔ `/api/status`; Nutzen von `/api/files`, `/api/files/delete` prüfen). Konsistentes, schlankes API-Schema definieren (einheitliche Pfade/Antworten, GET nur für Lesen). Basis für #13/#23. | M | offen |

## Paket F — Struktur & Modernisierung

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 10 | Refactor | Modularisierung | `.ino` in Module aufteilen (`config`, `web`, `display`, `motor`, `comm`, `actions`, `logging`); ggf. `.ino`→`.cpp` + Forward-Declarations. | L | offen |
| 15 | Cosmetic | Typos | `Whiper`→`Wiper`, `CORSE`/`corse`→`coarse` konsistent umbenennen (zusammen mit #10, da gleiche Dateien). | S | offen |
| 14 | Refactor | ArduinoJson v7 | Migration von `StaticJsonDocument` auf v7-API. | M | offen |
| 9  | Build | Partitionierung 16 MB | ~7,5 MB ungenutzt: LittleFS/OTA vergrößern; FS-Partitionslabel konsistent benennen + Code-Kommentar. | M | offen |

## Paket G — Web-Oberfläche: neues Design

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 23 | Frontend | Web-Oberfläche neu gestalten | Frisches, professionelles, responsives Design (index/settings/log/doc): konsistentes Styling, klare Bedienung, gegen die bereinigte API (#22). | L | offen |
| 13 | Frontend | Live-Daten über WebSocket | 2-s-Polling von `/data` durch WS-Push ersetzen (weniger Last, flüssigere Anzeige). Teil des Redesigns. | M | offen |

## Paket H — Sicherheit (optional)

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 11 | Security | Auth & REST-Hygiene | HTTP-Auth/Token für `/api/*`, OTA-Passwort aktivieren, GET→POST/PUT für Mutationen, `delete`/`reboot`/`calibration` absichern. *(laut Absprache „optional/später".)* | L | offen |

## Paket J — Voltmeter-Fernsteuerung via Controller

> Ziel: Voltmeter-Menüfunktionen (Status, Version, Live, Skalierungsfaktor, Auto-Zero & 3-Punkt-Kalibrierung) über die Web-Oberfläche/API des Controllers statt nur über die lokale USB-CDC-Konsole.
> **Routing-Entscheidung: USART1 (PA9/PA10)** — Hardware-Mod am Voltmeter-Print umgesetzt; **Senderichtung am Gerät verifiziert** (Voltmeter PA9 → Controller, Spannung wird angezeigt). Empfangsrichtung (Controller-TX → Voltmeter PA10) + GND für #27 noch zu legen. Damit ist neben dem Befehls-Link (#27–#29) auch das **FW-Update über den ROM-Bootloader** (#30, AN3155) ohne eigenen Bootloader möglich. Konsole bleibt auf USB-CDC (PA11/PA12).

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 27 | Protokoll | Bidirektionales Befehls-/Antwort-Protokoll (UART) | Bestehendes `0xAA…0xBB`-Framing um einen **Nachrichtentyp** erweitern, sodass neben den streamenden RMS-Frames auch Befehle/Antworten laufen. Controller sendet auf `Serial1` (TX); Voltmeter empfängt auf dem Link-UART (aktuell nur TX genutzt) — **USART2/PA3 oder, empfohlen, USART1/PA10** (siehe Prerequisite, ermöglicht zudem #30). Enabler für #28/#29/#30. | M | offen |
| 28 | Voltmeter | Menüfunktionen über den Link | Menülogik (Status, Version, Live, `setfactor`, Auto-Zero `recal`, 3-Punkt-`calibrate`) so umbauen, dass sie über das Protokoll (#27) ansprechbar ist; lokale USB-CDC-Konsole bleibt parallel. | M–L | offen |
| 29 | Controller | API + Web-UI fürs Voltmeter | API-Endpunkte (Status/Version/Faktor lesen+setzen, Kalibrierung anstoßen/führen) + Web-Panel. Live-RMS ist bereits vorhanden. Baut auf der bereinigten API (#22) und dem Redesign (#23) auf. | M–L | offen |
| 30 | Kür | Voltmeter-FW-Update via Controller | **Deutlich einfacher, wenn der Link auf USART1 (PA9/PA10) gelegt wird** (PCB-Mod): dann nutzbar über den eingebauten **ROM-UART-Bootloader** (AN3155). Voltmeter-App springt per Befehl ins System-Memory (`0x1FFFF000`), Controller flasht via Bootloader-Protokoll (Init/Erase/Write/Go) + FW-Binary per Web→LittleFS→Stream. Kein eigener Bootloader nötig; Recovery via BOOT0+Reset+ST-Link. *(Mit Link auf USART2 stattdessen nur via eigenem App-Bootloader — groß/riskant.)* | L–XL | offen *(Kür)* |

## Paket I — Dokumentation aktualisieren

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 24 | Docs | Doku aktualisieren & vervollständigen | Alle Dokumente (Kalibrierung, REST-API, Status-LED, USB CDC, TFT-Settings, Build/Flash) auf den finalen Stand bringen, Lücken schließen, einheitliche Form & Ablage. | M | offen |
| 12 | Docs | API-Doku Single-Source | API-Doku nur einmal pflegen (HTML in `data/` als Quelle vs. `.docx`), Drift vermeiden. Teil von #24. | M | offen |

---

## Notizen / weitere Kandidaten (noch nicht eingeordnet)

*(Hier sammeln wir während der Diskussion neue Punkte, bevor sie eine ID bekommen.)*
