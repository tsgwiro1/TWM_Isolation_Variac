# Changelog – TWM Isolation Variac Voltmeter

Nennenswerte Änderungen an der Voltmeter-Firmware (STM32F103).
Format angelehnt an [Keep a Changelog](https://keepachangelog.com/de/1.1.0/).
Versionierung nach [Semantic Versioning](https://semver.org/lang/de/) (MAJOR.MINOR.PATCH).
Die Version entspricht der `#define FW`-Zeichenkette in `src/tt_voltmeter.ino`.

## [V1.1.0] – in Entwicklung

### Geplant / in Arbeit
- RMS-Glättung reduzieren (geringere Latenz für die Spannungsregelung) – Paket C, siehe
  [`../BACKLOG.md`](../BACKLOG.md) #21.

## [V1.0.0]

### Infrastruktur
- Eigenes PlatformIO-Projekt (`ststm32`, `genericSTM32F103CB`, ST-Link); Quelle nach `src/`.
- Build-Fix für PlatformIO: `extern "C"`-Vorwärtsdeklaration für `DMA1_Channel1_IRQHandler`
  (verhindert Linkage-Konflikt mit dem automatisch erzeugten `.ino`-Prototyp).
  Keine Funktionsänderung.
