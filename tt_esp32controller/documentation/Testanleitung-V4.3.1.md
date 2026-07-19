# Testanleitung — Gesamtabnahme Controller V4.3.1 + Voltmeter V1.2.3

**Stand:** 2026-07-19 · **Prüflinge:** ESP32-Controller **V4.3.1**, STM32-Voltmeter **V1.2.3**
· **Dauer:** ca. 2–3 h (T15 Tools separat ~30 min)

> **Temporäres Dokument** — wird nach bestandenem Test wieder aus dem Repo entfernt
> (Ergebnis wandert ins BACKLOG-Protokoll; die Historie behält die Datei).
>
> **Gefundene Punkte bitte immer mit Testnummer melden** (z. B. „T9.4: Offset wird
> nicht übernommen") — als GitHub-Issue oder direkt. So ist die Zuordnung eindeutig.
>
> Die Tests bauen aufeinander auf: erst Grundzustand, dann Gerätebedienung, dann
> Web/Mobile, dann Kalibrierung/Voltmeter, zuletzt Updates und Tools. In der
> Reihenfolge durcharbeiten.

## ⚠️ Sicherheit

Das Gerät schaltet **Netzspannung** — es gelten die üblichen Regeln:

- Tests, die den **Ausgang einschalten**, nur im gewohnten Prüfaufbau und ohne
  angeschlossene empfindliche Last durchführen.
- Strombegrenzung eingeschaltet lassen (außer wo explizit anders nötig).
- Bei unerwartetem Verhalten: Ausgang aus (Gerätetaste ON/OFF).

---

## T0 — Vorbereitung

1. Repo auf den aktuellen Stand bringen (`git pull`), kein Flashen nötig —
   das Gerät läuft bereits auf V4.3.1.
2. Browser: Geräteseiten einmal **hart neu laden** (Mac: Cmd+Shift+R, Windows:
   Ctrl+F5, iPhone: Seite aus dem Tab-Speicher entfernen und neu öffnen) —
   sonst kommt altes JavaScript/CSS aus dem Cache.

- [ ] T0.1 Dashboard erreichbar unter `http://twm_variac.local/`
- [ ] T0.2 Einstellungsseite (Fußzeile) zeigt **Firmware V4.3.1**

## T1 — Boot, Status-LED, Grundzustand

1. Gerät aus- und wieder einschalten, Bootvorgang beobachten.
2. Status-LED nach dem Hochfahren beobachten (Referenz: Doku „Bedienung",
   Abschnitt Status-LED, bzw. `documentation/Status-LED.md`).

- [ ] T1.1 Gerät bootet ohne Fehlermeldung, Display zeigt Normalanzeige
- [ ] T1.2 Während des WLAN-Verbindens: gleichmäßiges Blinken (0,5 s/0,5 s)
- [ ] T1.3 Danach dauerhaft „Herzschlag" (kurzer Puls, lange Pause)
- [ ] T1.4 Einstellungsseite → Voltmeter → „Status aktualisieren":
      Installierte Version = **V1.2.3**

## T2 — Bedienung am Gerät (Tasten, Encoder, LEDs)

1. **Encoder drücken:** Geschwindigkeit wechselt Fein (x1) ↔ Grob (x10).
2. **Encoder drehen:** Soll-Spannung ändert sich in beiden Geschwindigkeiten.
3. **ON/OFF, LIMIT, REG:** je einmal schalten, Wirkung + Tasten-LED prüfen.
4. **P1 kurz:** Preset wird angefahren. **Danach Encoder drehen.**
5. **P2 lang (~2 s)** bei laufendem Voltmeter: aktuelle Ist-Spannung wird gespeichert
   (danach den alten Wert über die Einstellungsseite wiederherstellen).

- [ ] T2.1 Encoder-LED: **an = Grob (x10), aus = Fein (x1)**
- [ ] T2.2 x10 verstellt sichtbar schneller als x1
- [ ] T2.3 ON/OFF, LIMIT, REG schalten und die Tasten-LEDs folgen dem Zustand
- [ ] T2.4 P1 kurz: Sollwert = Preset-Spannung, **P1-LED leuchtet**, andere Preset-LEDs aus
- [ ] T2.5 Encoder-Drehung nach Preset-Abruf: **P1-LED erlischt**
- [ ] T2.6 P2 lang: Bestätigung über LED, neuer Wert erscheint auf der Einstellungsseite

## T3 — Spannungsregelung

1. REG einschalten, per Web-Dashboard Sollwert setzen (z. B. 100 V), Ausgang ein.
2. Anfahrt beobachten (schnelle Vorsteuerung, dann Feinkorrektur).
3. Sollwert-Sprünge testen: hoch (z. B. 100→200 V) und runter (200→50 V).
4. Mit dem Encoder manuell verstellen, dann loslassen.

- [ ] T3.1 Zielspannung wird zügig und ohne starkes Überschwingen erreicht
- [ ] T3.2 Endwert liegt innerhalb des Deadbands (Standard ±1 V)
- [ ] T3.3 Sprünge in beide Richtungen funktionieren
- [ ] T3.4 Nach Encoder-Verstellung: ~1 s nach der letzten Drehung wird die erreichte
      Spannung als Sollwert übernommen und gehalten (REG an)

## T4 — Dashboard (Desktop-Browser)

1. Dashboard öffnen, Werte und Bedienelemente prüfen.
2. Ausgang/Limit/Regelung über die Web-Buttons schalten; parallel am Gerät beobachten.
3. Trend-Chart: Zeitfenster umschalten (30 s … 10 min).

- [ ] T4.1 Ist-/Soll-Spannung, Temperatur, Position aktualisieren sich **flüssig**
      (WebSocket-Push ~0,5 s — kein 2-s-Polling-Ruckeln)
- [ ] T4.2 Gauge und Trend-Chart plausibel; Zeitfenster-Umschaltung wirkt
- [ ] T4.3 Web-Buttons wirken sofort; Zustands-Chips stimmen mit den Geräte-LEDs überein
- [ ] T4.4 Sollwert-Eingabefeld: Wert setzen funktioniert, Preset-Buttons fahren an
- [ ] T4.5 Umgekehrt: Tastendruck am Gerät erscheint ohne Reload im Dashboard

## T5 — Benannte Browser-Tabs (Desktop)

1. Im Desktop-Browser (Safari **und** Chrome oder Edge): vom Dashboard aus
   Einstellungen, Live-Log und Doku über die Header-Icons öffnen.
2. Icons mehrfach klicken, auch kreuzweise (Dashboard ↔ Einstellungen ↔ Log).

- [ ] T5.1 Jede Seite öffnet genau **einen** Tab; wiederholte Klicks wechseln zum
      vorhandenen Tab statt neue zu öffnen
- [ ] T5.2 Verhalten in Safari und Chrome/Edge identisch

## T6 — Mobile (iPhone, wenn möglich zusätzlich Android)

1. Einstellungsseite auf dem Handy öffnen, zum Abschnitt „Voltmeter" scrollen.
2. Header-Icons benutzen: Dashboard → Einstellungen → Log → zurück.

- [ ] T6.1 Voltmeter-Status-Felder (Skalierungsfaktor, ADC-Nullpunkt) bleiben
      **innerhalb des Panels** (einspaltig gestapelt, nichts läuft über den Rand)
- [ ] T6.2 Navigation wechselt **im selben Tab** — jeder Klick wirkt, keine toten Klicks
- [ ] T6.3 (falls Android-Gerät vorhanden) T6.1 + T6.2 auch dort

## T7 — Einstellungsseite: Konfiguration

1. Einen Regelparameter leicht ändern (z. B. Deadband 1.0 → 1.5) → **Speichern &
   Anwenden** → Seite neu laden → Wert zurückstellen → Speichern.
2. Validierung: Deadband testweise auf 50 setzen → Speichern.
3. Ein Preset um 1 V ändern → Speichern → am Gerät per Taste abrufen → zurückstellen.
4. **Konfig. herunterladen** → JSON-Datei prüfen (alle Blöcke: system, regulation,
   calibration, presets).
5. Einen Wert ändern und speichern, dann die heruntergeladene Datei per
   **Konfig. hochladen** wiederherstellen.
6. **Neustart**-Button.

- [ ] T7.1 Speichern meldet Erfolg („Werte sind aktiv"), Werte überleben den Reload
- [ ] T7.2 Ungültiger Wert wird mit Validierungsfehler abgelehnt, nichts übernommen
- [ ] T7.3 Preset-Änderung wirkt am Gerät
- [ ] T7.4 Download liefert vollständige JSON
- [ ] T7.5 Upload stellt den alten Stand wieder her (sofort sichtbar)
- [ ] T7.6 Neustart läuft durch, Webseite fängt den Reboot ab und verbindet neu

## T8 — Endpunkt-Kalibrierung (Variac)

> Referenz: Doku „Einstellungen", Kapitel Kalibrierungsprozess. Vorher aktuelle
> Kalibrierwerte notieren oder Konfig-Backup aus T7.4 bereithalten.

1. **Methode 1:** Gerät ausschalten, **REG gedrückt halten** und einschalten →
   Setup-Modus.
2. Ablauf beobachten: Homing auf den Endschalter, dann automatische Anfahrt des
   gespeicherten Min-Punkts (P1 vorgewählt).
3. Min-Punkt mit Encoder minimal nachjustieren → **P1 lang** → speichern.
4. **P2 kurz** → Anfahrt Max-Punkt (oberhalb Position 2000 hörbar langsamer) →
   **P2 lang** → speichern.
5. Neustart (Web oder aus/ein) → Normalbetrieb.
6. **Methode 2:** Auf der Einstellungsseite einen Kalibrierwert um eine Winzigkeit
   ändern → Speichern → zurückstellen → Speichern.
7. Optional (Plausibilitätswarnung): im Setup-Modus P1 an einer Position weit oberhalb
   des 0-V-Punkts lang drücken → Warnung auf Display + im Live-Log → Wert danach
   korrekt neu kalibrieren.

- [ ] T8.1 Setup-Einstieg per REG-Taste funktioniert, Display zeigt „SETUP"
- [ ] T8.2 Homing läuft automatisch, danach Anfahrt des Min-Punkts
- [ ] T8.3 Beide Punkte lassen sich speichern (gemessene Spannung wird übernommen)
- [ ] T8.4 **Presets sind nach der Kalibrierung unverändert** (Regression Preset-Bugfix)
- [ ] T8.5 Nach Neustart: Normalbetrieb, Regelung nutzt die neuen Werte
- [ ] T8.6 Methode 2: Direkteingabe wird ohne Neustart aktiv
- [ ] T8.7 (optional) Plausibilitätswarnung erscheint bei unplausiblem Messwert

## T9 — Voltmeter-Panel (Einstellungsseite)

> Referenz: Doku „Einstellungen", Abschnitt Voltmeter + Kapitel 3-Punkt-Kalibrierung.
> Für T9.4 wird ein True-RMS-Multimeter am Ausgang benötigt.
> **Achtung:** Nach T9.4 (3-Punkt) müssen die Endpunkte neu kalibriert werden (T8) —
> T9.4 deshalb nur zusammen mit einem echten Kalibrierdurchgang ausführen oder auslassen.

1. **Status aktualisieren** → vier Werte erscheinen.
2. **Faktor** auf den aktuell angezeigten Wert „neu setzen" (keine echte Änderung) →
   Erfolgsmeldung. Danach einen Wert **außerhalb 100–1000** versuchen.
3. **Offset** analog: aktuellen Wert neu setzen; dann einen Wert außerhalb ±50 V versuchen.
4. **Auto-Zero** auslösen (bei laufender Regelung beobachten).
5. Optional: **3-Punkt-Kalibrierung** komplett nach Doku-Kapitel (z. B. 50/125/230 V,
   Werte vom Multimeter eintragen, je „Messen", dann „Kalibrierung abschließen").
6. **Voltmeter-Neustart** auslösen.

- [ ] T9.1 Status zeigt Version, Faktor, Offset, ADC-Nullpunkt
- [ ] T9.2 Gültiger Faktor/Offset: Erfolgsmeldung; ungültige Werte werden **abgelehnt**
- [ ] T9.3 Auto-Zero: dauert ~5 s, Messwert-Stream pausiert, Regelung hält an und
      läuft danach normal weiter; ADC-Nullpunkt danach plausibel (~2000–2100)
- [ ] T9.4 (optional) 3-Punkt: Anzeige stimmt danach mit dem Multimeter überein
      (± ~0,5 V über den Bereich)
- [ ] T9.5 VM-Neustart: Stream setzt automatisch wieder ein, Anzeige läuft weiter

## T10 — Voltmeter-Firmware-Update über den Controller

> Testet den kompletten AN3155-Pfad. Als Update-Datei die **aktuelle V1.2.3-.bin**
> verwenden (Re-Flash derselben Version = risikoarm).
> Build: `cd tt_voltmeter && pio run` → `.pio/build/.../firmware.bin`.

1. Einstellungen → Voltmeter → Firmware-Update, Schritt 1: Datei wählen → Hochladen.
2. Angezeigte Datei-Version prüfen.
3. Schritt 2: **Update starten**; Gerät (LCD) und Web-Fortschritt beobachten.
4. Nach Abschluss: Status aktualisieren.

- [ ] T10.1 Upload ok, „Version auf Controller" zeigt **V1.2.3** (aus der .bin gelesen)
- [ ] T10.2 Während des Flashens: LCD zeigt Update-Screen mit Fortschritt, Bedienung
      gesperrt, **Ausgang aus**; Web zeigt Fortschritt
- [ ] T10.3 Danach: Voltmeter meldet sich mit V1.2.3, Stream läuft, **Kalibrierwerte
      (Faktor/Offset) unverändert** (EEPROM überlebt das Update)
- [ ] T10.4 Ausgang bleibt nach dem Update **aus** (kein Auto-Einschalten)

## T11 — Live-Log

1. Log-Seite öffnen; parallel am Gerät etwas auslösen (Taste drücken, Preset abrufen).
2. Seite neu laden.
3. **Log herunterladen**.

- [ ] T11.1 Einträge erscheinen live (ohne Reload), Level (INFO/WARN/ERROR) farblich
      unterschieden, Zeitspalte plausibel
- [ ] T11.2 Nach Reload: Historie ist sofort da (RAM-Puffer wird beim Verbinden gesendet)
- [ ] T11.3 Download liefert die Logdatei

## T12 — API & API-Doku

1. Doku „API-Referenz" öffnen (RapiDoc).
2. In der Suche `enter_settings` eingeben.
3. TRY: `GET /api/status` ausführen.
4. Negativtest REST-Konventionen: `http://twm_variac.local/api/command?action=toggle_output`
   direkt als Browser-URL aufrufen (= GET).
5. Kopfbereich der Doku lesen.

- [ ] T12.1 RapiDoc lädt, Farben folgen Theme/Akzent der übrigen Seiten
- [ ] T12.2 Suche findet den Befehl (Aktionen stehen im Endpunkt-Titel)
- [ ] T12.3 TRY liefert JSON mit `fw_version` „Firmware V4.3.1"
- [ ] T12.4 GET auf die Aktions-URL schaltet **nichts** (Aktionen nur per POST)
- [ ] T12.5 Versionierungs-Hinweis sichtbar (API-Version 4.2.0 ≠ FW-Version, gewollt)

## T13 — Doku-Seiten (Inhalte nach Paket I)

1. Alle drei Doku-Seiten durchsehen: Bedienung, API, Einstellungen.
2. Theme (Dark/Light) und Akzentfarbe im Doku-Header umschalten.

- [ ] T13.1 „Bedienung": LED-Verhalten von Encoder- und Preset-Tasten beschrieben;
      Status-LED-Abschnitt vollständig (Blinkzeiten, Ursachen, Aktionen)
- [ ] T13.2 „Einstellungen": beide Kalibriermethoden, komplette Feldreferenz,
      Voltmeter-Abschnitt, Kapitel 3-Punkt-Kalibrierung
- [ ] T13.3 Theme-/Akzentwechsel wirkt auf allen Seiten und bleibt gespeichert
- [ ] T13.4 Alle Links/Icons führen zum richtigen Ziel

## T14 — OTA-Updates (Controller)

> Prüft den Update-Pfad mit dem unveränderten aktuellen Stand (Re-Flash).

```bash
cd tt_esp32controller
pio run -e esp32s3_ota -t upload      # Firmware
pio run -e esp32s3_ota -t uploadfs    # Filesystem
```

- [ ] T14.1 Beide Uploads laufen fehlerfrei durch (Status-LED: schnelles Blinken)
- [ ] T14.2 Danach: Version weiterhin V4.3.1, **Kalibrierung und Presets unverändert**
      (NVS überlebt Firmware- und Filesystem-Update)
- [ ] T14.3 Offene Webseiten fangen den Neustart ab und verbinden automatisch neu

## T15 — Tools (inkl. Retests GitHub-#6/#7)

> Ordner `tt_esp32controller/tools/`. Einmal auf **Windows** mit
> `variac_sequence.ps1` ausführen (bisher nur auf Sicht geprüft) — sonst macOS/Python.

1. Sequenz mit Standardeinstellungen laufen lassen (Limit an).
2. Sequenz mit `--limit-off` bzw. UI-Checkbox „ohne Strombegrenzung".
3. Während eines Laufs den Not-Aus in der Tools-UI betätigen.

- [ ] T15.1 (#6) Messwerte werden erst **nach** dem Ausregeln übernommen
      (stabil ±0,5 V — keine verfrühten Werte mehr)
- [ ] T15.2 (#7) Lauf ohne Strombegrenzung möglich; die Schlussfrage zur
      Strombegrenzung erscheint **nur**, wenn sie eingeschaltet war
- [ ] T15.3 Not-Aus wirkt sofort und ohne Sicherheitsabfrage
- [ ] T15.4 `variac_sequence.ps1` läuft unter Windows durch (Parität zum Python-Skript)

---

## Abschluss

- Alle Punkte ✓ → Ergebnis ins BACKLOG-Protokoll, **diese Datei löschen**.
- Befunde mit Testnummer (z. B. „T9.3") als GitHub-Issue melden — Fixes erfolgen
  unabhängig, Teil-Abnahmen sind möglich.
