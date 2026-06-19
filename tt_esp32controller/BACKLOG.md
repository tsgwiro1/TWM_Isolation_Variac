# Backlog – `tt_esp32controller` (+ `tt_voltmeter`)

Priorisierte Umsetzungsliste, gruppiert in Pakete. Detail-Analyse siehe [`REVIEW.md`](REVIEW.md).

**Legende**
- **Aufwand:** S = klein (Minuten–~1h), M = mittel, L = groß
- **Status:** `offen` · `in Arbeit` · `erledigt` · `verworfen`
- IDs sind stabil; neue Punkte hinten anhängen, IDs nicht wiederverwenden.

## Paket-Reihenfolge (festgelegt)

> Repo-Layout: **Mono-Repo** (`tt_esp32controller/` + `tt_voltmeter/` + gemeinsame Doku). Migration (#25) zuerst.


| Reihenfolge | Paket | Ziel | enthält IDs |
|:-:|------|------|-------------|
| **A** | Projekt-Setup: Neues Repo & Flash-Basis | Sauberer Start, zuverlässig bauen/flashen | 25, 6, 7, (8) |
| **B** | Schnelle Bugfixes & Konsistenz | Quick Wins, geringes Risiko | 1, 16, 19 |
| **C** | Regelung „snappy" (Kernanliegen) | Schnelles, präzises Anfahren ohne Pendeln | 20, 21, 18, 3, 17 (+2 erledigt sich) |
| **D** | Robustheit / Nebenläufigkeit | Stabilität unter RTOS | 4, 5 |
| **E** | API-Vereinfachung & -Bereinigung | Klare, schlanke, konsistente API | 22 |
| **F** | Struktur & Modernisierung | Wartbarkeit | 10, 15, 14, 9 |
| **G** | Web-Oberfläche: neues Design | Frisch, professionell, responsiv | 23, 13 |
| **H** | Sicherheit (optional) | Absicherung API/OTA | 11 |
| **I** | Dokumentation aktualisieren | Vollständig, auf finalem Stand | 24, 12 |

> Begründung: **A** zuerst — Migration ins saubere Repo + verlässliches Flash-/Test-Setup, danach passiert alle Arbeit dort. **B** räumt billige Fehler/Inkonsistenzen weg (inkl. #19, das in C gebraucht wird). **C** ist das Kernanliegen (Regelung) als zusammenhängender Block, mit Sim-Modus #20 als Enabler vorab. **D** härtet das Laufzeitverhalten. **E** schlankt die API, bevor **F** (Refactoring) und **G** (UI) darauf aufbauen — die UI wird gegen die *finale* API gebaut. **H** Sicherheit (laut Absprache optional). **I** Doku zuletzt, weil sie den endgültigen Stand von Code, API und UI beschreibt.

---

## Paket A — Projekt-Setup: Neues Repo & Flash-Basis

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 25 | Projekt | Migration in neues GitHub-Repo | **Mono-Repo** mit `tt_esp32controller/` + `tt_voltmeter/` + gemeinsamer Doku in ein sauberes, neues Repo überführen. Altlasten (CAD, Gehäuse, Messung, alte `libraries/` usw.) **nicht** mitnehmen; frische Struktur, `.gitignore`, Top-README. | M | offen |
| 6  | Build/Port | Upload-/Flash-Workflow | Getrennte Environments `esp32s3_usb` / `esp32s3_ota` statt Kommentar-Umschaltung; Filesystem-Upload (`buildfs`/`uploadfs`, auch via OTA) testen & dokumentieren. | M | offen |
| 7  | Build/Port | README + Flash-Anleitung | Build, USB-Erstflash, OTA-Update, Filesystem-Upload (im neuen Repo). | S | offen |
| 8  | Cleanup | Redundante `libraries/` | Alte Arduino-IDE-Libs nicht ins neue Repo übernehmen. **Geht in #25 auf** — nur relevant, falls Migration verschoben wird. | S | offen |

## Paket B — Schnelle Bugfixes & Konsistenz

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 1  | Bug | P3-Validierung | `:425` prüft `p1` statt `p3`. Korrigieren. | S | offen |
| 16 | Doc | Kommentar/Code-Drift | `communicationTask` Kommentar „>200ms" vs. Code `<1000ms` angleichen. | S | offen |
| 19 | Konsistenz | Spannungs-Limits vereinheitlichen | `MIN/MAX_VOLTAGE_TARGET` (260) vs. `maxVoltageAtMaxPos`: ein einheitliches Limit-Konzept für API/Presets/Kalibrierung. | S | offen |

## Paket C — Regelung „snappy" (Kernanliegen)

> Reihenfolge im Paket: Sim-Modus als Enabler (#20) → Messquelle entschärfen (#21) → Datenfrische (#18) → Vorsteuer-Fix (#3) → neue Regellogik (#17). #2 erledigt sich dabei.

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 20 | Test | Simulationsmodus | Build-Flag `SIM`: Voltmeter-Input durch einfaches Streckenmodell (Spannung = Modell(Position) + Lag) ersetzen, MCP/TFT stubben → Web-API & Regelung **ohne Hardware** test-/abstimmbar. Vorgezogen, weil es #17 stark vereinfacht. | M | offen |
| 21 | Voltmeter | RMS-Glättung reduzieren | In `tt_voltmeter`: EMA `ALPHA=0.1` @40 ms (≈0,4 s τ, ~0,9 s Settling) dominiert die Latenz. 2‑Zyklen‑Rohwert senden oder α erhöhen; EMA nur fürs Display. **Voraussetzung für „snappy".** | S–M | offen |
| 18 | Robustheit | Voltmeter-Datenfrische | `received_rms_value` nur nutzen, wenn frisch (Timeout auf Serial1). Betrifft Kalibrierung (`min/max_voltage` aus Messwert) **und** Regelung. | M | offen |
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

## Paket I — Dokumentation aktualisieren

| ID | Kategorie | Titel | Beschreibung | Aufwand | Status |
|----|-----------|-------|--------------|---------|--------|
| 24 | Docs | Doku aktualisieren & vervollständigen | Alle Dokumente (Kalibrierung, REST-API, Status-LED, USB CDC, TFT-Settings, Build/Flash) auf den finalen Stand bringen, Lücken schließen, einheitliche Form & Ablage. | M | offen |
| 12 | Docs | API-Doku Single-Source | API-Doku nur einmal pflegen (HTML in `data/` als Quelle vs. `.docx`), Drift vermeiden. Teil von #24. | M | offen |

---

## Notizen / weitere Kandidaten (noch nicht eingeordnet)

*(Hier sammeln wir während der Diskussion neue Punkte, bevor sie eine ID bekommen.)*
