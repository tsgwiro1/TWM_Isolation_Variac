# Controller ↔ Voltmeter – Link-Protokoll & FW-Update (Paket J)

**Stand:** 2026-06-20

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
| 0x20 | RECAL | – | 1 Byte ACK | Auto-Zero; **blockiert danach ~5 s** |
| 0x21 | CAL3_MEASURE | u8 index + float U_ref | 1 Byte ok | mittelt **~2 s** |
| 0x22 | CAL3_FINISH | – | `[ok(1)][m(float)][b(float)]` | Regression, EEPROM; min. 2 Punkte |
| 0x30 | REBOOT | – | 1 Byte ACK | dann `NVIC_SystemReset()` |
| 0x31 | RESET_DEFAULTS | – | 1 Byte ok | Faktor/Offset auf Standard + EEPROM |
| 0x40 | ENTER_BOOTLOADER | – | 1 Byte ACK | **geplant (#30)**; danach Sprung ins ROM-System-Memory |

Während blockierender Befehle (RECAL/CAL3_MEASURE) pausiert der RMS-Stream → der Controller
erkennt das über `isVoltageDataFresh()` (Timeout 250 ms) und hält die Regelung an.

## Controller-API → Befehl

| HTTP | Befehl |
|------|--------|
| `GET /api/voltmeter/version` | GET_VERSION |
| `GET /api/voltmeter/status` | GET_STATUS |
| `GET /api/voltmeter/factor?value=` | SET_FACTOR |
| `GET /api/voltmeter/autozero` | RECAL |
| `GET /api/voltmeter/cal3/measure?index=&voltage=` | CAL3_MEASURE |
| `GET /api/voltmeter/cal3/finish` | CAL3_FINISH |
| `GET /api/voltmeter/reboot` | REBOOT |
| `GET /api/voltmeter/reset-defaults` | RESET_DEFAULTS |

Controller-Helper: `voltmeterRequest(cmd, payload, len, timeoutMs)` sendet und wartet (mit
`delay()`-Yield, damit der `communicationTask` die Antwort parst). Bedienfeld vorläufig in
`settings.html` (finale UI in Paket G/#23).

## #30 – Voltmeter-FW-Update via Controller (ROM-Bootloader, geplant)

Nutzt den **eingebauten STM32-UART-Bootloader** (AN3155) auf USART1. Recovery-Strategie:
**App-Sprung-only** (keine BOOT0/NRST-Steuerung) → ein Fehlflash mit nicht reagierender App
braucht Öffnen + ST-Link. Daher **am offenen Gerät entwickeln und verifizieren, erst danach versiegeln.**

**Ablauf:**
1. **FW-Binary hochladen** (Web, POST multipart) → LittleFS `/voltmeter_fw.bin` (~60 KB).
2. **ENTER_BOOTLOADER** (CMD 0x40): VM sendet ACK, dann Sprung ins System-Memory `0x1FFFF000`
   (IRQs aus, Peripherie/SysTick deinit, MSP = `*0x1FFFF000`, PC = `*0x1FFFF004`, springen).
3. **Controller = AN3155-Host** auf `Serial1`:
   - Umschalten auf **8E1** (Bootloader nutzt gerade Parität!), 115200.
   - `0x7F` → ACK `0x79` (Auto-Baud); optional Get/Get-ID.
   - **(Extended/Mass) Erase** → **Write Memory** (0x31, 256-Byte-Blöcke ab `0x08000000`, je Checksumme + ACK) → **Go** (0x21, `0x08000000`).
   - zurück auf **8N1**, RMS-Empfang läuft weiter.
4. **Web-UI**: Upload → „Update starten" → Fortschritt → Erfolg/Fehler.
5. **Schutz**: .bin vor dem Flashen plausibilisieren (Größe, Vektortabelle); Ausgang während Update aus.

**Ende-zu-Ende-Test:** leicht geänderte Voltmeter-FW (Versions-String) per Web flashen →
`GET_VERSION` muss danach die neue Version liefern.

## Referenzen
- Backlog: [`../../BACKLOG.md`](../../BACKLOG.md) – Paket J (#27–#30).
- Hardware-Umbau: [`Hardware-Umbau-USART1.md`](Hardware-Umbau-USART1.md).
