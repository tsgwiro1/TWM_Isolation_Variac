# Controller ↔ Voltmeter – Link-Protokoll & FW-Update (Paket J)

**Stand:** 2026-07-19 (Paket J abgeschlossen; API-Methoden gemäß V4-REST-Konventionen)

Serielle Verbindung zwischen ESP32-**Controller** und STM32F103-**Voltmeter**.

## Physik / Verkabelung
- **USART1** am Voltmeter über die Arduino-`HardwareSerial Serial1` (Core verwaltet IRQ + RX-Ringpuffer).
- 115200 Baud, 8N1 (Normalbetrieb).
- Verdrahtung (Cross-over):
  - Voltmeter **PA9 (TX)** → Controller GPIO **17** (`Serial1` RX)
  - Controller GPIO **18** (`Serial1` TX) → Voltmeter **PA10 (RX)**
  - GND ↔ GND
- Konsole/Menü des Voltmeters läuft getrennt über **USB-CDC** (PA11/PA12).

## Frame-Formate
Drei Frame-Typen teilen sich die Leitung (unterschiedliche Start-Bytes):

| Typ | Richtung | Format |
|-----|----------|--------|
| RMS-Stream | VM → Ctrl | `0xAA HI LO CHK 0xBB`, Wert = (HI<<8\|LO)/10 [V], CHK = (HI+LO)&0xFF, alle ~40 ms |
| Befehl | Ctrl → VM | `0xA5 CMD LEN [payload…] CHK 0xBB` |
| Antwort | VM → Ctrl | `0xB5 CMD LEN [payload…] CHK 0xBB` |

CHK (Befehl/Antwort) = `(CMD + LEN + Σ payload) & 0xFF`.
Floats werden als rohe 4 Byte little-endian übertragen (ESP32 und STM32 sind beide LE).

## Befehlssatz

| CMD | Name | Payload (Ctrl→VM) | Antwort (VM→Ctrl) | Hinweise |
|-----|------|-------------------|-------------------|----------|
| 0x01 | GET_VERSION | – | FW-String | |
| 0x02 | GET_STATUS | – | 3× float: scaling_factor, voltage_offset, adc_zero_offset | |
| 0x10 | SET_FACTOR | float (Faktor) | 1 Byte (1=ok, 0=abgelehnt) | Plausi 100…1000; speichert EEPROM |
| 0x11 | SET_OFFSET | float (Offset V) | 1 Byte (1=ok, 0=abgelehnt) | Plausi −50…+50 V; speichert EEPROM |
| 0x20 | RECAL | – | 1 Byte ACK | Auto-Zero; **blockiert danach ~5 s** |
| 0x21 | CAL3_MEASURE | u8 index + float U_ref | 1 Byte ok | mittelt **~2 s** |
| 0x22 | CAL3_FINISH | – | `[ok(1)][m(float)][b(float)]` | Regression, EEPROM; min. 2 Punkte |
| 0x30 | REBOOT | – | 1 Byte ACK | dann `NVIC_SystemReset()` |
| 0x31 | RESET_DEFAULTS | – | 1 Byte ok | Faktor/Offset auf Standard + EEPROM |
| 0x40 | ENTER_BOOTLOADER | – | 1 Byte ACK | ACK, dann Sprung ins ROM-System-Memory `0x1FFFF000` (Basis für das FW-Update via Controller, #30). |

Während blockierender Befehle (RECAL/CAL3_MEASURE) pausiert der RMS-Stream → der Controller
erkennt das über `isVoltageDataFresh()` (Timeout 250 ms) und hält die Regelung an.

## Controller-API → Befehl

| HTTP | Befehl |
|------|--------|
| `GET /api/voltmeter/version` | GET_VERSION |
| `GET /api/voltmeter/status` | GET_STATUS |
| `POST /api/voltmeter/factor?value=` | SET_FACTOR |
| `POST /api/voltmeter/offset?value=` | SET_OFFSET |
| `POST /api/voltmeter/autozero` | RECAL |
| `POST /api/voltmeter/cal3/measure?index=&voltage=` | CAL3_MEASURE |
| `POST /api/voltmeter/cal3/finish` | CAL3_FINISH |
| `POST /api/voltmeter/reboot` | REBOOT |
| `POST /api/voltmeter/reset-defaults` | RESET_DEFAULTS |
| `POST /api/voltmeter/update/upload` | – (FW-Binary → LittleFS) |
| `POST /api/voltmeter/update/start` | ENTER_BOOTLOADER + AN3155-Flash |
| `GET /api/voltmeter/update/status` | – (Fortschritt/Status) |
| `GET /api/voltmeter/update/fileversion` | – (Version aus hochgeladener .bin) |

Vollständige Parameter/Antworten: `openapi.yaml` bzw. die API-Doku-Seite auf dem Gerät.
Controller-Helper: `voltmeterRequest(cmd, payload, len, timeoutMs)` sendet und wartet (mit
`delay()`-Yield, damit der `communicationTask` die Antwort parst). Bedienfeld: Abschnitt
„Voltmeter" auf der Einstellungsseite (seit Paket G im finalen Design).

## #30 – Voltmeter-FW-Update via Controller (ROM-Bootloader)

**Umgesetzt und am Gerät end-to-end verifiziert (2026-06-21)** — inkl. Nachweis, dass das
emulierte EEPROM (Kalibrierung) den Flashvorgang übersteht.

Nutzt den **eingebauten STM32-UART-Bootloader** (AN3155) auf USART1. Recovery-Strategie:
**App-Sprung-only** (keine BOOT0/NRST-Steuerung) → ein Fehlflash mit nicht reagierender App
braucht Öffnen + ST-Link.

**Ablauf:**
1. **FW-Binary hochladen** (Web, POST multipart) → LittleFS `/voltmeter_fw.bin`
   (`/api/voltmeter/update/upload`).
2. **ENTER_BOOTLOADER** (CMD 0x40): VM sendet ACK, dann Sprung ins System-Memory `0x1FFFF000`
   (IRQs aus, SysTick/NVIC deinit, `HAL_RCC_DeInit`, MSP = `*0x1FFFF000`, PC = `*0x1FFFF004`, springen).
   → `jumpToSystemBootloader()` in der Voltmeter-FW.
3. **Controller = AN3155-Host** auf `Serial1` (`voltmeterUpdateTask`, `communicationTask`
   währenddessen suspendiert, Ausgang/Regelung aus):
   - Umschalten auf **8E1** (Bootloader nutzt gerade Parität!), 115200.
   - `0x7F` → ACK `0x79` (Auto-Baud); **Get (0x00)** zur Erkennung Standard- (0x43) vs. Extended-Erase (0x44).
   - **Page-Erase nur der Programmpages** (Page 0 … `ceil(fwSize/1024)-1`) — **kein Mass-Erase**, damit
     die letzte Flash-Page (Page 127 = emuliertes EEPROM = Kalibrierung) erhalten bleibt.
   - **Write Memory** (0x31, 256-Byte-Blöcke ab `0x08000000`, 4-Byte-aligned, je Checksumme + ACK) → **Go** (0x21, `0x08000000`).
   - zurück auf **8N1**, `communicationTask` resume, RMS-Empfang läuft weiter.
4. **Web-UI** (`settings.html`): Upload → „Update starten" → Fortschritt-Polling
   (`/api/voltmeter/update/status`) → Erfolg/Fehler.
   - **Versionserkennung (#33):** Die Voltmeter-FW enthält den Magic-Tag `@@VMFW@@<FW-String>`
     (`__attribute__((used))`). Der Controller scannt die hochgeladene `.bin`
     (`/api/voltmeter/update/fileversion`) und zeigt die Datei-Version dauerhaft an; beim Start
     wird gegen die laufende Version (`GET_VERSION`) verglichen.
5. **Schutz**: .bin vor dem Flashen plausibilisiert (Größe ≤124 KB, MSP im RAM, Reset-Vektor im Flash);
   Ausgang während Update aus.

**Verifikation (durchgeführt):** leicht geänderte Voltmeter-FW (Versions-String) per Web
geflasht → `GET_VERSION` lieferte danach die neue Version; Kalibrierung blieb erhalten.

## Referenzen
- Backlog: [`../../BACKLOG.md`](../../BACKLOG.md) – Paket J (#27–#30).
- Hardware-Umbau: [`Hardware-Umbau-USART1.md`](Hardware-Umbau-USART1.md).
