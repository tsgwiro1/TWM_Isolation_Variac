# USB CDC (ESP32-S3)

Die **USB CDC** (Communications Device Class) lässt den ESP32-S3 wie eine serielle
Schnittstelle ansprechen. Darüber kann das Gerät **direkt über den internen USB-Port**
geflasht und überwacht werden – ohne externen USB-Seriell-Adapter.

## Konfiguration (in diesem Projekt)

Die Einstellung erfolgt **nicht** mehr über ein IDE-Menü, sondern über Build-Flags in
[`../platformio.ini`](../platformio.ini):

```ini
-D ARDUINO_USB_CDC_ON_BOOT=1   ; USB CDC beim Start aktiv
-D ARDUINO_USB_MODE=1          ; Hardware-CDC (ESP32-S3)
```

Diese sind bereits gesetzt; normalerweise ist nichts weiter nötig.

## Download-Mode manuell auslösen

Falls der Upload den Port nicht findet (z. B. nach einem Absturz oder beim allerersten
Flash), das Board manuell in den Download-Mode bringen:

1. **BOOT**-Taste gedrückt halten
2. kurz **RESET** drücken und loslassen
3. **BOOT** loslassen

Danach erscheint ein neuer USB-Port in der Geräteliste – diesen für den Upload verwenden.
Nach dem ersten erfolgreichen Flash muss das Board einmal manuell zurückgesetzt werden.

## Monitor

```bash
pio device monitor        # 115200 Baud (entspricht Serial.begin(115200) im Code)
```
