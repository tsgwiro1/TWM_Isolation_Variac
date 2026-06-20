# Changelog – TWM Isolation Variac Controller

Nennenswerte Änderungen an der Controller-Firmware (ESP32-S3).
Format angelehnt an [Keep a Changelog](https://keepachangelog.com/de/1.1.0/).
Versionierung nach [Semantic Versioning](https://semver.org/lang/de/) (MAJOR.MINOR.PATCH),
ab V3.2.0. Frühere Tags (V3.13 usw.) folgten der alten Zählweise.
Die Version entspricht der `#define FW`-Zeichenkette in `src/tt_esp32controller.ino`.

## [V3.2.0] – in Entwicklung

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
