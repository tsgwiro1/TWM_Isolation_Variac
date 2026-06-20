# Changelog – TWM Isolation Variac Voltmeter

Nennenswerte Änderungen an der Voltmeter-Firmware (STM32F103).
Format angelehnt an [Keep a Changelog](https://keepachangelog.com/de/1.1.0/).
Die Version entspricht der `#define FW`-Zeichenkette in `src/tt_voltmeter.ino`.

## [V1.00] – aktueller Stand

### Infrastruktur
- Eigenes PlatformIO-Projekt (`ststm32`, `genericSTM32F103CB`, ST-Link); Quelle nach `src/`.
- Build-Fix für PlatformIO: `extern "C"`-Vorwärtsdeklaration für `DMA1_Channel1_IRQHandler`
  (verhindert Linkage-Konflikt mit dem automatisch erzeugten `.ino`-Prototyp).
  Keine Funktionsänderung.

> Geplant für die nächste Version: RMS-Glättung reduzieren (geringere Latenz) –
> siehe [`../BACKLOG.md`](../BACKLOG.md) #21.
