# TFT-Display – Redesign (finales Layout)

> **Status:** Umgesetzt und am Gerät iteriert (Branch `feature/redesign-display`, aufsetzend
> auf V4.7.0). Der Normalbetrieb-Screen ist neu gestaltet und läuft auf dem Gerät; die übrigen
> Screens bleiben vorerst unverändert. Feinschliff (Größen/Abstände) noch im Gang.

## Ziel

Das TFT (240×320, Portrait, ILI9341) wird grafischer und hebt die **wichtigen Infos klar
hervor**. Bewusst **keine** Anlehnung an die Web-Oberfläche und **keine Gauge** – die
Zeigerinstrumente sitzen bereits auf der Frontplatte. Kernideen:

- **Ist- und Zielspannung** gestapelt, rechtsbündig in einer Zahlenspalte; links kleine
  Labels „Ist"/„Ziel". **Ist** groß, fett, farbcodiert nach Sicherheitszustand; **Ziel**
  gleich groß, aber **grau und nicht fett**.
- **Zwei Warndreiecke** in eigener linker Spalte (Achtung / > 50 V): aktiv pulsierend
  (Farbe↔dim), sonst dim-grau.
- **Drei bedienbare Chips** (Ausgang · Limit · Regelung), spaltengleich über den Presets:
  nur das Frontplatten-Symbol, leuchtet farbig = EIN, grau = AUS (wie die Taster-LED).
- **Presets** im Look der Taster 1 · 2 · 3; das passende Preset leuchtet blau.
- Glatte Vektor-Schrift (TFT_eSPI-FreeFonts), Kopfzeile mit WLAN-Symbol + Temperatur.

## Das finale Layout (drei Betriebszustände)

Ein Layout — die Anzeige ändert sich mit dem Zustand:

<table>
  <tr>
    <th><img src="final-1-mit-limit.svg" width="220" alt="Mit Strombegrenzung"></th>
    <th><img src="final-2-ohne-limit.svg" width="220" alt="Ohne Strombegrenzung"></th>
    <th><img src="final-3-ausgang-aus.svg" width="220" alt="Ausgang aus"></th>
  </tr>
  <tr>
    <td valign="top"><b>① Mit Strombegrenzung</b><br>
      Ausgang + Limit + Regelung EIN. Ist <b>gelb</b>, Ziel grau. Nur das Blitz-Dreieck
      (&gt; 50 V) pulst. Ziel 230 V = Preset 3 → P3 blau (Schloss = Regelung hält).</td>
    <td valign="top"><b>② Ohne Strombegrenzung</b><br>
      Limit-Chip grau → Ist <b>rot</b>; Achtung-Dreieck + Blitz-Dreieck pulsen.
      Regelung weiter EIN.</td>
    <td valign="top"><b>③ Ausgang aus</b><br>
      Alle Chips grau, Warndreiecke dim-grau, Ist <b>hellgrau</b> (~0 V). Ziel bleibt grau;
      Preset 3 bleibt blau (folgt dem Zielwert).</td>
  </tr>
</table>

## Anzeige-Logik

<img src="legend.svg" width="470" alt="Anzeige-Logik">

| Element | Bedeutung |
| :--- | :--- |
| **Ist-Spannung** (groß, fett) | **gelb** = Ausgang ein mit Limit · **rot** = ohne Limit · **hellgrau** = Ausgang aus |
| **Zielspannung** (groß, **grau**, nicht fett) | rechtsbündig unter der Ist-Spannung |
| Chips **Ausgang / Limit / Regelung** | nur das Symbol (⚡ / →\| / 🔒 Schloss): leuchtet farbig = **EIN**, grau = **AUS** (wie die Taster-LED) |
| **Achtung-Dreieck** (rot, links oben) | Strombegrenzung **aus** — **unabhängig vom Ausgang**; pulst (1 s/1 s, Farbe↔dim) |
| **Blitz-Dreieck** (gelb, links unten) | Ausgangsspannung > 50 V (Berührungsgefahr) — pulst |
| Warndreieck inaktiv | dim-grau am selben Platz |
| **Preset** blau umrandet | **Zielwert** entspricht dem Preset (auch bei Ausgang aus) |
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

- **Icons als Zeichenprimitive** (kein XBM): Chips, Warndreieck, Blitz, Limit-Pfeil (→|) und
  Schloss werden mit `fillTriangle`/`drawLine`/`fillRoundRect`/`fillCircle` gezeichnet. WLAN
  bleibt vorerst als 16×16-XBM, Thermometer als Kontur-Primitive.
- **Schriften**: glatte Vektor-FreeFonts — `FreeSansBold24pt7b` (Ist-Spannung),
  `FreeSans24pt7b` (graue Zielspannung, nicht fett), `FreeSansBold12pt7b`/`9pt7b` (Labels).
  Bei aktivem `LOAD_GFXFF` sind sie schon von TFT_eSPI eingebunden — **nicht** erneut includen
  (Doppeldefinition), direkt via `&FreeSansBold…` verwenden.
- Farben (RGB565): `TFT_YELLOW`, `TFT_RED`, dunkleres Blau (`0x1A9B`, Kontrast zur weißen
  Preset-Nummer), Hellgrau (Ausgang aus), mittleres Grau (`0x9492`) für die Zielspannung.
- **Rendering**: Dirty-Check pro Zone (nur Geändertes neu zeichnen, flimmerfrei); Warnspalte
  und Werte-Spalte haben **getrennte Lösch-Bereiche**, damit nichts gegenseitig überschrieben wird.

---

*Die SVGs sind maßstabsgetreu 240×320 und werden aus `scratchpad/gen_final.py` erzeugt.*
