# TWM Isolation Variac

Steuerung für einen motorisierten **Trenn-/Stelltransformator (Isolation Variac)**. Das Projekt
umfasst zwei zusammenarbeitende Firmwares sowie die gemeinsame Dokumentation in einem Mono-Repo.

Der **Controller** (`tt_esp32controller/`, ESP32-S3) fährt über einen Schrittmotor den Schleifer
des Variac und stellt so die Ausgangsspannung ein. Bedienung wahlweise lokal — TFT-Display,
Drehencoder und Tasten (Ein/Aus, Strombegrenzung, Regelung, 3 Presets) — oder über das integrierte
**Web-Interface** mit REST-/WebSocket-API. Dazu kommen temperaturgeregelte Lüftersteuerung,
WLAN-Einrichtung per WiFiManager und Updates over-the-air (OTA).

Das **Voltmeter** (`tt_voltmeter/`, STM32F103) misst die Ausgangsspannung als echten AC-RMS-Wert
(ZMPT101B-Sensor) und liefert ihn laufend über eine serielle Verbindung an den Controller — die
Basis für die geschlossene Spannungsregelung.

## Aufbau

| Pfad | Inhalt |
|------|--------|
| `tt_esp32controller/` | Controller-Firmware (PlatformIO/Arduino), Web-Oberfläche (`data/`), Controller-Doku (inkl. Trinamic-Treiber, Hardware-Modifikationen) |
| `tt_voltmeter/` | Voltmeter-Firmware (STM32) und Voltmeter-Doku |

Arbeitsstand und geplante Aufgaben:
[`REVIEW.md`](REVIEW.md) (Analyse) und [`BACKLOG.md`](BACKLOG.md) (priorisierter Plan).

## Build & Flash (Controller)

Der Controller wird mit **PlatformIO** (VS Code, Erweiterung „PlatformIO IDE") gebaut.
Alle Befehle im Ordner `tt_esp32controller/` ausführen.

Es gibt drei Environments – kein Umkommentieren mehr nötig:

| Environment | Zweck |
|-------------|-------|
| `esp32s3_usb` | Upload/Monitor über **USB** – erster Flash eines neuen Boards, Recovery |
| `esp32s3_ota` | Upload über **WLAN (OTA)** – täglicher Gebrauch (Default) |
| `esp32s3_sim` | **Simulationsmodus** – Regelung ohne Variac/Voltmeter testen (Spannung wird aus der Stepper-Position simuliert), Upload via OTA wie Standard |

Das Gerät besteht aus **zwei Teilen**, die getrennt geflasht werden:
1. **Firmware** (der Programmcode)
2. **Filesystem** (die Webseiten aus `data/`, als LittleFS-Image)

### Erster Flash (neues Board, nur USB)

```bash
# Board per USB anschließen
pio run -e esp32s3_usb -t upload      # Firmware
pio run -e esp32s3_usb -t uploadfs    # Webseiten (data/)
```

> **Download-Mode** (falls der USB-Port nicht gefunden wird): **BOOT** gedrückt halten,
> kurz **RESET** drücken und loslassen, dann **BOOT** loslassen. Details:
> [`tt_esp32controller/documentation/USB CDC.md`](tt_esp32controller/documentation/USB%20CDC.md).

Danach WLAN einrichten: Das Gerät öffnet beim ersten Start den Access Point
`TWM_IsolationVariac` (WiFiManager). Dort das Heim-WLAN hinterlegen. Anschließend ist es
unter `http://twm_variac.local/` erreichbar.

### Updates über OTA (Standard)

```bash
pio run -e esp32s3_ota -t upload      # Firmware via WLAN
pio run -e esp32s3_ota -t uploadfs    # Webseiten via WLAN
```

Wird `-e` weggelassen, gilt das Default-Environment `esp32s3_ota`.

### Serieller Monitor

```bash
pio device monitor          # 115200 Baud (USB-CDC)
```

### Hinweise

- **Hostname/IP:** Findet dein Rechner `twm_variac.local` nicht (häufig unter Windows),
  in `platformio.ini` unter `[env:esp32s3_ota]` die feste IP als `upload_port` eintragen.
- **OTA-Passwort:** Optional in der Firmware aktivierbar; dann in `[env:esp32s3_ota]`
  `upload_flags = --auth=...` setzen.
- **Webseiten geändert?** Nach Änderungen in `data/` immer `uploadfs` ausführen –
  ein reiner Firmware-Upload überträgt die Webseiten **nicht**.

## Build & Flash (Voltmeter)

Das Voltmeter ist ein eigenes **PlatformIO-Projekt** (STM32F103 „Blue Pill", STM32duino).
Alle Befehle im Ordner `tt_voltmeter/` ausführen. Geflasht wird per **ST-Link (SWD)**.

```bash
pio run                     # bauen
pio run -t upload           # via ST-Link flashen
pio device monitor          # Konsole/Kalibrierung (USB-CDC, USB-Port der Blue Pill)
```

Schnittstellen: **USART1** (PA9/PA10) ist der bidirektionale Befehls-/Datenlink zum Controller
(darüber läuft auch das Voltmeter-FW-Update via Controller), die serielle **Konsole** für
Befehle und Kalibrierung läuft über die native **USB-CDC** (PA11/PA12). Details:
[`tt_voltmeter/documentation/Link-Protokoll.md`](tt_voltmeter/documentation/Link-Protokoll.md).

## Dokumentation

Die **Bedienungs-, Einstellungs- und API-Dokumentation** liegt auf dem Gerät selbst und ist
im Webinterface verlinkt (`http://twm_variac.local/doc_usage.html`): Bedienung inkl.
Status-LED-Blinkcodes, interaktive API-Referenz (aus
[`openapi.yaml`](tt_esp32controller/data/openapi.yaml) — der Single Source of Truth für die
REST-/WebSocket-API) sowie Systemeinstellungen und Kalibrierung.

Ergänzende Dokumente im Repo:

| Dokument | Inhalt |
|----------|--------|
| [`tt_esp32controller/documentation/Status-LED.md`](tt_esp32controller/documentation/Status-LED.md) | Status-LED-Blinkmuster und Fehlerursachen |
| [`tt_esp32controller/documentation/USB CDC.md`](tt_esp32controller/documentation/USB%20CDC.md) | USB-CDC-Konfiguration, Download-Mode |
| `tt_esp32controller/documentation/Modifikation Controller Print.docx` | Hardware-Umbau am Controller-Print für das AC-Voltmeter (12-V-Speisung an J6) |
| `tt_esp32controller/documentation/Trinamic_*.PNG` | Referenz-Einstellungen des Trinamic-Schrittmotortreibers |
| [`tt_voltmeter/documentation/Link-Protokoll.md`](tt_voltmeter/documentation/Link-Protokoll.md) | Controller↔Voltmeter-Protokoll, Befehlssatz, FW-Update via Controller |
| [`tt_voltmeter/documentation/Hardware-Umbau-USART1.md`](tt_voltmeter/documentation/Hardware-Umbau-USART1.md) | Verlegung des Controller-Links auf USART1 |
| `tt_voltmeter/documentation/The ZMPT101B AC Voltage Sensor Module.docx` | Referenz zum Spannungssensor-Modul |

Änderungshistorie je Firmware: [`tt_esp32controller/CHANGELOG.md`](tt_esp32controller/CHANGELOG.md)
und [`tt_voltmeter/CHANGELOG.md`](tt_voltmeter/CHANGELOG.md).

## Lizenz

MIT — siehe [`LICENSE`](LICENSE).
