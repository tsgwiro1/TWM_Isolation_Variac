# Backlog – `tt_esp32controller` (+ `tt_voltmeter`)

Priorisierte Umsetzungsliste, gruppiert in Pakete. Detail-Analyse siehe [`REVIEW.md`](REVIEW.md).

**Legende**
- **Aufwand:** S = klein (Minuten–~1h), M = mittel, L = groß
- **Status:** `offen` · `in Arbeit` · `erledigt` · `verworfen`
- IDs sind stabil; neue Punkte hinten anhängen, IDs nicht wiederverwenden.

## Fortschritt

**Stand:** 2026-08-06 · **Gesamt: 36 / 37 Punkte erledigt** · aktueller Release **V4.8.1**
· auf GitHub sind **alle Issues geschlossen**

| Paket | Status | Fortschritt |
|-------|--------|-------------|
| A — Projekt-Setup & Flash-Basis | ✅ erledigt | 5/5 |
| B — Schnelle Bugfixes & Konsistenz | ✅ erledigt | 3/3 |
| C — Regelung „snappy" | ✅ erledigt | 5/5 |
| D — Robustheit / Nebenläufigkeit | ✅ erledigt | 2/2 |
| E — API-Vereinfachung | ✅ erledigt | 1/1 |
| F — Struktur & Modernisierung | ✅ erledigt | 5/5 |
| K — Konfiguration ins NVS | ✅ erledigt | 1/1 |
| G — Web-Oberfläche | ✅ erledigt | 2/2 |
| H — Sicherheit (optional) | ⬜ offen | 0/1 |
| J — Voltmeter-Fernsteuerung via Controller | ✅ erledigt | 7/7 |
| I — Dokumentation | ✅ erledigt | 2/2 |
| L — LCD-Optimierung | ✅ erledigt | 2/2 |
| M — Display-Redesign | ✅ erledigt | 1/1 |

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
- [x] **E — API-Vereinfachung & -Bereinigung**
  - [x] #22 API vereinfachen & bereinigen (REST-Konventionen + OpenAPI-Spec + Doku-Seite)
- [x] **F — Struktur & Modernisierung**
  - [x] #10 Modularisierung
  - [x] #15 Typos (`Whiper`→`Wiper`, `corse`→`coarse`)
  - [x] #14 ArduinoJson v7
  - [x] #9 Partitionierung 16 MB
  - [x] #31 Regelparameter konfigurierbar
- [x] **K — Konfiguration ins NVS**
  - [x] #35 Konfiguration/Kalibrierung ins NVS (Preferences)
- [x] **G — Web-Oberfläche: neues Design**
  - [x] #23 Web-Oberfläche neu gestalten
  - [x] #13 Live-Daten über WebSocket
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
- [x] **I — Dokumentation aktualisieren**
  - [x] #24 Doku aktualisieren & vervollständigen
  - [x] #12 API-Doku Single-Source
- [x] **L — LCD-Optimierung** *(Plan: [`Paket-L-LCD-Optimierung.md`](Paket-L-LCD-Optimierung.md); weitere Punkte können jederzeit dazukommen)*
  - [x] #36 WLAN-Status als Icon auf dem TFT
  - [x] #37 Temperaturanzeige mit Icon, ohne Nachkommastellen
- [x] **M — Display-Redesign** *(Plan/Mockups: [`display-redesign/`](tt_esp32controller/documentation/display-redesign/))*
  - [x] #38 Normalbetrieb-Screen neu gestalten

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
- **2026-06-21 — Neues Paket K (#35) aufgenommen: Konfiguration ins NVS.** Analyse: `uploadfs`
  löscht die ganze LittleFS-Partition inkl. `config.json` (Kalibrierung!). Optionen abgewogen
  (NVS / eigene Config-Partition / Datei-Upload-Endpoint); Entscheidung **NVS/Preferences**
  (keine Partitionstabellen-Änderung, kein USB-Reflash-Risiko am versiegelten Gerät).
  Eingeplant **zwischen F und G**, weil das Web-Redesign (G) mehrere `uploadfs` braucht.
  Reihenfolge neu: A, B, C, D, E, F, **K**, G, H, J, I. Gesamt jetzt 22/34.
- **2026-06-21 — #35 umgesetzt (Paket K vorgezogen), Geräte-Test ausstehend.** Config-JSON
  wird als String im NVS gespeichert (Namespace `twm`, Key `config`; Schema bleibt allein
  `applyAndValidateConfig()`). Einmalige Boot-Migration: `config.json` validieren → rohen
  Inhalt 1:1 ins NVS (bewahrt Presets exakt) → Datei löschen. `GET /api/config` liefert aus
  dem NVS (inkl. `?download`), `POST` unverändert. OTA- und SIM-Build kompilieren.
- **2026-06-21 — #35 am Gerät verifiziert → Paket K abgeschlossen (1/1, gesamt 23/34).**
  Migration lief, Konfiguration/Kalibrierung überlebt `uploadfs`. Nächstes Paket: E (API).
- **2026-06-21 — #22 erweitert: REST-Konventionen + OpenAPI.** Entscheidung: API in Paket E
  entlang der REST-Konventionen neu schneiden (GET nur lesen, Aktionen auf POST/PUT — inkl.
  `fetch()`-Anpassungen in den Webseiten; einheitliche Antwort-Hülle), dabei `openapi.yaml`
  als Design-Artefakt/Doku-Quelle mitschreiben und eine interaktive Doku-Seite
  (RapiDoc/Swagger-UI) auf dem Gerät ausliefern. Löst #12 im Wesentlichen mit (dort bleibt
  nur Aufräumen der alten Doku); Aufwand #22: M → L. Umsetzung noch nicht gestartet.
- **2026-07-04 — #22 umgesetzt (Paket E), Geräte-Test ausstehend.** Entscheidungen:
  POST mit Query-String-Parametern; ungenutzte Endpoints entfernt; RapiDoc lokal.
  Umbau: alle zustandsändernden Routen GET→POST; `/data` in `/api/status` aufgegangen
  (`states.p1_on..p3_on`); `/api/presets` (GET), `/api/presets/save`, `/api/calibration` (GET)
  entfernt; `calibration/save`→`POST /api/calibration`; `files/delete`→`DELETE /api/files`.
  `script.js`/`settings.js` angepasst. **`openapi.yaml`** (komplette Spec) + **RapiDoc-Doku-Seite**
  ersetzt die alte `doc_api.html` (Dateiname bleibt → Nav-Links intakt; #12 damit im Kern
  erledigt). OTA- und SIM-Build kompilieren. **Breaking Change** für externe Skripte/Lesezeichen
  (alte GET-Aktions-URLs funktionieren nicht mehr) → **MAJOR-Bump: Controller V4.0.0**
  (V3.3.0 im CHANGELOG abgeschlossen: #4/#5/#35).
- **2026-07-04 — Paket F gestartet; #14 umgesetzt (ArduinoJson v7).** Reihenfolge in F
  festgelegt: #14 → #10+#15 (Modularisierung + Typos, Struktur mit 9 Modulen abgestimmt)
  → #31 → #9 (nur vorbereiten: neue Partitionstabelle als separate Datei, USB-Flash erst
  am offenen Gerät; aktive partitions.csv bleibt bis dahin unverändert). Geräte-Tests
  V4.0.0 laufen parallel beim Kollegen (Testanleitung-V4.0.0.md). #14: v6→v7-Idiome
  migriert, `lib_deps ^7.0.0`, OTA- und SIM-Build kompilieren, keine Verhaltensänderung.
- **2026-07-05 — #10 + #15 umgesetzt (Modularisierung + Typos).** `.ino` 3363 → 414 Zeilen
  (nur noch `setup()`/`loop()`); 11 Module (`pins`, `state`, `logging`, `config`, `motor`,
  `comm`, `display`, `actions`, `web`, `system`, `sim`), Interna gekapselt (`loggingInit()`,
  `logHistorySnapshot()`, `configRawJson()`, `initStepper()`). `Whiper`→`Wiper` (77 Stellen,
  keine API-Keys betroffen); `corse` gab es nicht mehr. Reine Verschiebung: OTA- und
  SIM-Build kompilieren, RAM/Flash praktisch identisch (+1,2 KB). Geräte-Test steht mit
  dem V4.0.0-Testlauf zusammen aus. **Version: Controller V4.1.0** (V4.0.0 im CHANGELOG
  abgeschlossen; `openapi.yaml` bleibt bewusst auf API-Version 4.0.0 — API unverändert).
  Testanleitung entsprechend aktualisiert (T1 erwartet V4.1.0). **Tag `v4.0.0`** auf dem
  V4.0.0-Stand (`71e4bdb`) gesetzt statt Feature-Branch — bei Testproblemen lässt sich der
  reine V4.0.0-Stand auschecken/flashen (Bugs API vs. Refactoring eindeutig zuordenbar).
- **2026-07-05 — #31 umgesetzt (Regelparameter konfigurierbar).** Deadband/Dämpfung/
  Beruhigungszeit/Anfahr-Marge als `regulation`-Block in der Config (Validierung mit
  Plausibilitätsgrenzen, Settings-UI, NVS); Defaults = bisherige Tuning-Werte, alte Configs
  ohne Block laufen unverändert. Ungenutztes `coarse_move_threshold` überall entfernt.
  `openapi.yaml` → API-Version 4.1.0 (Config-Schema geändert). OTA-/SIM-Build kompilieren;
  Geräte-Test steht mit dem Testlauf zusammen aus.
- **2026-07-25 — Bugfix-Runde aus dem V4.3.1-Testlauf abgeschlossen (V4.4.0 – V4.6.1);
  Testanleitung entfernt.** Die 13 GitHub-Issues des Testlaufs (#13–#25) sind umgesetzt und
  am Gerät abgenommen. Bewusst in Einzelversionen zerlegt, jede für sich getestet:
  **V4.4.0** #13/#11 Boot-Hänger — `wm.autoConnect()` lief vor der Hardware-Init, das
  Config-Portal blockierte 10 min (Display schwarz, Tasten tot, Lüfter 100 %). WLAN/Web/OTA
  laufen jetzt im `networkTask` nach dem Homing; ohne Netz: ein Versuch, dann Portal, nach
  Timeout Funk aus und Weiterbetrieb ohne Web/API (neuer Versuch nur per Neustart). Dazu
  #20 (Temperaturschwellen, führende Quelle `MAXFANTEMP`). **V4.5.0** Paket L (siehe unten).
  **V4.5.1** #16 Android-Layout (`minmax(0,1fr)`, `100dvh`) + #21 totes `FAN_PWM_CHANNEL`.
  **V4.5.2** #18 Anzeige-Race beim Kalibrier-Einstieg über die API (`homingScreenActive`).
  **V4.5.3** #14 `statusLedTask` mit Stack-Überwachung, wirkungslose Serial-Warteschleife
  entfernt. **V4.5.4** #15 Presets live über den Status-Push (auch auf der Settings-Seite,
  ohne laufende Eingaben zu überschreiben), #17 Fly-out-Menüs im Viewport, #19 Link-Fehler
  sichtbar, #22 Voltmeter-Meldungen je Box statt in einem Sammelfeld. **V4.6.0** #23 Logging
  neu: Datei entspricht wieder dem Live-Log, Drop-Meldungen raus (stiller Zähler
  `log_dropped`), gepuffertes Schreiben, Rotation nach 5000 Zeilen, verketteter Download.
  **V4.6.1** #24 Zeiger-Drehung (Animation jetzt in `script.js` statt CSS-Transition — die
  Engines interpolieren Drehungen unterschiedlich) und #25 API-Doku unter 768 px
  (`render-style="view"`, da RapiDoc seine Navigation per Container-Query ausblendet).
  Anschliessender Doku-Abgleich: neue Abschnitte „Live-Log und Logdatei" und „Anzeige am
  Gerät" in der Bedienungs-Doku (die V4.6.0-Umstellung stand bisher nur in `openapi.yaml`),
  Korrektur der Debug-Option in der Einstellungs-Doku, README/REVIEW/Paket-Pläne nachgezogen.
  `documentation/Testanleitung-V4.3.1.md` gelöscht (Zweck erfüllt; Historie behält die Datei).
  Offen aus dem Testlauf bleiben nur die Tools-Nachtests #6/#7 bei Michael.
  Commits `e7b453f` … `ac64d35`, Tags `v4.4.0`–`v4.6.1`.
- **2026-08-06 — Stand aufgeräumt: alle GitHub-Issues geschlossen, Release V4.8.1.**
  Die zuletzt noch offenen Tools-Nachtests **#6/#7** sind bei Michael erledigt; auf GitHub
  ist damit **kein Issue mehr offen** (26 gesamt, 0 offen). Dazwischen zwei Releases:
  **V4.8.0 — Display-Redesign (neues Paket M, #38).** Michaels Branch
  `feature/redesign-display` ist auf `main` gemerged, getaggt und der Remote-Branch
  gelöscht. Der Normalbetrieb-Screen ist komplett neu: Ist-Spannung gross und
  farbcodiert, **Regelabweichungsbalken** (−5…+5 V) in zwei per Langdruck auf die
  Regelungstaste umschaltbaren Varianten (persistiert als `display.variant` im NVS),
  Warndreieck für fehlende Strombegrenzung, Schalter-Chips und Presets im Taster-Look,
  eigener Font und generierte Icons. Rogers Rückmeldung zum Zwischenstand liegt als
  `tt_esp32controller/documentation/display-redesign/Feedback-Roger-2026-07-29.md` im Repo.
  **V4.8.1 — GitHub-#27:** Über Dashboard bzw. `POST /api/command?action=toggle_regulation`
  eingeschaltete Regelung schaltete nur die LED, ohne den Feedforward anzustossen (reines
  `A_reg->toggle()` ohne `isRecallPreset` → `is_regulation_active` blieb false, der
  `motorControlTask` verliess `RP_IDLE` nie); ein Lastabfall nach dem Preset blieb
  unkorrigiert. Fix: gemeinsame `toggleRegulation()` für Gerätetaste und Web/API. Am Gerät
  verifiziert (Droop 147 → 150 V, Position 1143 → 1163) und per OTA ausgerollt.
  Offen bleibt allein **Paket H** (Sicherheit, optional) — es bekäme dann **V4.9.0**.
- **2026-07-23 — V4.5.0: Paket L umgesetzt und am Gerät abgenommen (35/36).**
  #36 WLAN-Status als Icon in der Kopfzeile (verbunden / Config-AP / kein Symbol,
  Neuzeichnen nur bei Zustandswechsel) und #37 Temperatur mit Thermometer-Icon,
  ohne Nachkommastellen, mit Gradzeichen. Titel jetzt zentriert. Die Material-
  Symbols-Icons liegen als XBM in der neuen `src/icons.h` (16 × 16 px, 96 Byte,
  Apache 2.0) — TFT_eSPI kann keine Web-Fonts; das Gradzeichen wird als Kreis
  gezeichnet, weil Font 2 nur ASCII 32–127 abdeckt. Kopfzeile vorab rechnerisch
  geprüft (Titel 113 px, Temperaturgruppe ab 179 px): passt, breitere Gruppen
  lassen das Icon weg. Nachgezogen: `drawLegend()` setzt sein Text-Datum jetzt
  selbst — es erbte `TL_DATUM` von `drawBackground()`, wodurch der zentrierte
  Titel die Beschriftungen aus dem Display schob (am Gerät aufgefallen, sofort
  behoben). Commit `a72f7e2`.
- **2026-07-23 — V4.4.0: Boot-Hänger behoben (GitHub-#13/#11) + neues Paket L.**
  Aus dem Testlauf nach `Testanleitung-V4.3.1.md` kamen 11 neue GitHub-Issues
  (#13–#23); alle gegen den Code analysiert, drei davon im Browser reproduziert.
  Umgesetzt wurde zunächst nur der kritische Befund: `wm.autoConnect()` lief vor der
  gesamten Hardware-Init, ein fehlgeschlagener Connect blockierte über das
  Config-Portal bis zu 10 Minuten — Display schwarz, Tasten tot, Lüfter auf 100 %.
  WLAN/Webserver/OTA laufen jetzt im eigenen `networkTask` nach Hardware und Homing.
  Festgelegtes Verhalten ohne WLAN: ein Verbindungsversuch, dann Config-Portal, nach
  dessen Timeout Funk aus und Weiterbetrieb ohne Web/API (neuer Versuch erst nach
  Neustart). Mitgenommen: #20 (Dashboard-Temperaturschwellen folgen der Firmware,
  60/70 °C, führende Quelle `MAXFANTEMP`), `STATE_WIFI_CONNECTING` → `STATE_STARTING`,
  Status-LED-Doku nachgezogen. FW + Filesystem per OTA aufs Gerät; **am Gerät
  abgenommen** — Start ohne erreichbares WLAN verifiziert, Variac kommt sofort
  vollständig hoch. Commit `e7b453f`, Tag `v4.4.0`. **Neues Paket L (LCD-Optimierung)** aufgenommen: #36 WLAN-Icon,
  #37 Temperaturanzeige — Sammlung wächst noch, Plan in `Paket-L-LCD-Optimierung.md`.
  Offen aus dem Testlauf bleiben: #14, #15, #16, #17, #18, #19, #21, #22, #23.
- **2026-07-19 — Paket-H-Plan + Testanleitung V4.3.1 ins Repo.** `Paket-H-Plan.md`
  (Root): abgestimmter Umsetzungsplan für #11 (Blöcke A–D, ~1 Arbeitstag, offene
  Entscheidungen dokumentiert) — Umsetzung weiterhin „optional/später".
  `documentation/Testanleitung-V4.3.1.md`: Gesamtabnahme Controller V4.3.1 +
  Voltmeter V1.2.3, aufbauend T0–T15 (inkl. Tools-Retests GitHub-#6/#7 als
  T15.1/T15.2), Befunde werden mit Testnummer referenziert; Dokument ist temporär
  und wird nach bestandenem Test wieder entfernt. Zuvor: Nachträge V4.3.1
  (Mobile-Fixes Layout + Navigation, am iPhone verifiziert) committet + getaggt.
- **2026-07-19 — Paket I umgesetzt → #24 + #12 abgehakt (33/34); V4.3.0 aufs Gerät
  geflasht.** Doku konsolidiert ohne Informationsverlust: vier veraltete docx entfernt
  (REST-API V3.11, Kalibrieranleitung V3.11, Status-LED, Voltmeter-Designnotizen — das
  Messprinzip steckt vollständig kommentiert im Voltmeter-Quellcode). Vorher übernommen:
  Status-LED komplett (Blinkzeiten, Ursachen, Aktionen) in `doc_usage.html` **und** neu als
  `documentation/Status-LED.md` (gegen `statusLedTask` verifiziert; neu: auch OTA-Fehler
  setzt STATE_ERROR); Software-Kalibrierung als „Methode 2" in `doc_settings.html`;
  „PID"-Formulierung korrigiert. `Link-Protokoll.md` gegen den Code abgeglichen
  (GET→POST gemäß V4-REST, Update-Routen ergänzt, #30 als end-to-end-verifiziert
  markiert). README: drei Environments, Voltmeter-Konsole = USB-CDC/USART1 = Link,
  neuer Abschnitt „Dokumentation" mit Index. OTA-/SIM-Build ok, FW + FS per OTA aufs
  Gerät, ausgelieferte Seiten verifiziert. **Review-Nachträge (Roger):** LED-Verhalten
  dokumentiert (Encoder-Taste: an = x10; Preset-LEDs: aktiv/erlischt bei manueller
  Verstellung/Bestätigung beim Speichern — gegen den Code verifiziert);
  Einstellungs-Doku komplett: alle GUI-Felder gruppiert beschrieben + neuer Abschnitt
  „Voltmeter (serieller Link)" (Status, manuelle/3-Punkt-Kalibrierung, Gerät,
  FW-Update); Korrektur: `POST /api/config` wirkt sofort, Neustart-Schritt aus alter
  docx entfernt. API-Version bleibt per Konvention bei 4.2.0 (folgt API-Änderungen,
  nicht der FW) — Hinweis dazu in die openapi-Beschreibung aufgenommen.
  Offen nur noch Paket H (#11, optional).
- **2026-07-19 — #9 umgesetzt: 16-MB-Partitionierung am Gerät aktiv → Paket F
  komplett (31/34).** Prozedur U1–U7 durchlaufen: Konfig-Backup, USB-Flash
  (Bootloader + neue Tabelle + App), `uploadfs` (~9,9-MB-Image an 0x610000),
  Verifikation: **NVS überlebte den Umstieg** (Kalibrierung/Presets identisch),
  alle Webdateien inkl. Fonts da, **OTA funktioniert weiter**. Aufgeräumt:
  16-MB-Layout nach `partitions.csv` konsolidiert, `partitions_16mb.csv` +
  `Partitionierung-16MB.md` entfernt (Zweck erfüllt), platformio.ini bereinigt.
  Offen im BACKLOG nur noch Paket H (#11) und I (#24/#12) + Tools-Nachtests.
- **2026-07-19 — Paket G am Gerät abgenommen → #23 + #13 abgehakt (30/34); V4.2.0
  abgeschlossen + Tag `v4.2.0`.** Roger hat das komplette Redesign am Gerät
  durchgetestet (Dashboard mit WS-Livedaten, Settings inkl. Kalibrierung/Presets/
  Backup, Live-Log, Doku-Seiten mit Theme/Akzent, RapiDoc). Nachträge während des
  Tests: benannte Browser-Tabs (ein Tab je Seite, Safari-kompatibel), Preset-
  Bugfix, API-Doku-Auffindbarkeit. Tags `v4.0.0`/`v4.1.0` liegen verifiziert auf
  GitHub. Offen bleiben: Tools-Nachtests GitHub-#6/#7, BACKLOG-#9 (USB-Flash),
  dann Pakete H (#11) und I (#24/#12-Rest).
- **2026-07-14 — GitHub-#3 am Gerät verifiziert (Roger); Preset-Bugfix bestätigt.**
  Kalibrier-Ablauf funktioniert wie gebaut (Setup-Einstieg → Homing → Anfahrten →
  Plausibilitätswarnung); Issue wird geschlossen. Dabei entdeckte Altlast behoben
  und nachgetestet: `saveConfiguration()` nullte beim Speichern eines
  Kalibrierpunkts im Setup-Modus die Presets (P1/P2 tragen dort Positionen statt
  Spannungen; Commit `01d93e2` übernimmt die Presets jetzt aus dem NVS —
  Presets überleben die Kalibrierung). Außerdem `POST /api/command`-Aktionen
  (u. a. `enter_settings`) in der API-Doku sichtbar/suchbar gemacht (`4d39b0c`).
  Offene Nachtests damit nur noch: Tools GitHub-#6/#7 (inkl. `.ps1` auf Windows).
- **2026-07-13 — Paket G gestartet: V4.2.0 + Tag `v4.1.0`; #23 + #13 umgesetzt
  (Gerätetest ausstehend).** Grundlage ist Rogers Claude-Design-Entwurf
  („ESP32 Variac design review"); entschieden: Vanilla-Nachbau statt
  React-Bundle, Fonts lokal (WOFF2, ~53 KB, offline-fähig), kein BOOT0-Button
  mehr, Log-Download neu, Akzentfarbe in der UI umschaltbar (Default teal).
  **#23:** Dashboard/Settings/Log komplett neu (app.css/app.js als gemeinsames
  Design-System, Dark/Light + Akzent persistiert, Gauge + Spannungsverlauf-Chart,
  Konfig-Backup/Restore, Voltmeter-Panel inkl. 2-Schritt-FW-Update).
  **#13:** Neuer WebSocket `/ws_status` pusht den Status alle 500 ms
  (eine JSON-Quelle mit `GET /api/status`); Dashboard nutzt ihn mit
  automatischem Polling-Fallback. Auch die **doc_*-Seiten** aufs Design-System
  gezogen (Theme/Akzent umschaltbar; RapiDoc wird per JS live aus den
  CSS-Tokens eingefärbt und folgt dem Theme-Wechsel); das alte `style.css` ist
  damit vollständig abgelöst und entfernt. Alle Seiten gegen einen
  Mock-Controller (inkl. WebSocket) im Browser verifiziert; beide Firmware-Envs
  bauen. Offen: Gerätetest.
- **2026-07-13 — #22 abgehakt → Paket E fertig (28/34); Testanleitung entfernt.**
  Michaels Nachtest der API-Doku ist grün: RapiDoc lädt (YAML-Fix), TRY funktioniert
  auch bei Zugriff per IP (relative Server-URL), und die Voltmeter-Endpoints liefern
  Live-JSON — damit ist auch das Backend hinter GitHub-#9 verifiziert; das Issue wird
  geschlossen („wird durch Web-Redesign Paket G ersetzt", altes Frontend wird nicht
  mehr gefixt). #12 ist damit im Kern miterledigt (openapi.yaml als Single Source,
  Doku-Seite auf dem Gerät); dort verbleibt nur das Abräumen der Alt-Doku (Teil #24).
  `Testanleitung-V4.0.0.md` gelöscht (Zweck erfüllt; Historie behält die Datei).
  **Noch offene Nachtests (unkritisch, Befunde werden unabhängig gefixt):**
  Kalibrier-Ablauf GitHub-#3 (Setup-Einstieg → Homing → Anfahrten → Plausibilitäts-
  warnung) sowie die Tools-Retests GitHub-#6/#7 (Endwert-Anzeige, Limit-Wahl).
- **2026-07-12 — Testlauf T1–T3a/T6/T8 bestanden → #10/#15/#14/#31 abgehakt (27/34).**
  Rogers Freigabe nach Michaels Testlauf: Version/Konfig-Erhalt, Hauptseite,
  Settings inkl. Regelparameter, Breaking-Change-Negativtest und Regression sind
  grün — damit sind die Paket-F-Punkte am Gerät verifiziert (F offen nur noch
  wegen #9/USB-Flash). **#22 (Paket E) bleibt offen:** seine Abnahmetests T4
  (Voltmeter-Panel, GitHub-#9 ungeklärt) und T5 (RapiDoc, Fix committet aber
  nicht nachgetestet) sind genau die offenen Befunde; T7 (Tools) zurückgestellt.
  Testanleitung bleibt im Repo, bis T4/T5/T7 nachgetestet sind.
- **2026-07-12 — GitHub-#3 behoben (Kalibrier-Einstieg).** Wurzelursache: Der
  Setup-Einstieg per REG-Taste beim Einschalten übersprang das Homing — die
  zufällige physische Schleifer-Position wurde zur logischen 0 (0-V-Punkt
  unerreichbar, Max-Anfahrt in den Anschlag). Fix nach Rogers Vorgaben: Homing
  bei jedem Kalibrier-Einstieg (Gerät + API, mit Stepper-/Display-Pause),
  P1/P2-Anfahrten geklemmt und außerhalb 0..2000 gedrosselt, Plausibilitäts-
  warnung beim Speichern (Min > 10 V bzw. Max außerhalb 250–270 V → Warnung auf
  Display/Log, Speichern nie blockiert; Min-Spannung bleibt gemessen, typisch
  3–8 V). Doku (Kalibrierprozess) + API-Spec nachgezogen. Beide Envs bauen;
  Gerätetest ausstehend.
- **2026-07-12 — Testlauf-Befunde (GitHub-Issues 3–9): #5 + #4 behoben.** Michaels
  Testlauf lieferte 7 Issues. Behoben: GitHub-#5 (API-Doku „Unable to load the Spec" —
  `openapi.yaml` enthielt ungültiges YAML, unquotierte `[V]`/`[°C]` in Flow-Mappings;
  gequotet + Parser-Check) und GitHub-#4 (Doku-Hinweis: Voltmeter **vor** den Endpunkten
  kalibrieren, sonst falsche Spannungs-Stützwerte → Presets ungenau). Offen: GitHub-#9
  (Voltmeter-Panel ohne Reaktion — Rückfragen an Michael gestellt), GitHub-#3
  (Kalibrier-UX Min-Fenster/Auto-Anfahrt — Design-Entscheid ausstehend), GitHub-#6/7/8
  (Tools, zurückgestellt). Achtung: GitHub-Issue-Nummern ≠ BACKLOG-IDs.
- **2026-07-05 — #9 vorbereitet (16-MB-Partitionierung) → Paket F code-seitig komplett.**
  `partitions_16mb.csv` (nur FS wächst auf ~9,9 MB, alle anderen Offsets identisch →
  NVS-Konfig und Firmware überleben den Umstieg) + `Partitionierung-16MB.md`
  (Warum/Prozedur/Rollback) + auskommentierte Umschaltzeile in `platformio.ini`.
  Aktive `partitions.csv` unverändert — OTA/`uploadfs` bauen weiter gegen das reale
  Gerätelayout. USB-Flash erfolgt, wenn das Gerät das nächste Mal zugänglich ist.
  Damit ist Paket F implementiert (#14/#10/#15/#31 warten auf den Geräte-Testlauf,
  #9 auf den USB-Flash).
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
| **K** | Konfiguration ins NVS | Config/Kalibrierung überlebt `uploadfs`/OTA | 35 |
| **G** | Web-Oberfläche: neues Design | Frisch, professionell, responsiv | 23, 13 |
| **H** | Sicherheit (optional) | Absicherung API/OTA | 11 |
| **J** | Voltmeter-Fernsteuerung via Controller | Menü/Kalibrierung/Status des Voltmeters über Web/API | 27, 28, 29, 30 |
| **I** | Dokumentation aktualisieren | Vollständig, auf finalem Stand | 24, 12 |
| **L** | LCD-Optimierung | Anzeige auf dem Gerät verbessern | 36, 37 |
| **M** | Display-Redesign | Normalbetrieb-Screen grafisch neu aufbauen | 38 |

> Begründung: **A** zuerst — Migration ins saubere Repo + verlässliches Flash-/Test-Setup, danach passiert alle Arbeit dort. **B** räumt billige Fehler/Inkonsistenzen weg (inkl. #19, das in C gebraucht wird). **C** ist das Kernanliegen (Regelung) als zusammenhängender Block, mit Sim-Modus #20 als Enabler vorab. **D** härtet das Laufzeitverhalten. **E** schlankt die API, bevor **F** (Refactoring) und **G** (UI) darauf aufbauen — die UI wird gegen die *finale* API gebaut. **H** Sicherheit (laut Absprache optional). **J** (nachträglich aufgenommen) baut die Voltmeter-Fernsteuerung auf der finalen API/UI auf, kommt daher nach **G/H**. **I** Doku zuletzt, weil sie den endgültigen Stand von Code, API und UI beschreibt. *(Buchstabe J liegt vor I, weil später ergänzt — die Reihenfolge richtet sich nach der Tabelle, nicht nach dem Alphabet.)* **K** (Config ins NVS, später ergänzt) liegt bewusst **vor G**: das Web-Redesign braucht mehrere `uploadfs`, und ab K überlebt die Konfiguration/Kalibrierung diese.

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

> Erweitert (2026-06-21): API wird entlang der **REST-Konventionen** neu geschnitten und mit
> einer **OpenAPI-3-Spec** (`openapi.yaml` im Repo, Single Source of Truth → löst #12) plus
> **interaktiver Doku-Seite** auf dem Gerät dokumentiert. Hinweis: OpenAPI beschreibt/
> dokumentiert nur (handgepflegt, keine Codegen/Validierung auf dem ESP32) — das Design
> machen die REST-Konventionen.

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 22 | API | API vereinfachen & bereinigen (REST + OpenAPI) | (a) Redundante/wenig sinnvolle Endpoints zusammenführen oder entfernen (z. B. Überschneidung `/data` ↔ `/api/status`; Nutzen von `/api/files`, `/api/files/delete` prüfen). (b) **REST-Konventionen**: GET nur lesen, zustandsändernde Aktionen auf POST/PUT umstellen (betrifft u. a. `/api/reboot`, `/api/calibration/save`, `/api/voltmeter/*` — inkl. Anpassung aller `fetch()`-Aufrufe in den Webseiten), ressourcen-orientierte Pfade, einheitliche Antwort-Hülle `{status, message, …}` + Fehlercodes. (c) **`openapi.yaml`** parallel zum Umbau schreiben (Design-Artefakt + Doku-Quelle). (d) **Interaktive Doku-Seite** auf dem Gerät: kleine HTML-Seite + RapiDoc/Swagger-UI (per CDN oder lokal, 16 MB Flash vorhanden), Gerät serviert die `openapi.yaml`. Basis für #13/#23; zahlt auf #11 (CSRF/REST-Hygiene) ein. | L | **erledigt** *(am Gerät verifiziert: Web/Tools-Testlauf + RapiDoc-TRY inkl. Voltmeter-Endpoints)* |

## Paket F — Struktur & Modernisierung

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 10 | Refactor | Modularisierung | `.ino` in Module aufteilen (`config`, `web`, `display`, `motor`, `comm`, `actions`, `logging`); ggf. `.ino`→`.cpp` + Forward-Declarations. | L | **erledigt** *(am Gerät verifiziert, Testlauf T1–T3a/T8)* |
| 15 | Cosmetic | Typos | `Whiper`→`Wiper`, `CORSE`/`corse`→`coarse` konsistent umbenennen (zusammen mit #10, da gleiche Dateien). | S | **erledigt** *(am Gerät verifiziert)* |
| 14 | Refactor | ArduinoJson v7 | Migration von `StaticJsonDocument` auf v7-API. | M | **erledigt** *(am Gerät verifiziert)* |
| 9  | Build | Partitionierung 16 MB | Neue Tabelle `partitions_16mb.csv`: LittleFS 2 MB → ~9,9 MB, NVS/otadata/App-Slots an identischen Offsets (Konfig + Firmware überleben den Umstieg). Label bleibt bewusst `spiffs` (dasselbe Binary muss mit alter UND neuer Tabelle laufen) — stattdessen Kommentar in csv/Code. Umstiegs-/Rollback-Prozedur: `documentation/Partitionierung-16MB.md`. | M | **erledigt** *(2026-07-19 per USB geflasht; NVS überlebte, OTA verifiziert; Tabelle nach `partitions.csv` konsolidiert)* |
| 31 | Konfig | Regelparameter konfigurierbar | Regelparameter (`REG_FEEDFORWARD_UNDERSHOOT_V`, Damping, Deadband, Settle) statt `#define` über config.json/Settings einstellbar machen → Tuning pro Gerät ohne Code-Änderung (z. B. richtungsabhängige Hysterese). Dabei ungenutztes `coarse_move_threshold` aufräumen/umwidmen. | M | **erledigt** *(am Gerät verifiziert, Testlauf T3a)* |

## Paket K — Konfiguration ins NVS

> Ziel: Konfiguration + Kalibrierung überleben jedes `uploadfs` (Webseiten-Update) und jede
> App-OTA. Hintergrund: `uploadfs` schreibt die gesamte LittleFS-Partition neu → `config.json`
> (inkl. Kalibrierung) geht verloren. Entscheidung: **Option 1 (NVS/Preferences)** — eigene,
> von OTA/`uploadfs` unberührte Partition, kein Eingriff in die Partitionstabelle nötig.
> Bewusst **vor Paket G** eingeplant, weil das Web-Redesign mehrere `uploadfs` braucht.

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 35 | Konfig | Konfiguration/Kalibrierung ins NVS (Preferences) | Config-JSON-String im NVS (Namespace `twm`, Key `config`); Schema bleibt allein `applyAndValidateConfig()`. Einmalige Boot-Migration: `config.json` validieren → roh 1:1 ins NVS → Datei löschen. `GET /api/config` (inkl. `?download`) aus dem NVS, `POST` unverändert. Hinweis: falls #9 (Partitionierung) später die Tabelle ändert, NVS-Offset beibehalten, damit die Werte überleben. | M | **erledigt** *(am Gerät verifiziert: Migration + Überleben von `uploadfs`)* |

## Paket G — Web-Oberfläche: neues Design

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 23 | Frontend | Web-Oberfläche neu gestalten | Frisches, professionelles, responsives Design (index/settings/log/doc): konsistentes Styling, klare Bedienung, gegen die bereinigte API (#22). | L | **erledigt** *(am Gerät verifiziert; V4.2.0)* |
| 13 | Frontend | Live-Daten über WebSocket | 2-s-Polling von `/data` durch WS-Push ersetzen (weniger Last, flüssigere Anzeige). Teil des Redesigns. | M | **erledigt** *(am Gerät verifiziert; `/ws_status`, V4.2.0)* |

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
| 24 | Docs | Doku aktualisieren & vervollständigen | Alle Dokumente (Kalibrierung, REST-API, Status-LED, USB CDC, TFT-Settings, Build/Flash) auf den finalen Stand bringen, Lücken schließen, einheitliche Form & Ablage. | M | **erledigt** |
| 12 | Docs | API-Doku Single-Source | Wird im Wesentlichen durch #22 gelöst: `openapi.yaml` als Single Source of Truth + Doku-Seite auf dem Gerät. Hier verbleibt nur: alte HTML-/docx-API-Doku entfernen bzw. auf die neue Seite verweisen. Teil von #24. | S | **erledigt** |

---

## Paket L — LCD-Optimierung

> Sammelpaket, **abgeschlossen**: Die weitergehende Überarbeitung der Anzeige ist in
> Paket M (Display-Redesign, V4.8.0) aufgegangen. Details und technischer Weg im Plan
> [`Paket-L-LCD-Optimierung.md`](Paket-L-LCD-Optimierung.md).

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 36 | Display | WLAN-Status als Icon auf dem TFT | Oben links den WLAN-Zustand als Icon zeigen: verbunden → Material Symbol `wifi`, eigener Config-AP aktiv → `wifi_find`, kein Netz (AP beendet) → Icon entfernen. Zustände sind seit V4.4.0 über `currentSystemState`/`WiFi.status()` unterscheidbar. Icons müssen als Bitmap (XBM) in die Firmware, da TFT_eSPI keine Web-Fonts kann. Kopfzeilen-Layout: Icon links, Titel neu zentriert, Temperatur rechts — Feinjustierung am Gerät. | S | **erledigt** *(V4.5.0, am Gerät verifiziert)* |
| 37 | Display | Temperaturanzeige mit Icon, ohne Nachkommastellen | Statt `34.00C` neu `34 °C` mit vorangestelltem Material Symbol `device_thermostat`. Offen: ob der TFT-Font das Grad-Zeichen enthält, sonst Kreis zeichnen. | S | **erledigt** *(V4.5.0; Font 2 kann kein `°`, daher als Kreis gezeichnet)* |

---

## Paket M — Display-Redesign

> Umgesetzt auf dem Branch `feature/redesign-display` (Michael Tanner), gemerged und als
> **V4.8.0** getaggt. Layout-Entwürfe, Mockups und Rückmeldung liegen in
> [`tt_esp32controller/documentation/display-redesign/`](tt_esp32controller/documentation/display-redesign/).

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 38 | Display | Normalbetrieb-Screen neu gestalten | Grafischer Aufbau statt Textliste: Ist-Spannung gross und nach Sicherheitszustand farbcodiert, **Regelabweichungsbalken** (−5…+5 V) in zwei zur Laufzeit umschaltbaren Varianten (Langdruck auf die Regelungstaste, persistiert als `display.variant` im NVS), Warndreieck bei fehlender Strombegrenzung, Schalter-Chips (Ausgang/Limit/Regelung) und Presets im Look der Frontplatten-Taster, eigener Font und generierte Icons. Bewusst keine Anlehnung ans Web-UI und keine Gauge (Zeigerinstrumente sitzen auf der Frontplatte). | L | **erledigt** *(V4.8.0, am Gerät iteriert)* |

---

## Notizen / weitere Kandidaten (noch nicht eingeordnet)

*(Hier sammeln wir während der Diskussion neue Punkte, bevor sie eine ID bekommen.)*
