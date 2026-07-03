# Backlog – `tt_esp32controller` (+ `tt_voltmeter`)

Priorisierte Umsetzungsliste, gruppiert in Pakete. Detail-Analyse siehe [`REVIEW.md`](REVIEW.md).

**Legende**
- **Aufwand:** S = klein (Minuten–~1h), M = mittel, L = groß
- **Status:** `offen` · `in Arbeit` · `erledigt` · `verworfen`
- IDs sind stabil; neue Punkte hinten anhängen, IDs nicht wiederverwenden.

## Fortschritt

**Stand:** 2026-06-21 · **Gesamt: 22 / 33 Punkte erledigt**

| Paket | Status | Fortschritt |
|-------|--------|-------------|
| A — Projekt-Setup & Flash-Basis | ✅ erledigt | 5/5 |
| B — Schnelle Bugfixes & Konsistenz | ✅ erledigt | 3/3 |
| C — Regelung „snappy" | ✅ erledigt | 5/5 |
| D — Robustheit / Nebenläufigkeit | ✅ erledigt | 2/2 |
| E — API-Vereinfachung | ⬜ offen | 0/1 |
| F — Struktur & Modernisierung | ⬜ offen | 0/5 |
| G — Web-Oberfläche | ⬜ offen | 0/2 |
| H — Sicherheit (optional) | ⬜ offen | 0/1 |
| J — Voltmeter-Fernsteuerung via Controller | ✅ erledigt | 7/7 |
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
- [x] **C — Regelung „snappy"**
  - [x] #20 Simulationsmodus
  - [x] #21 RMS-Glättung Voltmeter
  - [x] #18 Voltmeter-Datenfrische
  - [x] #3 Positions-Offset
  - [x] #17 Spannungsregelung neu  _(#2 Anti-Windup entfällt damit)_
- [x] **D — Robustheit / Nebenläufigkeit**
  - [x] #4 Logging thread-safe
  - [x] #5 Geteilte Zustände schützen
- [ ] **E — API-Vereinfachung & -Bereinigung**
  - [ ] #22 API vereinfachen & bereinigen
- [ ] **F — Struktur & Modernisierung**
  - [ ] #10 Modularisierung
  - [ ] #15 Typos (`Whiper`→`Wiper`, `corse`→`coarse`)
  - [ ] #14 ArduinoJson v7
  - [ ] #9 Partitionierung 16 MB
  - [ ] #31 Regelparameter konfigurierbar
- [ ] **G — Web-Oberfläche: neues Design**
  - [ ] #23 Web-Oberfläche neu gestalten
  - [ ] #13 Live-Daten über WebSocket
- [ ] **H — Sicherheit (optional)**
  - [ ] #11 Auth & REST-Hygiene
- [x] **J — Voltmeter-Fernsteuerung via Controller**
  - [x] #27 Bidirektionales Befehls-/Antwort-Protokoll (UART)
  - [x] #28 Voltmeter: Menüfunktionen über den Link
  - [x] #29 Controller: API + Web-UI fürs Voltmeter
  - [x] #30 Kür: Voltmeter-FW-Update via Controller
  - [x] #34 Spannungs-Offset über Web/API setzen
  - [x] #33 FW-Version aus .bin auslesen/anzeigen
  - [x] #32 LCD-Anzeige während Voltmeter-FW-Update
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
- **2026-06-20 — #3 umgesetzt (Positions-Offset).** `estimatePositionForVoltage()` um
  `minWhiperPos`-Offset ergänzt; Vorsteuer-Schätzung war systematisch um `|minWhiperPos|` zu hoch.
  Kompiliert.
- **2026-06-20 — #17 umgesetzt (Spannungsregelung neu) → Paket C (Code) fertig.** PID + Preset-
  Zustandsmaschine ersetzt durch Vorsteuerung + gain-gerechte Korrektur + Deadband/Drift-Trim
  (`voltsPerStep()`, `RegPhase`-Statemachine). REG-Taste: ein=anfahren+halten, aus=One-shot.
  #2 (Anti-Windup) hinfällig. Normal- und Sim-Build kompilieren. Am Gerät getestet: läuft gut.
  Verfeinerung: Vorsteuerung stoppt richtungsabhängig kurz vor dem Ziel
  (`REG_FEEDFORWARD_UNDERSHOOT_V`, ~3 V) → kein Überschießen, Sollwert wird einseitig angefahren.
- **2026-06-20 — #18 umgesetzt (Datenfrische).** `isVoltageDataFresh()` (Timeout 250 ms):
  Regelung pausiert ohne frische Werte (Position halten + Warnung), Kalibrier-/Preset-Übernahmen
  aus dem Messwert blockiert, API-Flags `ist_fresh`/`voltage_fresh`. Normal- und Sim-Build kompilieren.
- **2026-06-20 — Voltmeter REBOOT + RESET_DEFAULTS + Protokoll-Doku.** Fern-Rettungsleinen
  (Soft-Reset, Kalibrierung auf Standard) ergänzt. Link-Protokoll + #30-Ablauf konsolidiert in
  [`tt_voltmeter/documentation/Link-Protokoll.md`](tt_voltmeter/documentation/Link-Protokoll.md)
  (Befehlssatz/CMD-Codes, Frame-Format, ROM-Bootloader-Plan). Nächster Schritt: #30 Schritt 1.
- **2026-06-21 — #30 abgeschlossen (Paket J 4/5), am Gerät end-to-end verifiziert.**
  Voltmeter-FW-Update über den ROM-Bootloader (AN3155): Voltmeter `ENTER_BOOTLOADER` (0x40) +
  Sprung-Fix (`HAL_DeInit`/`HAL_RCC_DeInit` vor dem Sprung, `VTOR`; ohne war ADC/DMA/Timer aktiv
  und der ROM-Loader reagierte nicht). Controller = AN3155-Host (8E1, Init/Get/Erase/Write/Go),
  eigener `voltmeterUpdateTask`, `communicationTask` suspendiert, Ausgang/Regelung aus.
  Diagnose über BOOT0+`?skipenter=1` isoliert (Host ok → Sprung war schuld). **Page-Erase nur der
  Programmpages** → letzte Flash-Page (emuliertes EEPROM = Kalibrierung) bleibt erhalten (verifiziert).
  Upload mit Fortschrittsanzeige (XHR). Voltmeter auf **V1.2.2**. Commit `a11a7c7`.
  Neu eingeplant: **#32** (LCD-Anzeige während des Updates) als Paket-J-Punkt.
- **2026-06-21 — #34 umgesetzt (Spannungs-Offset über Web/API), Paket J 4/7.** Neuer Link-Befehl
  `SET_OFFSET` (0x11, Plausi −50…+50 V, EEPROM) + API `/api/voltmeter/offset` + Eingabefeld/Button
  im Panel. Voltmeter auf **V1.2.3**. Beide Builds kompilieren; Geräte-Test ausstehend.
  Außerdem neu eingeplant: **#33** (FW-Version aus .bin auslesen, Magic-Tag).
- **2026-06-21 — #33 umgesetzt (FW-Version aus .bin).** Magic-Tag `@@VMFW@@<FW>` im Voltmeter
  (`used` + Start-Referenz, im Image verifiziert). Controller scannt die `.bin`
  (`/api/voltmeter/update/fileversion`); UI zeigt die Datei-Version dauerhaft + nach Upload, und
  vergleicht beim „Update starten" mit der laufenden Version (Warnung bei identischer Version).
  Beide Builds kompilieren; Geräte-Test ausstehend.
- **2026-06-21 — #33 + #34 am Gerät verifiziert (Paket J 6/7).** Spannungs-Offset setzen und
  Datei-Versionsanzeige/-vergleich funktionieren. Offen in Paket J nur noch #32 (LCD-Anzeige).
- **2026-06-21 — #32 umgesetzt (LCD-Anzeige während Voltmeter-FW-Update).** Eigener Screen
  „Voltmeter-Update / Variac gesperrt - Ausgang AUS" mit Fortschrittsbalken, %-Wert und
  Statusmeldung (Erfolg grün / Fehler rot) im `displayUpdateTask`; Bedienung (Tasten/Encoder)
  während des Updates gesperrt (`userInputTask`), Encoder ohne Sprung nach Freigabe. Ergebnis
  bleibt 5 s stehen (`VM_UPDATE_RESULT_MS`), danach kompletter Screen-Rebuild (Normal- oder
  Settings-Modus); Ausgang bleibt AUS. OTA- und SIM-Build kompilieren.
- **2026-06-21 — #32 am Gerät verifiziert → Paket J abgeschlossen (7/7).** Damit ist die
  komplette Voltmeter-Fernsteuerung inkl. FW-Update über den Link fertig — das Gerät kann
  versiegelt werden, alles Weitere geht via OTA/Web. Nächstes Paket gemäß Reihenfolge: D.
- **2026-06-21 — Paket D umgesetzt (#4 + #5), Geräte-Test ausstehend.**
  #4: `logMessage()` → FreeRTOS-Queue (24 Einträge) + einzelner Logger-Task (Serial, Historie,
  WS, Flash nur dort); Historie-Snapshot unter Mutex beim WS-Connect; Drop-Zähler bei voller
  Queue. #5: Kalibrier-Viertupel überall atomar unter `calibMux` (Schreiber: Web-Config,
  `/api/calibration/save`, Settings-Modus; Leser: `getCalibration()`-Snapshot in
  `estimatePositionForVoltage()`/`voltsPerStep()`); `whiperPos`-RMW + `stepper.moveTo()`/`run()`
  unter `stepperMux` (`setWhiperMove()` als Kern). Einzelne 32-bit-Skalare bewusst volatile-only
  (auf ESP32 atomar, keine RMW-Sequenzen). OTA- und SIM-Build kompilieren.
- **2026-06-21 — Paket D am Gerät verifiziert → abgeschlossen (2/2, gesamt 22/33).** Controller
  auf **V3.3.0** (V3.2.0 im CHANGELOG abgeschlossen: Pakete B/C/J). Nächstes Paket: E (API).
- **2026-06-20 — #28/#29 abgeschlossen (Paket J 3/4).** Geführte 3-Punkt-Kalibrierung über den
  Link (`CAL3_MEASURE`/`CAL3_FINISH`, Referenzspannungen frei wählbar) + Web-Bedienfeld ergänzt;
  am Gerät verifiziert. Damit Voltmeter-Status/Faktor/Auto-Zero/Kalibrierung komplett via Web.
  Offen in Paket J nur noch #30 (FW-Update via Controller, Kür).
- **2026-06-20 — #28/#29 Batch 1 am Gerät verifiziert.** Link-Befehle `GET_STATUS`/`SET_FACTOR`/
  `RECAL` + Controller-API `/api/voltmeter/{status,factor,autozero}` + schlichtes Voltmeter-Panel
  in `settings.html`. Live-Wert ist bereits über den RMS-Stream da.
- **2026-06-20 — #27 Durchstich am Gerät verifiziert (Paket J).** Bidirektionaler Befehls-Link
  Controller↔Voltmeter über `Serial1`/USART1: Frame-Protokoll (`0xA5`/`0xB5`) neben dem RMS-Stream,
  `GET_VERSION` → `/api/voltmeter/version` liefert die Version über die Leitung. Voltmeter auf
  **V1.2.0**. Bidirektionale Kommunikation (auch RX-Richtung/PA10) bestätigt; Normalbetrieb läuft weiter.
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
| **F** | Struktur & Modernisierung | Wartbarkeit | 10, 15, 14, 9, 31 |
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
| 3  | Bug | Positions-Offset Grob-Anfahrt | `estimatePositionForVoltage()` um `minWhiperPos`-Offset ergänzt (war um `|minWhiperPos|` zu hoch). Basis für die Vorsteuerung in #17. | S | **erledigt** *(kompiliert; am Gerät/SIM zu prüfen)* |
| 17 | Regelung | **Spannungsregelung neu** | Dauer-PID + `currenPresetState`-Automat ersetzt durch: Vorsteuerung (`estimatePositionForVoltage`), Settle-Wait, gain-gerechte Einzelkorrektur (`voltsPerStep()`, Deadband ±1 V, Damping 0,8, Settle 150 ms, Klemme ±150 Schritte), Drift-Trim. REG-Taste: ein=anfahren+halten, aus=One-shot. Nutzt `isVoltageDataFresh()`. | L | **erledigt** *(kompiliert; im SIM/am Gerät abzustimmen)* |
| 2  | Bug | PID Anti-Windup Vorzeichen | **Entfallen** — Integrator mit #17 entfernt. | S | **erledigt** *(hinfällig durch #17)* |

## Paket D — Robustheit / Nebenläufigkeit

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 4  | Concurrency | Logging thread-safe | `logMessage()` über FreeRTOS-Queue an einen Logger-Task serialisieren (RAM-Historie, WS-Versand, Flash-Write nur dort). `String logHistory` nicht mehr aus mehreren Tasks. | M | **erledigt** *(am Gerät verifiziert)* |
| 5  | Concurrency | Geteilte Zustände schützen | Kalibrier-Viertupel atomar unter `calibMux` (+ `getCalibration()`-Snapshot); `whiperPos`-RMW + AccelStepper (`moveTo`/`run`) unter `stepperMux`. Einzelne 32-bit-Skalare (`setpoint_voltage`, `received_rms_value`) bewusst volatile-only — auf ESP32 atomar, keine RMW-Sequenzen. | L | **erledigt** *(am Gerät verifiziert)* |

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
| 31 | Konfig | Regelparameter konfigurierbar | Regelparameter (`REG_FEEDFORWARD_UNDERSHOOT_V`, Damping, Deadband, Settle) statt `#define` über config.json/Settings einstellbar machen → Tuning pro Gerät ohne Code-Änderung (z. B. richtungsabhängige Hysterese). Dabei ungenutztes `coarse_move_threshold` aufräumen/umwidmen. | M | offen |

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
> **Routing-Entscheidung: USART1 (PA9/PA10)** — Hardware-Mod am Voltmeter-Print umgesetzt; **beide Richtungen + GND am Gerät verifiziert**. Befehls-Link (#27–#29) und **FW-Update über den ROM-Bootloader** (#30, AN3155) laufen end-to-end. Konsole bleibt auf USB-CDC (PA11/PA12).

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 27 | Protokoll | Bidirektionales Befehls-/Antwort-Protokoll (UART) | Frame-Protokoll `0xA5 CMD LEN [payload] CHK 0xBB` (Befehl) / `0xB5 …` (Antwort), koexistiert mit dem RMS-Stream `0xAA…`. Voltmeter: `Serial1` (USART1) TX+RX (Core-IRQ/Ringpuffer). Controller: vereinheitlichter Parser + `sendVoltmeterCommand()`. Durchstich `GET_VERSION` / `/api/voltmeter/version` am Gerät verifiziert. | M | **erledigt** *(Durchstich am Gerät bestätigt)* |
| 28 | Voltmeter | Menüfunktionen über den Link | Link-Befehle `GET_VERSION`/`GET_STATUS`/`SET_FACTOR`/`RECAL` sowie `CAL3_MEASURE`/`CAL3_FINISH` (geführte 3-Punkt-Kalibrierung). Lokale USB-CDC-Konsole bleibt parallel. | M–L | **erledigt** *(am Gerät verifiziert)* |
| 29 | Controller | API + Web-UI fürs Voltmeter | API `/api/voltmeter/{version,status,factor,autozero,cal3/measure,cal3/finish}` + schlichtes Panel in `settings.html` (finale UI in Paket G/#23). Live-RMS schon vorhanden. | M–L | **erledigt** *(am Gerät verifiziert)* |
| 30 | Kür | Voltmeter-FW-Update via Controller | Über den eingebauten **ROM-UART-Bootloader** (AN3155): Voltmeter-App springt per Befehl ins System-Memory (`0x1FFFF000`), Controller (AN3155-Host, 8E1) flasht via Init/Get/**Page-Erase nur der Programmpages** (EEPROM/Kalibrierung bleibt)/Write/Go. FW-Binary per Web→LittleFS, Fortschritt/Status + Diagnose-Option (skip-enter/BOOT0). Recovery via BOOT0+Reset+ST-Link. | L–XL | **erledigt** *(am Gerät end-to-end verifiziert, EEPROM bleibt erhalten)* |
| 34 | Voltmeter | Spannungs-Offset über Web/API setzen | Gegenstück zu „Faktor setzen": Link-Befehl `SET_OFFSET` (0x11, Plausi −50…+50 V, EEPROM) + API `/api/voltmeter/offset?value=` + Eingabefeld/Button im Panel. Bisher war der Offset nur über die 3-Punkt-Kalibrierung änderbar. | S | **erledigt** *(am Gerät verifiziert)* |
| 33 | Controller | FW-Version aus .bin auslesen/anzeigen | Magic-Tag im Voltmeter (`"@@VMFW@@" FW`, `__attribute__((used))`); Controller scannt die hochgeladene `.bin` nach dem Tag → zeigt Datei-Version vs. laufende Version (`GET_VERSION`) → „Update nötig?". Greift erst ab dem ersten getaggten Build. | M | **erledigt** *(am Gerät verifiziert)* |
| 32 | LCD | Anzeige während Voltmeter-FW-Update | Während des Flashs eigener LCD-Screen „Voltmeter-Update läuft – Variac gesperrt" mit Fortschritt-% und Statusmeldung; Bedienung (Tasten/Encoder) gesperrt. Nach Abschluss (Erfolg/Fehler) ~5 s Ergebnis anzeigen, dann zurück in den Normalbetrieb — **Ausgang bleibt aus** (kein Auto-Einschalten). | M | **erledigt** *(am Gerät verifiziert)* |

## Paket I — Dokumentation aktualisieren

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 24 | Docs | Doku aktualisieren & vervollständigen | Alle Dokumente (Kalibrierung, REST-API, Status-LED, USB CDC, TFT-Settings, Build/Flash) auf den finalen Stand bringen, Lücken schließen, einheitliche Form & Ablage. | M | offen |
| 12 | Docs | API-Doku Single-Source | API-Doku nur einmal pflegen (HTML in `data/` als Quelle vs. `.docx`), Drift vermeiden. Teil von #24. | M | offen |

---

## Notizen / weitere Kandidaten (noch nicht eingeordnet)

*(Hier sammeln wir während der Diskussion neue Punkte, bevor sie eine ID bekommen.)*
