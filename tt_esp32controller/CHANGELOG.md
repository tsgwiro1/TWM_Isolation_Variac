# Changelog – TWM Isolation Variac Controller

Nennenswerte Änderungen an der Controller-Firmware (ESP32-S3).
Format angelehnt an [Keep a Changelog](https://keepachangelog.com/de/1.1.0/).
Versionierung nach [Semantic Versioning](https://semver.org/lang/de/) (MAJOR.MINOR.PATCH),
ab V3.2.0. Frühere Tags (V3.13 usw.) folgten der alten Zählweise.
Die Version entspricht der `#define FW`-Zeichenkette in `src/tt_esp32controller.ino`.

## [V4.1.0] – in Entwicklung

### Infrastruktur
- **ArduinoJson v7** (#14): Migration von v6 (`StaticJsonDocument` → `JsonDocument`,
  `containsKey()` → `isNull()`-Idiom, `createNestedObject()` → `to<JsonObject>()`/
  `add<JsonObject>()`); `lib_deps` auf `^7.0.0`. Keine Verhaltens-/API-Änderung.
- **Modularisierung** (#10): Die 3363-Zeilen-`.ino` ist in Module aufgeteilt (`pins.h`,
  `state`, `logging`, `config`, `motor`, `comm`, `display`, `actions`, `web`, `system`,
  `sim` — je `.h`/`.cpp`); die `.ino` enthält nur noch `setup()`/`loop()` (414 Zeilen).
  Modul-Interna jetzt `static`/gekapselt (`loggingInit()`, `logHistorySnapshot()`,
  `configRawJson()`, `initStepper()`). Reine Verschiebung — Verhalten, API und
  Speicherbedarf unverändert (Flash +1,2 KB durch Modul-Grenzen).
- **Typos bereinigt** (#15): `Whiper` → `Wiper` in allen internen Bezeichnern (77 Stellen;
  keine API-/JSON-Keys betroffen). `corse` existierte seit dem Regelungs-Umbau nicht mehr.

## [V4.0.0] – 2026-07-04

### Geändert
- **API nach REST-Konventionen umgebaut** (#22, **Breaking Change** → MAJOR-Version):
  - `GET` liest nur noch — alle zustandsändernden Aktionen sind jetzt `POST`
    (`/api/setpoint`, `/api/command`, `/api/reboot`, `/api/calibration`,
    `/api/voltmeter/{factor,offset,autozero,reboot,reset-defaults,cal3/*,update/start}`);
    Parameter weiterhin als Query-String.
  - `/data` entfernt — ist in `GET /api/status` aufgegangen (`states` um `p1_on..p3_on` ergänzt).
  - Entfernt (ungenutzt, Inhalte stecken in `/api/config`): `GET /api/presets`,
    `GET /api/presets/save`, `GET /api/calibration`.
  - `GET /api/calibration/save` → `POST /api/calibration`; `GET /api/files/delete` →
    `DELETE /api/files?filename=`.
  - Webseiten (`script.js`, `settings.js`) auf die neuen Methoden/Routen umgestellt.
  - **Neu: interaktive API-Doku** unter `doc_api.html` (RapiDoc, lokal im LittleFS —
    funktioniert ohne Internet) mit `openapi.yaml` als Single Source of Truth (löst #12);
    die alte handgepflegte HTML-API-Doku ist ersetzt.
  - Hinweis: Firmware und Filesystem (`uploadfs`) müssen zusammen aktualisiert werden.

## [V3.3.0] – 2026-07-04

### Geändert
- **Konfiguration/Kalibrierung ins NVS** (#35): `saveConfiguration()`/`loadConfiguration()`
  persistieren den Config-JSON-String jetzt im NVS (eigene Flash-Partition) statt als
  `config.json` im LittleFS — Konfiguration und Kalibrierung überleben damit jedes
  `uploadfs` (Webseiten-Update) und jede App-OTA. Einmalige Migration beim Boot:
  vorhandene `config.json` wird validiert, 1:1 ins NVS übernommen und aus dem LittleFS
  entfernt. `GET /api/config` (inkl. `?download`) liefert unverändert dasselbe JSON,
  `POST /api/config` unverändert.
- **Logging thread-safe** (#4): `logMessage()` formatiert nur noch und legt den Eintrag in eine
  FreeRTOS-Queue; ein einzelner Logger-Task übernimmt Serial-Ausgabe, RAM-Historie, WebSocket-
  Versand und Flash-Write (nur WARN+). Damit entfallen die konkurrierenden `String`-/LittleFS-/
  `ws.textAll()`-Zugriffe aus mehreren Tasks (Heap-Korruptionsrisiko). Volle Queue → Meldung wird
  verworfen und gezählt (Nachmeldung im Log). WS-Connect liest die Historie als Snapshot unter Mutex.
- **Geteilte Zustände geschützt** (#5): Die 4 Kalibrierwerte (min/max Position + Spannung) werden
  überall als konsistenter Satz unter `calibMux` geschrieben/gelesen (`getCalibration()`-Snapshot
  in der Regelungs-Mathematik). `whiperPos`-Read-Modify-Write und alle `stepper.moveTo()`/`run()`-
  Aufrufe laufen unter `stepperMux` (AccelStepper ist nicht thread-safe; `setWhiperMove()` als
  gemeinsamer Kern). Einzelne 32-bit-Skalare (`setpoint_voltage`, `received_rms_value`) bleiben
  bewusst volatile-only — ausgerichtete 32-bit-Zugriffe sind auf dem ESP32 atomar, zusammengesetzte
  Sequenzen existieren darauf nicht.

## [V3.2.0] – 2026-06-21

### Hinzugefügt
- Simulationsmodus (`SIM`, PlatformIO-Env `esp32s3_sim`): die „gemessene" Spannung wird aus der
  Stepper-Position berechnet (lineares Streckenmodell + First-Order-Lag + leichte Abweichung +
  Rauschen). Erlaubt das Abstimmen der Regelung ohne Variac/Voltmeter; die Firmware meldet sich
  als „(SIM)". (#20)
- Datenfrische-Prüfung der Voltmeter-Messwerte (`isVoltageDataFresh()`, Timeout 250 ms): die
  Regelung pausiert bei veralteten/fehlenden Werten (Position wird gehalten), Kalibrier- und
  Preset-Übernahmen aus dem Messwert werden blockiert. API liefert `ist_fresh` (`/data`) bzw.
  `voltage_fresh` (`/api/status`). (#18)
- **Bidirektionaler Befehls-Link zum Voltmeter** (Paket J, Durchstich): vereinheitlichter
  Serial1-Parser (RMS-Frames `0xAA` + Antwort-Frames `0xB5`), `sendVoltmeterCommand()`, neue
  Route `/api/voltmeter/version` (holt die Voltmeter-Version über die Leitung). (#27)
- Voltmeter-Fernsteuerung: API `/api/voltmeter/status` (Faktor/Offsets), `/api/voltmeter/factor`
  (Faktor setzen), `/api/voltmeter/autozero` (Auto-Zero) + schlichtes Voltmeter-Panel in
  `settings.html` (finale UI in Paket G). (#29)
- Voltmeter-Spannungs-Offset direkt setzbar: API `/api/voltmeter/offset?value=` (→ `SET_OFFSET`)
  + Eingabefeld/Button im Voltmeter-Panel. (#34)
- FW-Version der hochgeladenen `.bin` anzeigen: API `/api/voltmeter/update/fileversion` scannt die
  Datei auf dem LittleFS nach dem Magic-Tag `@@VMFW@@` und liefert die Version. UI zeigt sie
  dauerhaft an (auch nach Upload); beim „Update starten" wird Datei- vs. laufende Version
  verglichen und gewarnt, wenn bereits dieselbe Version installiert ist. (#33)
- LCD-Anzeige während des Voltmeter-FW-Updates: eigener Screen „Voltmeter-Update /
  Variac gesperrt - Ausgang AUS" mit Fortschrittsbalken, Prozentwert und Statusmeldung
  (Erfolg grün / Fehler rot). Bedienung (Tasten/Encoder) ist während des Updates gesperrt.
  Nach Abschluss bleibt das Ergebnis 5 s stehen, dann kehrt die Anzeige in den Normalbetrieb
  zurück — der Ausgang bleibt AUS (kein automatisches Wiedereinschalten). (#32)
- Geführte 3-Punkt-Kalibrierung über Web: API `/api/voltmeter/cal3/measure` (Punkt messen) und
  `/api/voltmeter/cal3/finish` (Regression + speichern), inkl. Bedienfeld in `settings.html`. (#29)
- API `/api/voltmeter/reboot` und `/api/voltmeter/reset-defaults` + Buttons im Panel
  (Voltmeter neu starten / Kalibrierung auf Standard). (#29)
- **Voltmeter-Firmware-Update über den Link** (#30): AN3155-Host (`Serial1` auf 8E1,
  `0x7F`-Handshake, `Get` zur Erase-Erkennung, Mass-Erase, Write-Memory in 256-Byte-Blöcken,
  `Go`) in einem eigenen `voltmeterUpdateTask`; `communicationTask` wird während des Flashens
  suspendiert, Ausgang/Regelung aus. `.bin`-Upload nach LittleFS + Trigger/Status über
  `/api/voltmeter/update/upload`, `/start`, `/status`; Bedienfeld in `settings.html`.
  Recovery bei Fehlflash: ST-Link (Entwicklung/Test nur am offenen Gerät).
  **EEPROM-Erhalt:** statt Mass-Erase werden nur die Programmpages (0 … benötigte) gelöscht,
  die letzte Flash-Page (emuliertes EEPROM = Voltmeter-Kalibrierung) bleibt erhalten.
  Diagnose-Option `?skipenter=1` (ENTER_BOOTLOADER überspringen, VM via BOOT0 im ROM-Loader)
  + „Bootloader-Test"-Button. Upload mit Fortschrittsanzeige (XHR). (#30)

### Geändert
- **Spannungsregelung neu** (#17): PID-Regler und Preset-Zustandsmaschine ersetzt durch
  modellbasierte **Vorsteuerung** (`estimatePositionForVoltage`) + **gain-gerechte Einzelkorrektur**
  (`voltsPerStep()`, Deadband ±1 V, Damping 0,8, Settle 150 ms, Korrektur-Klemme ±150 Schritte)
  + **Drift-Trim** im Halten. Die Vorsteuerung stoppt bewusst **kurz vor dem Ziel in Fahrtrichtung**
  (`REG_FEEDFORWARD_UNDERSHOOT_V`, ~3 V), sodass der Sollwert von einer Seite angefahren wird
  (kein Überschießen). REG-Taste: EIN = Sollwert schnell anfahren und halten,
  AUS = nach Erreichen stoppen. Nutzt `isVoltageDataFresh()` (#18). Damit entfällt der
  PID-Anti-Windup-Fix (#2). `coarse_move_threshold` wird nicht mehr verwendet (Config-Feld bleibt vorerst).
- WiFi-Modem-Sleep deaktiviert (`WiFi.setSleep(false)` nach erfolgreichem WLAN-Connect)
  → schnelleres OTA und reaktiveres Web-Interface.
- Spannungs-Limits vereinheitlicht: Sollwert und Presets überall `0 … kalibriertes Max`
  über die zentrale Funktion `maxVoltageTarget()`. `MAX_VOLTAGE_TARGET` (260 V) ist jetzt
  nur noch die absolute Sicherheits-Obergrenze (Schutz bei defekter Kalibrierung). (#19)
- Kommentar in `communicationTask` an die tatsächliche Encoder-Ruhezeit (1000 ms) angepasst. (#16)

### Behoben
- Preset-Validierung: P3 prüfte fälschlich `p1` statt `p3` → P3 wurde nicht korrekt validiert. (#1)
- Fehlermeldungen der Preset-Validierung gaben durch `String((int)…, 1)` (Zahlenbasis 1)
  Müll statt der Grenze aus — korrigiert.
- Vorsteuerung `estimatePositionForVoltage()`: fehlender `minWhiperPos`-Offset ergänzt — die
  geschätzte Anfahrposition war (bei negativem `minWhiperPos`) systematisch um `|minWhiperPos|`
  zu hoch. (#3)

### Infrastruktur
- Migration in das neue Mono-Repo `TWM_Isolation_Variac` und auf PlatformIO / VS Code
  (Build via `platformio.ini`, Environments `esp32s3_usb` / `esp32s3_ota`,
  Filesystem-Upload via `uploadfs`).
- Controller-Dokumentation bereinigt (veraltete Arduino-IDE-Dokumente entfernt).

## [V3.13]
- Letzter Stand vor der Repo-Migration (zuvor Arduino-IDE-Projekt).
  Ältere Historie: siehe Git-Verlauf.
