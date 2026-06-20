# Changelog – TWM Isolation Variac Voltmeter

Nennenswerte Änderungen an der Voltmeter-Firmware (STM32F103).
Format angelehnt an [Keep a Changelog](https://keepachangelog.com/de/1.1.0/).
Versionierung nach [Semantic Versioning](https://semver.org/lang/de/) (MAJOR.MINOR.PATCH).
Die Version entspricht der `#define FW`-Zeichenkette in `src/tt_voltmeter.ino`.

## [V1.1.0] – in Entwicklung

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
