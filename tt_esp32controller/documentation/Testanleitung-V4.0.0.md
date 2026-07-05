# Testanleitung — Controller-Firmware V4.0.0 (API-Umbau)

**Stand:** 2026-07-04 · **Prüfling:** ESP32-Controller V4.0.0 · **Dauer:** ca. 30–45 min

## Worum es geht

Mit V4.0.0 wurde die REST-API auf REST-Konventionen umgebaut (**Breaking Change**):

- `GET` liest nur noch — alle Aktionen (Sollwert, Befehle, Reboot, Kalibrierung, Voltmeter) sind jetzt `POST`.
- `/data` wurde entfernt und ist in `GET /api/status` aufgegangen.
- Neue interaktive API-Doku auf dem Gerät (`doc_api.html`, RapiDoc + `openapi.yaml`).
- Die Webseiten und die Tools (`tools/`-Ordner) wurden auf die neue API umgestellt.
- Außerdem seit V3.3.0: Konfiguration/Kalibrierung liegt im NVS und **überlebt Filesystem-Updates**.

Diese Tests prüfen, dass nach dem Update alles zusammenspielt: Firmware ↔ Webseiten ↔ Tools.

> **Update:** Der aktuelle Stand baut als **V4.1.0** — zusätzlich zur V4.0.0-API ist der
> Code modularisiert (reine interne Umstrukturierung, kein Verhaltensunterschied).
> Die Tests decken beides ab; wo unten „V4.0.0" steht, gilt sinngemäß „V4.1.0".

## ⚠️ Sicherheit

Das Gerät schaltet **Netzspannung** — es gelten die üblichen Regeln:

- Tests, die den **Ausgang einschalten** (T2, T7), nur im gewohnten Prüfaufbau und ohne
  angeschlossene empfindliche Last durchführen.
- Strombegrenzung während der Tests eingeschaltet lassen (außer wo explizit anders nötig).
- Bei unerwartetem Verhalten: Ausgang aus (Gerätetaste ON/OFF) bzw. Not-Aus in der Tools-UI.

## Vorbereitung (T0): Firmware + Filesystem flashen

**Wichtig: Beide Schritte ausführen — Firmware und Filesystem gehören bei V4.0.0 zwingend zusammen.**
(Altes JS + neue Firmware = Buttons ohne Funktion, und umgekehrt.)

```bash
cd TWM_Isolation_Variac/tt_esp32controller
~/.platformio/penv/bin/pio run -e esp32s3_ota -t upload      # Firmware (OTA)
~/.platformio/penv/bin/pio run -e esp32s3_ota -t uploadfs    # Webseiten/Filesystem
```

Danach im Browser die Geräteseite öffnen und **hart neu laden** (Mac: Cmd+Shift+R,
Windows: Ctrl+F5) — sonst kommt altes JavaScript aus dem Browser-Cache!

- [ ] Beide Uploads ohne Fehler durchgelaufen
- [ ] Gerät bootet, Display zeigt Normalanzeige

---

## T1 — Version & Konfigurations-Erhalt

1. Settings-Seite öffnen (`http://twm_variac.local/settings.html`).
2. Unten auf der Seite die Firmware-Version prüfen.
3. Kalibrierwerte und Presets im Formular anschauen.

**Erwartet:**
- [ ] Version zeigt **V4.1.0**
- [ ] Kalibrierung (min/max Position + Spannung) und Presets haben die **bekannten Werte**
      (NVS hat das `uploadfs` überlebt — nichts wurde zurückgesetzt)

## T2 — Hauptseite (index): Anzeige & Bedienung

1. Hauptseite öffnen (`http://twm_variac.local/`).
2. Anzeige beobachten: Ist-/Soll-Spannung, Zeigerinstrument.
3. Alle 6 Buttons einmal betätigen und Wirkung prüfen:
   ON/OFF, LIMIT, REG, P1, P2, P3 (Zustand am Gerät und Button-Hervorhebung im Web).
4. Sollwert über das Eingabefeld setzen (z. B. 50 V) → Gerät fährt an.

**Erwartet:**
- [ ] Anzeige aktualisiert sich laufend (ca. 2-s-Takt)
- [ ] Alle 6 Buttons wirken; „active"-Hervorhebung stimmt mit dem Gerät überein
- [ ] Sollwert-Setzen funktioniert, Statusmeldung erscheint
- [ ] P1–P3-Buttons zeigen ihren Aktiv-Zustand (neu über `/api/status`)

## T3 — Settings: Konfiguration schreiben & Geräte-Reboot

1. In den Settings einen unkritischen Wert ändern (z. B. ein Preset um 1 V) → **Speichern**.
2. Seite neu laden → Wert ist noch da.
3. Wert wieder zurückstellen → Speichern.
4. Button „Neustart" (Controller-Reboot) → Gerät startet neu, Seite leitet nach ~10 s weiter.

**Erwartet:**
- [ ] Speichern meldet Erfolg, Werte bleiben nach Reload erhalten
- [ ] Reboot funktioniert (jetzt via POST), automatische Weiterleitung kommt zurück

## T4 — Voltmeter-Panel (Settings-Seite)

1. „Status aktualisieren" → Version + Faktor/Offsets erscheinen.
2. Skalierungsfaktor **unverändert** neu setzen (aktuellen Wert eintragen → „Faktor speichern").
3. Spannungs-Offset ebenso (aktuellen Wert erneut speichern).
4. „Neustart" (Voltmeter) → nach ~8 s kommt der Status wieder.

**Erwartet:**
- [ ] Voltmeter-Version wird angezeigt (Firmware V1.2.3)
- [ ] Faktor/Offset speichern melden Erfolg (jetzt via POST)
- [ ] Voltmeter-Reboot funktioniert, Messwert läuft danach weiter
- Hinweis: Auto-Zero und 3-Punkt-Kalibrierung müssen **nicht** getestet werden
  (verändern die Kalibrierung; nur bei Bedarf und mit Referenz durchführen).

## T5 — Neue API-Doku (RapiDoc)

1. `http://twm_variac.local/doc_api.html` öffnen (auch erreichbar über die Doku-Navigation).
2. Optik prüfen: dunkles Theme, linke Navigation mit den API-Gruppen.
3. Einen Lese-Endpoint interaktiv testen: `GET /api/status` → „TRY" → Antwort erscheint.
4. Optional einen harmlosen POST: `POST /api/setpoint` mit `voltage=0` → „TRY".

**Erwartet:**
- [ ] Seite lädt **ohne Internetverbindung** (RapiDoc liegt lokal auf dem Gerät)
- [ ] Spec wird gerendert (Titel „TWM Isolation Variac API", Version 4.0.0)
- [ ] „TRY" bei `GET /api/status` liefert Live-JSON vom Gerät
- [ ] Navigation aus den anderen Doku-Seiten (Bedienung/Einstellungen) funktioniert weiter

## T6 — Breaking Change greift (Negativtest)

Alte Aktions-URLs per **GET** (z. B. Browser-Adresszeile) dürfen **nichts mehr auslösen**:

1. In der Browser-Adresszeile aufrufen: `http://twm_variac.local/api/reboot`
2. In der Browser-Adresszeile aufrufen: `http://twm_variac.local/api/setpoint?voltage=100`

**Erwartet:**
- [ ] **Kein** Reboot, **keine** Sollwert-Änderung — stattdessen Fehlerseite/JSON-Fehler
      (die Adresszeile macht GET; Aktionen brauchen jetzt POST)

## T7 — Tools (Sequenz-Skripte)

Voraussetzung: Python 3, Repo lokal ausgecheckt.

**Variante A — Verbindungstest (harmlos, ohne Bewegung):**
```bash
cd TWM_Isolation_Variac/tt_esp32controller/tools
python3 variac_sequence.py --host <IP-oder-hostname>
```
Zeigt Status; beim interaktiven Sequenz-Teil einfach mit Ctrl+C abbrechen,
falls keine Sequenz gefahren werden soll.

**Variante B — Browser-UI mit kurzer Sequenz (Gerät fährt & schaltet!):**
```bash
python3 variac_server.py        # öffnet http://127.0.0.1:8765
```
Host eintragen → Verbindung prüfen → kurze Sequenz (z. B. 20/50 V, Intervall 5 s) starten.
Auch den **Not-Aus-Button** einmal testen (setzt 0 V + Ausgang AUS — das ist der wichtigste
Pfad, denn er nutzt die umgestellten POST-Aufrufe).

**Erwartet:**
- [ ] Statusabfrage funktioniert (Variante A)
- [ ] Sequenz läuft: Spannungen werden gesetzt, Zustände geschaltet (Variante B)
- [ ] **Not-Aus** wirkt: 0 V + Ausgang AUS

## T8 — Regression Kurzcheck

- [ ] Live-Log-Seite (`log.html`): Meldungen laufen ein (WebSocket), Historie beim Öffnen da
- [ ] Bedienung am Gerät selbst (Encoder, Tasten) unverändert
- [ ] Regelung (REG ein, Sollwert ändern): fährt „snappy" wie gewohnt an

---

## Ergebnis melden

- **Alles grün:** kurze Rückmeldung genügt (z. B. „Testanleitung V4.0.0 komplett ok").
- **Fehler gefunden:** bitte ein GitHub-Issue mit Testschritt-Nummer (z. B. „T4.2"),
  beobachtetem Verhalten und — falls vorhanden — Browser-Konsolen-Fehlermeldung (F12)
  bzw. Ausschnitt aus dem Live-Log.
