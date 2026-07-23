# Changelog – TWM Isolation Variac Controller

Nennenswerte Änderungen an der Controller-Firmware (ESP32-S3).
Format angelehnt an [Keep a Changelog](https://keepachangelog.com/de/1.1.0/).
Versionierung nach [Semantic Versioning](https://semver.org/lang/de/) (MAJOR.MINOR.PATCH),
ab V3.2.0. Frühere Tags (V3.13 usw.) folgten der alten Zählweise.
Die Version entspricht der `#define FW`-Zeichenkette in `src/state.h`.

## [V4.5.2] – 2026-07-23

### Behoben
- **Überlagerte Anzeige beim Kalibrier-Einstieg über die API** (GitHub-#18): Während der
  Homing-Referenzfahrt legte sich der Settings-Screen über den „Homing…"-Bildschirm.
  Ursache war ein Zeitfenster: `currentMode` wechselte bereits auf `MODE_SETTINGS`,
  während das bisherige Schutzflag erst in `homing()` selbst gesetzt wurde — dazwischen
  sah der Display-Task einen Settings-Modus ohne laufendes Homing und zeichnete darüber.
  Ein eigenes Anzeige-Flag (`homingScreenActive`) sperrt die Aktualisierung jetzt vom
  Moduswechsel bis zum fertig aufgebauten Settings-Screen. Über die Taste am Gerät trat
  der Fehler nicht auf, weil dort die RTOS-Tasks noch gar nicht laufen.

## [V4.5.1] – 2026-07-23

### Behoben
- **Dashboard zu breit auf Android** (GitHub-#16): Auf schmalen Displays ragte die
  Hauptseite über den Bildschirm hinaus und erzeugte eine waagrechte Laufleiste
  (gemessen bei 360 px Viewport: 430 px Seitenbreite). Ursache war
  `grid-template-columns: 1fr` beim Dashboard-Grid — `1fr` entspricht
  `minmax(auto, 1fr)` und lässt die Spalte nicht unter die Mindestbreite ihrer
  Inhalte schrumpfen. Mit `minmax(0, 1fr)` bleibt die Seite exakt im Viewport.
  Einstellungs- und Log-Seite waren nicht betroffen, weil sie dieses Grid nicht nutzen.
- **Live-Log unten abgeschnitten auf Mobilgeräten** (GitHub-#16): `100vh` ist dort
  grösser als der tatsächlich sichtbare Bereich (die Browserleiste zählt nicht mit) —
  zusammen mit `overflow: hidden` liessen sich die neuesten Meldungen nicht erreichen.
  Die Seite nutzt jetzt `100dvh`, `100vh` bleibt als Fallback davor stehen.

### Intern
- **Totes `#define FAN_PWM_CHANNEL` entfernt** (GitHub-#21): Seit der Umstellung auf die
  pin-basierte LEDC-API wird keine Kanalnummer mehr verwendet. Das Define lud dazu ein,
  sie versehentlich wieder in einen `ledcWrite()`-Aufruf zu setzen — genau diese
  Verwechslung von Kanal und Pin war die Ursache des ursprünglichen Lüfter-Bugs.

## [V4.5.0] – 2026-07-23

### Hinzugefügt
- **WLAN-Status als Icon auf dem Display** (Paket L, #36): Links in der Kopfzeile zeigt
  ein Symbol den Netzzustand — verbunden (`wifi`), eigener Konfigurations-AP offen
  (`wifi_find`) oder gar kein Symbol, wenn keine Verbindung besteht bzw. der AP nach
  dem Timeout abgeschaltet wurde. Neu gezeichnet wird nur bei Zustandswechsel.

### Geändert
- **Temperaturanzeige** (Paket L, #37): Statt `34.00C` jetzt `34 °C` mit vorangestelltem
  Thermometer-Symbol. Das Grad-Zeichen wird als kleiner Kreis gezeichnet, weil der
  verwendete Font (Font 2) nur ASCII 32–127 abdeckt.
- **Kopfzeile neu aufgeteilt:** Titel „ISOLATION VARIAC" jetzt zentriert, links das
  WLAN-Symbol, rechts die Temperaturgruppe. Passt eine breitere Temperaturgruppe
  (dreistelliger Wert oder `N/A`) nicht in den reservierten Bereich, entfällt das
  Thermometer-Symbol — der Messwert hat Vorrang, der Titel bleibt unversehrt.

### Intern
- Neue `src/icons.h` mit den Symbolen als XBM-Bitmaps (16 × 16 px, zusammen 96 Byte).
  Sie stammen aus Google Material Symbols (Apache 2.0) und mussten gerastert werden,
  weil TFT_eSPI keine Web-Fonts darstellen kann.

## [V4.4.0] – 2026-07-23

### Behoben
- **Sporadischer Boot-Hänger** (GitHub-#13, #11): Das Gerät startete gelegentlich
  nicht — Display schwarz, Tasten ohne Funktion, Lüfter auf 100 %. Ursache war die
  Boot-Reihenfolge: `wm.autoConnect()` lief vor der gesamten Hardware-Init. Schlug
  der WLAN-Connect fehl (nach Resets sporadisch möglich), blockierte das
  WiFiManager-Config-Portal bis zu 10 Minuten und danach folgte ein Neustart — der
  Boot kam nie bis zum Display-, Lüfter- und Task-Start. Der 4-Pin-Lüfter lief
  dabei auf Volllast, weil sein PWM-Pin vor `initFAN()` floatet.

### Geändert
- **Netzwerk läuft neben der Hardware** (GitHub-#13): WLAN, Webserver und OTA sind
  in einen eigenen `networkTask` (Core 0, 8 KB Stack) gewandert, der erst nach der
  kompletten Hardware-Init und dem Homing startet. `setup()` bringt jetzt zuerst
  LittleFS, Display, Sensorik, Lüfter, Stepper und alle Bedien-Tasks hoch — der
  Variac startet damit immer sofort, unabhängig vom WLAN.
- **Definiertes Verhalten ohne WLAN:** Ein Verbindungsversuch mit den gespeicherten
  Zugangsdaten; schlägt er fehl, wird das protokolliert und das Config-Portal
  (AP `TWM_IsolationVariac`) geöffnet. Läuft auch dieses in den Timeout, schaltet
  der `networkTask` das Funkmodul ab und beendet sich — der Variac läuft ohne
  Weboberfläche und ohne API normal weiter. Ein neuer Verbindungsversuch erfolgt
  bewusst erst nach einem Neustart (früher: `ESP.restart()` in Endlosschleife).
- **Webserver startet erst nach dem WLAN-Aufbau** — im Portal-Fall belegt der
  WiFiManager selbst Port 80, ein früher gestarteter Webserver würde kollidieren.
- **Homing-Bildschirm** zeigt die IP-Zeile nur noch, wenn eine Verbindung besteht.
  Beim Start läuft das Homing jetzt vor dem WLAN-Aufbau; die Zeile bleibt dann leer,
  statt „0.0.0.0" anzuzeigen.
- **Systemzustand umbenannt:** `STATE_WIFI_CONNECTING` heißt jetzt `STATE_STARTING` —
  er markiert seit der neuen Boot-Reihenfolge die Hardware-Startphase und nicht mehr
  den WLAN-Aufbau. Rein intern, das Blinkmuster bleibt unverändert.
- **Temperatur-Schwellen von Dashboard und Firmware angeglichen** (GitHub-#20): Das
  Dashboard warnte erst ab 70 °C und skalierte bis 90 °C, während die Firmware ab
  60 °C den Lüfter auf 100 % fährt und Alarm loggt — die Weboberfläche zeigte in
  diesem Bereich noch grün. `cfg.tWarn`/`cfg.tMax` folgen jetzt der Firmware
  (60/70 °C); führende Quelle ist `MAXFANTEMP` in `src/system.cpp`, beide Stellen
  verweisen aufeinander.

### Dokumentation
- **Status-LED-Beschreibung nachgezogen** (`documentation/Status-LED.md` und
  Geräte-Doku): Das ruhige Blinken steht jetzt für die Startphase (Hardware-Init)
  statt für den WLAN-Aufbau, und beim Config-Portal ist beschrieben, was nach dem
  10-Minuten-Timeout passiert.

## [V4.3.1] – 2026-07-19

### Behoben
- **Mobile-Layout Einstellungsseite:** Die Status-Felder im Voltmeter-Panel
  (Skalierungsfaktor/ADC-Nullpunkt) liefen auf schmalen Displays über den
  Panel-Rand hinaus — das Grid darf jetzt schrumpfen (`minmax(0,1fr)`) und
  stapelt unter 560 px einspaltig.
- **Navigation auf Mobilgeräten:** Auf iOS und Android wechselte der Klick auf
  Home-/Settings-/Log-Icon nicht zum bereits offenen Tab (mobile Browser
  unterstützen programmatischen Tab-Wechsel nicht) — Mobilgeräte navigieren
  jetzt klassisch im selben Tab; benannte Tabs bleiben ein Desktop-Feature.

## [V4.3.0] – 2026-07-19

### Dokumentation
- **Doku konsolidiert** (#24, #12, Paket I): Veraltete docx-Dokumente entfernt
  (alte REST-API-Doku V3.11, Kalibrieranleitung V3.11, Status-LED, Voltmeter-
  Designnotizen) — deren Informationsgehalt steckt jetzt vollständig in der
  Doku auf dem Gerät (`doc_usage`/`doc_settings`) bzw. in Markdown-Dokumenten
  im Repo. Status-LED-Blinkmuster neu als `documentation/Status-LED.md`.
  README und `Link-Protokoll.md` auf den aktuellen Stand gebracht.
- **Geräte-Doku vervollständigt** (Review-Nachträge): LED-Verhalten von
  Encoder- und Preset-Tasten beschrieben; Einstellungs-Doku mit vollständiger
  Feldreferenz inkl. Voltmeter-Abschnitt (Status, Kalibrierung, FW-Update)
  und ausführlichem Kapitel zur 3-Punkt-Kalibrierung. Korrekturen:
  `POST /api/config` wirkt sofort (kein Neustart nötig), Auto-Zero läuft bei
  jedem Voltmeter-Start automatisch und erfordert keine Endpunkt-Neukalibrierung.

## [V4.2.1] – 2026-07-19

### Infrastruktur
- **16-MB-Partitionierung aktiv** (#9): Die vorbereitete Tabelle wurde per
  USB-Flash aufs Gerät gebracht — LittleFS wächst von 2 MB auf ~9,9 MB,
  NVS/otadata/App-Slots blieben an identischen Offsets (Konfiguration und
  Kalibrierung haben den Umstieg nachweislich überlebt, OTA funktioniert
  weiter). Repo aufgeräumt: Layout nach `partitions.csv` konsolidiert,
  `partitions_16mb.csv` und die Umstiegs-Doku entfernt.

## [V4.2.0] – 2026-07-19

### Geändert
- **Web-Oberfläche komplett neu** (#23, Paket G): Neues, responsives Design für
  Dashboard (bisher index), Einstellungen und Live-Log auf Basis des
  Claude-Design-Entwurfs — Dark/Light-Theme und Akzentfarbe umschaltbar
  (persistiert im Browser), Schriften lokal eingebettet (offline-fähig).
  Neu dabei: Spannungsverlauf-Chart, Konfiguration sichern/wiederherstellen,
  Log-Datei-Download. Der BOOT0-Diagnose-Button entfällt.
  Auch die Doku-Seiten (Bedienung/API/Einstellungen) tragen das neue Design
  inkl. Theme-/Akzentwechsel; RapiDoc wird live aus den Design-Tokens
  eingefärbt. Das alte `style.css` ist vollständig abgelöst und entfernt.
  Navigation als **benannte Browser-Tabs**: Dashboard, Einstellungen, Live-Log
  und Doku öffnen je einen eigenen Tab — existiert er schon, wird dorthin
  gewechselt statt einen weiteren zu öffnen (Safari-kompatibel über
  `window.open(url, name)` + natives `target`; wiederholte Klicks aus demselben
  Tab wechseln ohne Neuladen).

### Hinzugefügt
- **Live-Daten über WebSocket** (#13): Statuswerte werden vom Gerät gepusht
  statt per HTTP-Polling abgefragt (Grundlage für Gauge/Trend im Dashboard).

### Behoben
- **Presets gingen bei der Endpunkt-Kalibrierung verloren:** Beim Speichern
  eines Kalibrierpunkts im Setup-Modus schrieb `saveConfiguration()` die
  Presets als 0/0/0 ins NVS (im Setup-Modus tragen die P1/P2-Objekte
  Kalibrier-Positionen statt Preset-Spannungen; der alte Code wich deshalb auf
  Nullen aus). Jetzt werden die gespeicherten Presets unverändert aus dem NVS
  übernommen. Altlast, aufgefallen beim Nachtest von GitHub-Issue #3.
- **API-Doku: Aktionen von `POST /api/command` auffindbar gemacht** (u. a.
  `enter_settings`): Die Aktionsnamen stehen jetzt im Endpoint-Titel (die
  RapiDoc-Suche durchsucht keine Parameter-Werte) plus ausführliche
  Beschreibung je Aktion.

## [V4.1.0] – 2026-07-13

### Hinzugefügt
- **Regelparameter konfigurierbar** (#31): Deadband, Dämpfung, Beruhigungszeit und
  Anfahr-Marge der Spannungsregelung sind jetzt über die Konfiguration einstellbar
  (neuer `regulation`-Block in `/api/config` mit Plausibilitätsgrenzen, Felder auf der
  Settings-Seite, Persistenz im NVS) → Tuning pro Gerät ohne Code-Änderung.
  Defaults = bisherige Werte (1,0 V / 0,8 / 150 ms / 5,0 V); ältere gespeicherte
  Configs ohne den Block laufen unverändert mit den Defaults.

### Behoben
- **Endpunkt-Kalibrierung: Homing beim Einstieg + sichere Anfahrten** (GitHub-Issue #3):
  Der Einstieg in den Setup-Modus (REG-Taste beim Einschalten wie auch
  `enter_settings` per API) führt jetzt zuerst eine **Homing-Referenzfahrt** aus.
  Bisher wurde beim Geräte-Einstieg die zufällige physische Schleifer-Position
  stillschweigend zur logischen 0 — der echte 0-V-Punkt war dann unerreichbar und
  die automatische Max-Anfahrt konnte in den mechanischen Anschlag fahren.
  Zusätzlich: P1-/P2-Anfahrten auf sichere Bereiche geklemmt, Bewegungen außerhalb
  Position 0..2000 gedrosselt, und beim Speichern eines Kalibrierpunkts prüft eine
  **Plausibilitätsprüfung** den Voltmeter-Messwert (Min: < 10 V, Max: 250–270 V
  erwartet; warnt auf Display + Live-Log, blockiert nicht). Während des Homings
  sind Stepper-Task und Display sauber pausiert; `cb_SettingsHomingAction` (machte
  nie ein Homing) heißt jetzt `cb_SettingsOnOffAction`.
- **API-Doku lud nicht** (GitHub-Issue #5): `openapi.yaml` enthielt ungültiges YAML
  (unquotierte `[V]`/`[°C]`-Einheiten in Flow-Mappings) — RapiDoc meldete
  „Unable to load the Spec". Beschreibungen gequotet, Spec parst wieder.
  Nachtrag: „TRY" schlug mit „Failed to fetch (CORS or Network Issue)" fehl, wenn
  die Seite über die IP statt `twm_variac.local` geöffnet war (absolute Server-URL
  in der Spec → Cross-Origin). Server-URL jetzt relativ (`/`) — „TRY" geht immer
  an den Host, über den die Doku-Seite geladen wurde.
- **Doku: Kalibrier-Reihenfolge ergänzt** (GitHub-Issue #4): Hinweis in der
  Einstellungs-Doku, dass das Voltmeter vor den Endpunkten kalibriert werden muss
  (sonst werden falsche Spannungs-Stützwerte gespeichert und Presets ungenau angefahren).
  Dabei zwei veraltete Stellen derselben Seite korrigiert: Konfiguration liegt im NVS
  (nicht mehr in `/config.json`), Systemparameter beschreiben jetzt die vier
  Regelparameter aus #31 statt des entfernten Grob-Anfahrt-Schwellenwerts.

### Entfernt
- Ungenutztes Konfigfeld `system.coarse_move_threshold` (Überbleibsel der alten
  PID-/Preset-Logik) aus Config, Settings-Seite und API-Schema entfernt. (#31)

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
- **16-MB-Partitionierung vorbereitet** (#9): Neue Tabelle `partitions_16mb.csv`
  (LittleFS 2 MB → ~9,9 MB; NVS/otadata/App-Slots an identischen Offsets → Konfiguration
  und Firmware überleben den Umstieg) + Umstiegs-Prozedur in
  `documentation/Partitionierung-16MB.md`. **Noch nicht aktiv** — Umschalten erfordert
  einmalig einen USB-Flash am Gerät; bis dahin bleibt `partitions.csv` in Kraft.

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
