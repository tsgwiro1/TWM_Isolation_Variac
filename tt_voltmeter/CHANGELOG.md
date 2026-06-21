# Changelog – TWM Isolation Variac Voltmeter

Nennenswerte Änderungen an der Voltmeter-Firmware (STM32F103).
Format angelehnt an [Keep a Changelog](https://keepachangelog.com/de/1.1.0/).
Versionierung nach [Semantic Versioning](https://semver.org/lang/de/) (MAJOR.MINOR.PATCH).
Die Version entspricht der `#define FW`-Zeichenkette in `src/tt_voltmeter.ino`.

## [V1.2.3] – in Entwicklung

### Hinzugefügt
- Link-Befehl `SET_OFFSET` (CMD 0x11): setzt den Spannungs-Offset direkt (Plausi −50…+50 V,
  speichert EEPROM) – Gegenstück zu `SET_FACTOR`, damit auch der Offset über Web/API einstellbar
  ist (bisher nur über die 3-Punkt-Kalibrierung). (#34)
- Magic-getaggter Versions-String im Image (`"@@VMFW@@" FW`, `__attribute__((used))` + Referenz
  beim Start), damit der Controller die FW-Version direkt aus der `.bin` lesen kann. (#33)
- **Bidirektionaler Befehls-Link zum Controller** (Paket J, Durchstich): USART1 jetzt TX **+ RX**
  über die Arduino-`HardwareSerial Serial1` (Core verwaltet IRQ/Ringpuffer). Frame-Protokoll
  `0xA5 CMD LEN [payload] CHK 0xBB` (Befehl) / `0xB5 …` (Antwort), koexistiert mit dem
  RMS-Stream. Implementiert: `GET_VERSION` → liefert den FW-String. (#27)
- Link-Befehle `GET_STATUS` (Skalierungsfaktor, Spannungs-Offset, ADC-Nullpunkt),
  `SET_FACTOR` (+ EEPROM) und `RECAL` (Auto-Zero-Kalibrierung). (#28)
- Link-Befehle `CAL3_MEASURE` / `CAL3_FINISH` für die geführte 3-Punkt-Kalibrierung über den
  Link (Referenzspannung pro Punkt frei wählbar, lineare Regression → Faktor/Offset + EEPROM). (#28)
- Link-Befehle `REBOOT` (Soft-Reset `NVIC_SystemReset`) und `RESET_DEFAULTS` (Faktor/Offset auf
  Standard + EEPROM) als Fern-Rettungsleinen. (#28)
- Link-Befehl `ENTER_BOOTLOADER` (CMD 0x40): sendet ACK und springt anschließend in den
  eingebauten STM32-System-Bootloader (ROM, `0x1FFFF000`) – Voraussetzung für das
  FW-Update über den Controller (AN3155). #30 (Voltmeter-Seite). (#30)

### Behoben
- Sprung in den ROM-Bootloader (`jumpToSystemBootloader`): Die laufende Peripherie
  (ADC/DMA/Timer) störte den Bootloader, der danach nicht auf den USART-Handshake reagierte.
  Fix: vor dem Sprung `HAL_DeInit()` + `HAL_RCC_DeInit()` (Peripherie/Takt in Reset-Zustand),
  `SCB->VTOR` auf System-Memory (F1 kann den Speicher nicht per Software spiegeln), Interrupts
  bleiben für die USART-IRQ des ROM-Loaders aktiv. Damit funktioniert das FW-Update über den
  Link end-to-end. (#30)

### Geändert
- Daten-/Befehlsleitung intern von Raw-HAL (`huart1`) auf `Serial1` umgestellt (robuster,
  gepuffert); RMS-Versand unverändert im Format.

## [V1.1.0]

### Geändert
- An den Controller wird jetzt der ungeglättete 2-Zyklen-RMS gesendet (`last_rms_value`,
  ~40 ms Latenz) statt des stark EMA-geglätteten Werts (~0,4–0,9 s). Reduziert die Mess-Latenz
  deutlich → Voraussetzung für die schnelle Regelung. Die EMA bleibt nur noch für die lokale
  Live-Anzeige (`?live`). (#21)
- Konsole/Menü (`Serial`) von USART1 auf **native USB-CDC** umgestellt (USB-Port der Blue Pill),
  da PA9/PA10 auf dem Print nicht herausgeführt sind. Flashen weiterhin via ST-Link.
- Daten-Link zum Controller von **USART2 (PA2/PA3) auf USART1 (PA9/PA10)** verlegt (weiterhin nur TX).
  Vorbereitung für bidirektionalen Befehls-Link und FW-Update über den ROM-Bootloader (Paket J).
  **Erfordert die entsprechende Hardware-Anpassung am Print.**

### Behoben
- Float-Ausgabe in `printf`/`snprintf` aktiviert (`PIO_FRAMEWORK_ARDUINO_NANOLIB_FLOAT_PRINTF`);
  `nano.specs` liess `%f` sonst leer (betraf `?status` und `?live`).

## [V1.0.0]

### Infrastruktur
- Eigenes PlatformIO-Projekt (`ststm32`, `genericSTM32F103CB`, ST-Link); Quelle nach `src/`.
- Build-Fix für PlatformIO: `extern "C"`-Vorwärtsdeklaration für `DMA1_Channel1_IRQHandler`
  (verhindert Linkage-Konflikt mit dem automatisch erzeugten `.ino`-Prototyp).
  Keine Funktionsänderung.
