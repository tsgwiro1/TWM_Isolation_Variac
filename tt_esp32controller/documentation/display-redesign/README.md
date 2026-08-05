# TFT-Display – Redesign

> **Status:** in **V4.8.0 implementiert und geflasht.** Der Normalbetrieb-Screen ersetzt die
> Zielspannungs-Zeile durch einen Regelabweichungsbalken (zwei zur Laufzeit umschaltbare
> Varianten). Die On-Device-Umsetzung verfeinert die hier gezeigten Mockups: grössere
> Ist-Spannung mit eigenem Font, generierte Icons für Warndreieck und Thermometer, überarbeitete
> Kopfzeile. Umsetzungs-Spezifikation:
> [`umsetzung-regelabweichungsbalken.md`](umsetzung-regelabweichungsbalken.md).

## Ziel

Das TFT (240×320, Portrait, ILI9341) wird grafischer und hebt die **wichtigen Infos klar
hervor**. Bewusst **keine** Anlehnung an die Web-Oberfläche und **keine Gauge** – die
Zeigerinstrumente sitzen bereits auf der Frontplatte. Kernideen:

- **Ist-Spannung** groß + fett + farbcodiert nach Sicherheitszustand, rechtsbündig oben im
  Rahmen. Die **Zielspannung als eigener Text entfällt** — sie bleibt über den blau
  umrandeten Preset sichtbar.
- **Regelabweichungsbalken** (neu, ersetzt die Zielspannungs-Zeile): volle Kastenbreite, zeigt
  Ist minus Ziel auf einer Skala von -5V bis +5V. Zwei Varianten, per langem Tastendruck auf
  „Strombegrenzung" umschaltbar und über Neustart persistent — Details, Geometrie und
  Zustands-Mockups in [`umsetzung-regelabweichungsbalken.md`](umsetzung-regelabweichungsbalken.md).
- **Rahmen** (Radius wie Chips, links/rechts an den äußeren Chips ausgerichtet) um Werte +
  Warnfeld — trennt Messwerte vom Bedienbereich und füllt den oberen Rand.
- **Ein gelbes Achtung-Dreieck** (schwarzes „!") — jetzt **immer sichtbar**: dim-grau im
  Normalfall, kräftig gelb + hartes Blinken nur bei **ausgeschalteter Strombegrenzung** mit
  laufendem Ausgang.
- **Drei bedienbare Chips** (Ausgang · Limit · Regelung): nur das Frontplatten-Symbol,
  leuchtet farbig = EIN, grau = AUS (wie die Taster-LED).
- **Presets** im Look der Taster 1 · 2 · 3; leuchten nur bei **explizitem Aufruf** blau (nicht
  mehr, wenn der Zielwert zufällig durch manuelles Verstellen in den Preset-Bereich kommt).
- Glatte Vektor-Schrift (TFT_eSPI-FreeFonts), **Double-Buffering** (Sprite) gegen Flackern,
  Kopfzeile mit WLAN-Symbol + Temperatur.

## Betriebszustände

Ein Layout — die Anzeige ändert sich mit dem Zustand. Der Regelabweichungsbalken hat zwei
Varianten (A = feste Zonen + Pfeil, B = Füllbalken), per Langdruck auf die Regelungstaste
umschaltbar; Details in [`umsetzung-regelabweichungsbalken.md`](umsetzung-regelabweichungsbalken.md).

- **① Normal (Limit ein):** Ausgang + Limit + Regelung EIN. Ist **gelb**. Warndreieck dim-grau
  (inaktiv). Balken grün (Regelung trifft), Pfeil mittig. Ziel 230 V = Preset 3 → P3 blau
  (Schloss = Regelung hält).
- **② Ohne Strombegrenzung:** Limit-Chip grau → Ist **rot**; das gelbe Achtung-Dreieck
  (schwarzes „!") aktiv und blinkt hart. Balken zeigt eine Abweichung in der gelben Zone.
  Regelung weiter EIN.
- **③ Ausgang aus:** Alle Chips grau, Warndreieck dim-grau, Ist **hellgrau** (~0 V). Balken
  einheitlich grau (kein Regelfehler, da Ausgang aus). Preset 3 bleibt blau (folgt dem Zielwert).

## Anzeige-Logik

| Element | Bedeutung |
| :--- | :--- |
| **Ist-Spannung** (groß, fett, rechtsbündig) | **gelb** = Ausgang ein mit Limit · **rot** = ohne Limit · **hellgrau** = Ausgang aus |
| **Regelabweichungsbalken** | ersetzt die frühere Zielspannungs-Zeile; Ist./.Ziel auf -5V..+5V-Skala, Farbe/Pfeil folgt dem Ausgangszustand (grün/gelb/rot wenn EIN, grau wenn AUS) — Details in [`umsetzung-regelabweichungsbalken.md`](umsetzung-regelabweichungsbalken.md) |
| **Rahmen** (Radius wie Chips) | umschließt Werte + Warnfeld, links/rechts an den äußeren Chips ausgerichtet |
| **Achtung-Dreieck** (gelb, schwarzes „!") | jetzt **immer sichtbar**: dim-grau im Normalfall, gelb + **hartes Blinken** (1 s an / 1 s aus) nur bei ausgeschalteter Strombegrenzung mit laufendem Ausgang |
| Chips **Ausgang / Limit / Regelung** | nur das Symbol (⚡ / →\| / 🔒 Schloss): leuchtet farbig = **EIN**, grau = **AUS** (wie die Taster-LED) |
| **Preset** blau umrandet | leuchtet **nur bei explizitem Aufruf** (Taste/API), nicht mehr bei zufälliger Wertübereinstimmung |
| **Schloss** (Preset / Regelung-Chip) | die Regelung hält den Wert |
| Kopfzeile | schwarz, nur WLAN-Symbol + Temperatur (kein Titel, keine Trennlinie) |

## Nicht vergessen: es gibt mehr als die Hauptanzeige

Das finale Layout betrifft den Normalbetrieb. Das Display kennt aber weitere Vollbild-Screens
(alle in [`../../src/display.cpp`](../../src/display.cpp)) — wird nur die Hauptanzeige neu
gestaltet, stehen die anderen stilistisch daneben:

| Screen | Funktion | Inhalt |
| :--- | :--- | :--- |
| Normalbetrieb | `drawBackground()` + `drawLegend()` + `updateDisplay()` | Gegenstand dieses Layouts |
| Einstellungen / Kalibrierung | `drawSettingsScreen()` + `updateSettingsDisplay()` | Enc/Out/P1/P1S/P2/P2S, Warnzeile der Kalibrier-Plausibilität |
| Referenzfahrt | `drawHomingScreen()` | „Homing…", IP-Adresse, Firmware-Version |
| Systemfehler | `drawErrorScreen()` | Fehlerhinweis bei `STATE_ERROR` |
| Voltmeter-Update (#32) | `drawVmUpdateScreen()` + `updateVmUpdateScreen()` | Fortschrittsbalken, Prozent, Statusmeldung, „Variac gesperrt - Ausgang AUS" |
| OTA-Update (#26, V4.7.0) | `drawOtaScreen()` + `updateOtaScreen()` | Fortschrittsbalken, Prozent, Firmware/Filesystem, „Gerät nicht ausschalten!" |

Die Update-Screens sollten dieselbe Formensprache bekommen wie das neue Hauptlayout. Seit
V4.7.0 stellt `forceSafeState()` beim Sperren einen definierten Zustand her (Ausgang aus,
Strombegrenzung ein, Regelung aus, Preset-/x10-LED aus) — die Chip-Darstellung im Redesign
zeigt genau diesen Zustand eindeutig an.

## Technische Notiz zur Umsetzung

- **Icons als Zeichenprimitive** (kein XBM): Chips (Blitz, Limit-Pfeil →|, Schloss) und das
  Warndreieck (`fillRoundTri()` = gefülltes Dreieck mit abgerundeten Ecken) mit
  `fillTriangle`/`fillRoundRect`/`fillCircle`. WLAN als `drawSmoothArc`-Fächer, Thermometer als
  Röhre (Umriss) + gefüllter Quecksilbersäule/Kolben.
- **Schriften**: glatte Vektor-FreeFonts — `FreeSansBold24pt7b`-Äquivalent für die (etwas
  vergrößerte) Ist-Spannung, Font 4 (Temperatur). Bei aktivem `LOAD_GFXFF` sind sie schon von
  TFT_eSPI eingebunden — **nicht** erneut includen (Doppeldefinition), direkt via
  `&FreeSansBold…` verwenden. Welche konkrete Punktgröße die vergrößerte Ist-Spannung im Code
  trifft, klärt die Umsetzungs-Session anhand der verfügbaren Free_Fonts (siehe
  [`umsetzung-regelabweichungsbalken.md`](umsetzung-regelabweichungsbalken.md)).
- **Double-Buffering gegen Flackern**: die Ist-Spannung wird in `TFT_eSprite` gezeichnet und in
  einem Rutsch gepusht (statt löschen → neu beschriften). Sprites werden einmal angelegt;
  Fallback auf Direktzeichnen, falls das Anlegen scheitert.
- Farben (RGB565): `TFT_YELLOW`/`TFT_RED` (Warndreieck aktiv, Balken-Zonen gelb/rot),
  dunkleres Blau (`0x1A9B`, Kontrast zur weißen Preset-Nummer), Hellgrau (Ausgang aus),
  `COL_CHIPOFF` (`0x39E7`, dim-graues Warndreieck im Normalfall), `0x52AA` für den Rahmen,
  neu eine Grün-Konstante für die Regelabweichungs-Zone (~`#3ECF6B`).
- **Rendering**: Dirty-Check pro Zone; das Warndreieck-Feld und die Werte-Sprites haben
  **getrennte Bereiche**, damit nichts gegenseitig überschrieben wird.

---

*Vollständige Geometrie, Zustandslogik und beide Balken-Varianten (inkl. Umschalt-Mechanik)
stehen in [`umsetzung-regelabweichungsbalken.md`](umsetzung-regelabweichungsbalken.md).*
