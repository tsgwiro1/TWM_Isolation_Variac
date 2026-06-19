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
| `tt_esp32controller/` | Controller-Firmware (PlatformIO/Arduino), Web-Oberfläche (`data/`), projekteigene Doku |
| `tt_voltmeter/` | Voltmeter-Firmware (STM32) und projekteigene Doku |
| `documentation/` | Gemeinsame Dokumentation (z. B. Trinamic-Treiber, Hardware-Modifikationen) |

Arbeitsstand und geplante Aufgaben:
[`tt_esp32controller/REVIEW.md`](tt_esp32controller/REVIEW.md) (Analyse) und
[`tt_esp32controller/BACKLOG.md`](tt_esp32controller/BACKLOG.md) (priorisierter Plan).

## Build & Flash

Der Controller wird mit **PlatformIO** (VS Code) gebaut. Eine ausführliche Build-/Flash-Anleitung
(USB-Erstflash, OTA-Update, Filesystem-Upload) folgt — siehe Backlog-Paket A.

## Lizenz

MIT — siehe [`LICENSE`](LICENSE).
