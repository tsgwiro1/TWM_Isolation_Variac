# Paket H — Sicherheit (#11): Umsetzungsplan

**Stand:** 2026-08-06 · Status: **geplant, nicht gestartet** (laut Absprache „optional/später")
— das einzige noch offene Paket im [`BACKLOG.md`](BACKLOG.md).

> Dieses Dokument hält den abgestimmten Plan fest, damit Paket H zu einem späteren
> Zeitpunkt ohne neue Analyse umgesetzt werden kann. Beim Start: nächste
> MINOR-Version setzen (bei Stand V4.8.1 → **V4.9.0**; V4.8.0 ist inzwischen durch das
> Display-Redesign vergeben) und die offenen Entscheidungen (unten) einholen.

## Ist-Zustand (Sicherheitslücken)

- **Keine Authentifizierung auf `/api/*` und den Webseiten:** Jeder im WLAN kann den
  Ausgang schalten, den Motor verfahren, die Kalibrierung überschreiben, Dateien
  löschen und das Gerät neu starten — bei einem Hochspannungsgerät der eigentliche
  Grund für dieses Paket.
- **OTA ohne Passwort** (auskommentiert in `src/tt_esp32controller.ino` und
  `platformio.ini`): Jeder im WLAN könnte eine eigene Firmware flashen — Totalübernahme.
- **WiFiManager-AP ist offen:** Findet das Gerät das Heim-WLAN nicht, öffnet es
  10 Minuten einen unverschlüsselten Konfigurations-AP — in Funkreichweite könnte
  jemand fremde WLAN-Zugangsdaten hinterlegen.
- Bereits erledigt (nicht mehr Teil von H): „GET→POST für Mutationen" aus der
  ursprünglichen #11-Beschreibung wurde mit #22 (V4.0.0) umgesetzt.

## Umsetzung in 4 Blöcken

| Block | Inhalt | Aufwand |
|-------|--------|---------|
| **A** | **OTA-Passwort** aktivieren (`ArduinoOTA.setPassword()`, `upload_flags = --auth=…` in beiden OTA-Envs) + **AP-Passwort** für den WiFiManager-Portal-Modus (`wm.autoConnect(ssid, pass)`) | ~1 h |
| **B** | **HTTP-Basic-Auth** auf Web-UI + `/api/*` (ESPAsyncWebServer `request->authenticate()`); Passwort im NVS mit Default, änderbar über die Einstellungsseite; `openapi.yaml` bekommt ein `securityScheme` (RapiDoc bietet dafür ein Login-Feld) | ~3–5 h |
| **C** | **Tools nachziehen:** `variac_sequence.py`/`.ps1`, `variac_server.py` um Credentials erweitern (Parameter oder Umgebungsvariable) | ~1–2 h |
| **D** | **Doku:** doc_settings (Passwort ändern, Verhalten bei Verlust), README (Flash mit OTA-Passwort — betrifft alle Entwickler), API-Doku | ~1 h |

**Gesamt: ca. 1 Arbeitstag (M–L)** plus Gerätetest.

## Nutzen

- Schutz gegen versehentliche und neugierige Zugriffe aus dem eigenen Netz
  (Gäste-Geräte, IoT, kompromittierte Rechner) auf ein Gerät, das Netzspannung schaltet.
- Schutz gegen Geräteübernahme per OTA-Flash.
- AP-Passwort: Schutz gegen WLAN-Hijacking aus Funkreichweite, wenn das Heim-WLAN
  ausfällt.

## Grenzen und Nachteile

1. **Kein HTTPS:** Der ESP32 liefert alles über unverschlüsseltes HTTP — Basic-Auth-
   Passwörter sind im LAN mitlesbar (TLS ist mit ESPAsyncWebServer praktisch nicht
   machbar). Das Paket schützt vor Gelegenheitszugriff, **nicht** vor einem aktiven
   Angreifer im Netz. Im vertrauenswürdigen Heimnetz der passende Kompromiss.
2. **Komfortverlust:** Browser-Passwortabfrage; jedes curl/Skript/Lesezeichen braucht
   Credentials; das OTA-Passwort müssen alle kennen, die flashen (auch Michael).
3. **Aussperr-Risiko:** Passwort vergessen → Recovery nur per USB-Flash am offenen
   Gerät. Milderung: Default-Passwort, das ein USB-Reflash wiederherstellt.
4. **WebSockets:** Browser können beim `ws://`-Handshake keine Auth-Header setzen.
   Vorschlag: `/ws` (Log) und `/ws_status` (Statuswerte) **offen lassen** — beide sind
   rein lesend. Wer den Log als sensibel einstuft, braucht eine Token-Lösung
   (~2 h Mehraufwand).

## Offene Entscheidungen (vor dem Start einholen)

1. **Umfang:** Alles hinter Passwort (Web-UI + API) — oder nur die zustandsändernden
   POST/DELETE-Endpunkte (Lesen/Anschauen bleibt frei)?
2. **WebSockets** offen lassen wie vorgeschlagen?
3. **Passwort-Ablage:** NVS + änderbar in der UI (Vorschlag) — oder fest im Build?
4. **Tools (Block C)** mitmachen oder auslassen (falls die Skripte nur auf Rechnern
   laufen, wo das Passwort ohnehin bekannt ist)?

## Referenzen

- [`BACKLOG.md`](BACKLOG.md) — Paket H, #11
- [`REVIEW.md`](REVIEW.md) — Abschnitt 1.3 (Sicherheit) und O-1
