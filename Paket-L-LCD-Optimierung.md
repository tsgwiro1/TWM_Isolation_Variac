# Paket L — LCD-Optimierung

**Status:** #36 + #37 umgesetzt in V4.5.0, am Gerät abgenommen · **Stand:** 2026-07-25 ·
**Betrifft:** Controller-TFT

Sammlung von Verbesserungen an der Anzeige auf dem Gerät (TFT 240 × 320, Portrait,
`tft.setRotation(2)`). Die Punkte werden hier gesammelt und gemeinsam umgesetzt.
Die erste Charge (L1 + L2) steckt in V4.5.0; weitere Punkte können jederzeit dazukommen.

---

## Ausgangslage *(Stand vor V4.5.0)*

Die Hauptanzeige besteht aus einer Kopfzeile und dem Wertebereich
([`display.cpp`](tt_esp32controller/src/display.cpp)):

| Element | Position | Details |
| --- | --- | --- |
| Kopfzeile (Balken) | `0,0` bis `240,20` | `TFT_NAVY`, gezeichnet in `drawBackground()` |
| Titel „ISOLATION VARIAC" | `10, 2`, Font 2, linksbündig | in `drawBackground()` |
| Temperatur | `230, 2`, Font 2, rechtsbündig | in `updateDisplay()`, weiss auf Navy |

Die Temperatur wird heute als `(String)wiperTemp + "C"` ausgegeben — der float-Cast
liefert zwei Nachkommastellen, angezeigt wird also z. B. `34.00C`. Ohne Sensor steht
dort `N/A`.

---

## L1 — WLAN-Status als Icon (oben links)

Der WLAN-Zustand soll auf einen Blick erkennbar sein:

| Zustand | Icon (Material Symbols) | Verhalten |
| --- | --- | --- |
| Mit WLAN verbunden | `wifi` | Icon dauerhaft sichtbar |
| Eigener Config-AP aktiv | `wifi_find` | Icon dauerhaft sichtbar |
| Kein Netz (AP beendet) | — | Icon entfernen, Fläche leeren |

Die Zustände sind seit V4.4.0 sauber unterscheidbar (GitHub-#13):
`STATE_WIFIMANAGER_AP` markiert den offenen Config-AP; nach dessen Timeout schaltet der
`networkTask` das Funkmodul ab und beendet sich, danach ist dauerhaft kein Netz mehr da
(bis zum Neustart). Für die Abfrage im Display-Task eignen sich `WiFi.status()` und
`currentSystemState`.

### Kopfzeilen-Layout (entschieden)

Der Titel wandert von linksbündig auf **zentriert** (`TC_DATUM` statt `TL_DATUM`), das
WLAN-Icon nimmt den frei werdenden Platz links ein:

```
┌────────────────────────────────────────────┐
│ [wifi]     ISOLATION VARIAC     [temp] 34 °C │   Navy-Balken, 0..20 px
└────────────────────────────────────────────┘
0          →           240 px
```

| Element | Position | Datum |
| --- | --- | --- |
| WLAN-Icon | x = 2, 16 × 16 px | — |
| Titel | zentriert | `TC_DATUM` |
| Temperatur (Icon + Wert + `°C`) | rechtsbündig bis x = 230 | `TR_DATUM` |

**Am Gerät zu prüfen (bewusst so beschlossen):** Es wird eng. Der Titel belegt in Font 2
rund 128 px; zentriert auf die Bildschirmmitte (x = 120) reicht er von etwa x = 56 bis
x = 184, während die Temperaturgruppe (Icon + Wert + Einheit, ~60 px) links bei rund
x = 178 beginnt — eine Überlappung von wenigen Pixeln ist möglich. Wenn das am Gerät
stört, gibt es drei einfache Auswege: Titel auf den freien Bereich zwischen den Icons
zentrieren (Mitte dann bei ca. x = 98 statt 120), Titel kürzen („ISOLATION VARIAC" →
„VARIAC"), oder für den Titel den kleineren Font 1 verwenden.

## L2 — Temperaturanzeige

- **Ohne Nachkommastellen:** `34 °C` statt `34.00C` (`String(wiperTemp, 0)`).
- **Icon davor:** Material Symbol `device_thermostat`.
- **Einheit dahinter:** `°C` (mit Grad-Zeichen, heute nur `C`).

**Zu klären:** Ob der verwendete TFT_eSPI-Font das Grad-Zeichen (`°`, 0xB0) überhaupt
enthält — die mitgelieferten Bitmap-Fonts decken oft nur ASCII 32–126 ab. Falls nicht,
ist ein kleiner gezeichneter Kreis (`tft.drawCircle()`) die einfachste Lösung; ein Font
mit erweitertem Zeichensatz wäre der grössere Eingriff.

---

## Technischer Rahmen: Icons aufs TFT bringen

Die verlinkten Material Symbols sind **Web-Fonts** und lassen sich nicht direkt in
TFT_eSPI verwenden — die Bibliothek kennt nur Bitmap-Fonts und Pixel-Grafiken. Die
Symbole müssen daher vorab in Bitmaps umgewandelt und als Array in die Firmware
kompiliert werden.

**Empfohlener Weg — XBM (1 Bit monochrom):**

1. Symbol von [fonts.google.com/icons](https://fonts.google.com/icons) als SVG laden
   (`wifi`, `wifi_find`, `device_thermostat`).
2. Auf die Zielgrösse rendern — 16 × 16 px passt in die 20 px hohe Kopfzeile.
3. Nach XBM konvertieren und als `static const uint8_t icon_wifi[] PROGMEM = {…}`
   ablegen (z. B. in einer neuen `icons.h`).
4. Mit `tft.drawXBitmap(x, y, icon, 16, 16, TFT_WHITE, TFT_NAVY)` zeichnen.

Monochrom reicht hier, weil die Icons einfarbig auf einheitlichem Navy-Grund liegen.
Drei Icons à 16 × 16 px kosten zusammen rund 96 Byte Flash. Farbige Alternative wäre
`pushImage()` mit RGB565 (512 Byte je Icon), bringt hier aber keinen Mehrwert.

**Zeichnen nur bei Änderung:** `updateDisplay()` aktualisiert Werte bewusst nur, wenn
sie sich geändert haben. Das WLAN-Icon sollte demselben Muster folgen (letzten
Zustand in `actDispValues` mitführen), sonst flackert es bei jedem Durchlauf.

**Lizenz:** Material Symbols stehen unter Apache 2.0. Die Lizenz ist mit der MIT-Lizenz
des Projekts vereinbar, verlangt aber einen Hinweis — bei Umsetzung einen Vermerk in
`icons.h` und im README ergänzen.

---

## Offene Punkte

- [x] Layout der Kopfzeile festlegen — Titel zentriert (x = 120), WLAN-Icon links,
      Temperatur rechts. Rechnerisch geprüft: Titel ist in Font 2 **113 px** breit,
      belegt also 63…176; die Temperaturgruppe („34 °C" mit Icon, 51 px) beginnt bei
      179 — passt. Breitere Gruppen (dreistellig ab 171, `N/A` ab 172) würden
      kollidieren, deshalb entfällt dort das Icon.
- [x] Grad-Zeichen: Font 2 geprüft (`firstchr_f16 32`, `nr_chrs_f16 96` → nur
      ASCII 32…127, kein `°`) → wird als Kreis gezeichnet. *Alternative für später:*
      TFT_eSPI kennt das Build-Flag `TFT_ESPI_GRAVE_IS_DEGREE`, das den Backtick
      durch ein echtes Grad-Glyph ersetzt — nicht genutzt, weil es global auf alle
      Fonts wirkt.
- [x] Icon-Grösse festgelegt: 16 × 16 px (drei Icons = 96 Byte Flash)
- [x] Am Gerät geprüft: Die Kopfzeile geht optisch auf (2,5 px Luft zwischen Titel
      und Temperaturgruppe — rechnerisch knapp, aber kollisionsfrei). Dabei fiel auf,
      dass `drawLegend()` kein eigenes Text-Datum setzte und das neue `TC_DATUM` von
      `drawBackground()` erbte — die Fusszeile rutschte dadurch aus dem Display.
      Behoben, indem `drawLegend()` sein `TL_DATUM` selbst setzt.
- [ ] Weitere Punkte des Pakets sammeln

## Aufwand (Schätzung, Stand heute)

| Teil | Aufwand |
| --- | --- |
| Icons konvertieren + `icons.h` anlegen | ~1 h |
| L1 WLAN-Icon (Zustandslogik + Zeichnen) | ~1 h |
| L2 Temperaturanzeige | ~30 min |
| Layout-Feinschliff am Gerät | ~30 min |

Zusammen rund ein halber Tag — wächst mit weiteren Punkten.
