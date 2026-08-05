# Umsetzungs-Auftrag: Regelabweichungs-Balken (Varianten A + B, umschaltbar)

> Dieses Dokument ist als eigenständiger Prompt für eine **andere Claude-Session** gedacht,
> die diesen Screen im Code umsetzt. Es enthält alle Design-Entscheidungen, Geometrie und
> finalen Mockups — die Session, die es liest, muss keinen Vorlauf aus einer anderen
> Konversation kennen.

Beide Varianten (**A** — feste Zonen + Pfeil, **B** — Füllbalken von der Mitte) werden im
selben Firmware-Build umgesetzt und zur Laufzeit per langem Tastendruck umgeschaltet (siehe
unten). Alles bis auf den Balken selbst (Rahmen, Ist-Spannung, Warndreieck, Chips, Presets,
Umschalt-Mechanik) ist für beide Varianten identisch und wird hier nur einmal beschrieben.

## Projekt-Kontext

- Repo: TWM Isolation Variac (ESP32-Controller + separates Voltmeter), Branch
  `feature/redesign-display`.
- Betroffene Datei: `tt_esp32controller/src/display.cpp` (TFT_eSPI, ILI9341, 240×320 Portrait).
- Relevante bestehende Funktionen/Strukturen dort:
  - `struct displayValues` / `actDispValues` — Dirty-Check-Zustand für den Normalbetrieb-Screen.
  - `drawBackground()` — zeichnet den statischen Rahmen (Rundrahmen um Werte-/Warnbereich).
  - `updateDisplay()` — pro-Zone Dirty-Check-Update, läuft alle 100ms aus `displayUpdateTask()`.
  - `drawIstValue(int v, uint16_t col)` — grosse Ist-Spannung, per `TFT_eSprite` (Flacker-frei).
  - `drawZielValue(int v)` — **entfällt in diesem Umbau**, samt Sprite `sprZiel`.
  - `drawWarn(...)` / `icoWarnTri(...)` — bestehendes Warndreieck, wird ersetzt/angepasst.
  - Farbkonstanten oben in der Datei: `COL_CHIPOFF` (0x39E7, dezentes Grau für inaktive
    Chips/Icons), `COL_VGREY` (`TFT_LIGHTGREY`, für Ist-Spannung bei Ausgang aus), `COL_FRAME`
    (0x52AA, Rahmenfarbe), `TFT_YELLOW`, `TFT_RED`.
  - Relevante Zustandsgrössen: `received_rms_value` (Ist-Spannung, float), `setpoint_voltage`
    (Zielspannung, float), `A_limit->getState()` (Strombegrenzung ein/aus), `A_onoff->getState()`
    (Ausgang ein/aus).
  - `Action.h:36-37` (`BUTTONLONGPRESSTIMEOUT`, `ButtonEvent::LONGPRESSED`), `actions.cpp:90`
    (`cb_RelaisAction`, Callback von `A_limit`/`A_onoff`), `actions.cpp:301` und `web.cpp:106`
    (`resetPresetActions()`), `config.cpp:392` (`saveConfiguration()`, NVS).
- **Wichtig:** Vor dem eigentlichen Kompilieren/OTA-Hochladen unbedingt mit dem Nutzer
  abstimmen — dieses Dokument beschreibt nur die Design-Vorgabe, keine Freigabe zum Bauen/Flashen.

## Was sich ändert (Soll-Zustand)

1. **Zielspannung als Text entfällt** komplett (`drawZielValue` + `sprZiel` können weg). Der
   Zielwert bleibt intern natürlich nötig (für die Abweichungsberechnung und weil er über den
   blau umrandeten Preset sichtbar bleibt) — nur die eigene Text-Zeile verschwindet.
2. **Ist-Spannung** bleibt an derselben horizontalen Position (rechtsbündig, x=228), wird aber
   etwas grösser und die Baseline leicht nach unten verschoben (siehe Geometrie).
3. **Warndreieck** wird kleiner (an der Ziffernhöhe der Ist-Spannung orientiert statt am vollen
   Font-Ascent), rückt näher an einen einheitlichen Abstand zu Rahmen oben/links, und ist ab
   jetzt **immer sichtbar** — dim-grau (`COL_CHIPOFF`) wenn inaktiv, kräftig gelb blinkend wenn
   aktiv. Vorher war das Feld im Normalfall komplett leer.
4. **Neu:** Regelabweichungs-Balken über die volle Kastenbreite, an der Stelle, wo bisher die
   Zielspannung stand — in zwei umschaltbaren Varianten (siehe unten).
5. **Neu:** Presets leuchten nur noch bei explizitem Aufruf (Bugfix, siehe eigener Abschnitt).

Der äussere Rahmen selbst (`drawBackground()`, `thickRoundRect(12, 46, 224, 106, 9, 2,
COL_FRAME)`) ändert sich **nicht** — nur der Inhalt darin wird neu verteilt.

## Geometrie (exakte Pixel-Koordinaten)

Alle Koordinaten beziehen sich auf das 240×320-Display, Rahmen bei x=12..236, y=46..152.

| Element | Koordinaten |
| :--- | :--- |
| Ist-Spannung, Baseline | `x=228, y=92`, `text-anchor: end` (rechtsbündig) |
| Ist-Spannung, Schriftgrösse | etwas grösser als bisher (Mockup nutzt ~38px SVG-Schrift für `FreeSansBold24pt7b`-Äquivalent — **welche `TFT_eSPI` FreeFont das im Code konkret trifft, muss die umsetzende Session anhand der verfügbaren Free_Fonts prüfen**; Standard-Set hat oft nur Stufen bis 24pt, ggf. ist ein eigener Font nötig oder 24pt bleibt bestehen und nur die Positionierung ändert sich) |
| Warndreieck, Apex | `(45, 65)` |
| Warndreieck, Basis links/rechts | `(30, 93)` / `(60, 93)` — Höhe 28px, Halbbreite 15px |
| Warndreieck, Abstand zum Rahmen | oben **und** links je ca. 18px (gleich) |
| Ausrufezeichen (Balken) | `rect x=42, y=74, w=6, h=7` |
| Ausrufezeichen (Punkt) | `circle cx=45, cy=87, r=1.5` |
| Balken-Track | `x=20, y=122, width=208, height=7, rx=3.5` |
| Balken, 0V-Mitte | `x=124` (= Rahmenmitte) |
| Balken, Skala | 20.8 px pro Volt (208px / 2 / 5V) |
| Balken, Zonen-Grenzen (x) | -5V→20 · -4V→40.8 · -2V→82.4 · 0V→124 · +2V→165.6 · +4V→207.2 · +5V→228 |
| Pfeil-Marker (Dreieck, **nur Variante A**) | Apex `(x, 122)`, Basis `(x-7, 110)` / `(x+7, 110)` — `x` = Pixel-Position der aktuellen Abweichung |
| Grüner Strich (**nur Variante B**, bei ~0V + Ausgang ein) | `rect x=121, y=116, width=6, height=14, rx=3` |
| Zonen-Beschriftung | Baseline `y=143`, Schriftgrösse 13px, Texte `"-4V"`, `"-2V"`, `"2V"`, `"4V"` bei den jeweiligen Zonen-Grenzen-x, Farbe wie `COL_LABEL`/`COL_CHIPOFF`-Ton (siehe Referenz-SVGs) |

## Warndreieck: Logik

> Nicht zu verwechseln mit dem Pfeil-Marker/Füllbalken weiter unten — das sind eigene Elemente
> mit eigener Farblogik (siehe dortiger Abschnitt). Das Warndreieck hier bleibt eine reine
> Sicherheits-Warnung zur Strombegrenzung, unverändert zur letzten Iteration, und gilt
> identisch für beide Varianten.

```
bool warnActive = !limit && out;   // Strombegrenzung aus UND Ausgang an — die echte Gefahrensituation
bool blinkOn = ((millis() / 1000) % 2) == 0;   // bestehendes hartes Blinken beibehalten

Farbe:
  !warnActive         -> COL_CHIPOFF (dim-grau), KEIN Blinken, immer gezeichnet
  warnActive, blinkOn -> TFT_YELLOW, "!" schwarz
  warnActive, !blinkOn -> dim565(TFT_YELLOW) oder COL_CHIPOFF, "!" schwarz
```

Wichtig: **immer zeichnen** (kein leeres Feld mehr) — nur Farbe/Blink-Zustand wechseln. Das Feld
war bisher `fillRect(25, 63, 70, 72, TFT_BLACK)` vor jedem Neuzeichnen — Fläche und Position an
die neue, kleinere Geometrie oben anpassen.

## Varianten-Umschaltung per langem Tastendruck

- **Auslöser**: langer Druck (bestehender `ButtonEvent::LONGPRESSED`, Timeout
  `BUTTONLONGPRESSTIMEOUT` = 2000ms, `Action.h:36-37`) auf die "Strombegrenzung"-Taste
  (`A_limit`). Kurzer Druck bleibt unverändert die bestehende Funktion (Strombegrenzung ein/aus
  toggeln, `cb_RelaisAction`, `actions.cpp:90`). `LONGPRESSED` ist für `A_limit` in
  `cb_RelaisAction` aktuell unbelegt (nur `PRESSED` wird behandelt) — sauberer Hook ohne
  Konflikt mit der bestehenden Funktion.
- **Zustand**: neues `uint8_t displayVariant` (0 = A, 1 = B) im globalen Anzeige-/Config-Zustand
  (Ablageort nach bestehendem Muster wählen, z.B. neben `actDispValues` oder in `state.h`).
- **Speichern**: via bestehendes `saveConfiguration()` (`config.cpp:392`, NVS über
  `Preferences`) — neues Feld analog zu den bestehenden Presets/Kalibrierwerten ergänzen.
- **Laden**: beim Boot aus NVS lesen, dort wo auch die übrigen Preferences beim Start geladen
  werden. Default = Variante A, falls das Feld noch nicht in NVS existiert (Erstinstallation /
  Update von einer Firmware-Version ohne dieses Feld).
- **Wirkung**: `updateDisplay()` ruft je nach `displayVariant` die A- oder B-Zeichenroutine für
  den Regelabweichungs-Balken auf. Rahmen, Ist-Spannung, Warndreieck, Chips, Presets bleiben in
  beiden Fällen identisch — nur die Balken-Zeichenfunktion wechselt. Beim Umschalten den
  Balkenbereich einmal komplett neu aufbauen (alte Zeichnung der anderen Variante sauber
  überschreiben, z.B. über den bestehenden Dirty-Check-Reset wie in `initDisplayStruct()`).

## Presets: nur bei explizitem Aufruf aufleuchten

Bestehendes, unerwünschtes Verhalten in `display.cpp` (`updateDisplay()`): das aktive Preset
wird aktuell per **Wertevergleich** ermittelt —

```cpp
int pv[3] = { A_p1->getValuePreset(), A_p2->getValuePreset(), A_p3->getValuePreset() };
int8_t active = -1;
for (int i = 0; i < 3; i++) {
    if (abs(target - pv[i]) <= 2) { active = (int8_t)i; break; }
}
```

— dadurch leuchtet ein Preset auch dann auf, wenn der Zielwert zufällig (z.B. durch manuelles
Drehen am Encoder) in dessen Bereich liegt, **ohne** dass das Preset tatsächlich aufgerufen
wurde. Gewünscht: Presets leuchten **nur**, wenn sie effektiv per Taste oder API aufgerufen
wurden.

**Der Fix ist einfacher als das Symptom vermuten lässt** — `Action::getState()` der Preset-
Objekte spiegelt bereits korrekt "wurde dieses Preset zuletzt explizit aufgerufen": beim
Aufruf schaltet `cb_ValueAction` (`actions.cpp:112-117`) das Preset per `act->on()` ein, und bei
jeder manuellen Encoder-Drehung (`actions.cpp:301`) sowie bei einem freien API-Sollwert
(`web.cpp:106`) wird bereits `resetPresetActions()` aufgerufen, was alle drei Preset-LEDs
`off()` schaltet. Der Wertevergleich in `updateDisplay()` umgeht diesen bereits korrekten
Zustand unnötig. Fix:

```cpp
int8_t active = -1;
if (A_p1->getState()) active = 0;
else if (A_p2->getState()) active = 1;
else if (A_p3->getState()) active = 2;
```

`pv[]` (die drei Preset-Werte für die Anzeige der Zahl auf der Kachel) bleibt unverändert
nötig, nur die Bestimmung von `active` ändert sich. `held` (Schloss-Icon, `bool held = (active
>= 0) && reg;`) bleibt unverändert, da es direkt auf dem neuen `active` aufbaut. Gilt
unverändert für beide Varianten.

---

# Variante A — feste Zonen + Pfeil

Kernidee: der Balken zeigt **immer alle drei Zonen vollständig eingefärbt**; ein Pfeil markiert
zusätzlich die aktuelle Position darauf. Der Pfeil (und die Zonenfarben) spiegeln **immer den
Ausgangszustand**: Ausgang **ein** → Farbe passend zur Zone, in der die aktuelle Abweichung
liegt (Grün/Gelb/Rot — auch bei einer Abweichung nahe 0V, die liegt ja bereits in der grünen
Zone). Ausgang **aus** → der ganze Balken inkl. Pfeil wird einheitlich grau, unabhängig vom
rechnerischen Zahlenwert. Es gibt **keinen** separaten "genau getroffen"-Sonderzustand — die
grüne Zone deckt das bereits ab.

## Berechnung

```
float deviation = received_rms_value - setpoint_voltage;   // V
float devClamped = clamp(deviation, -5.0f, 5.0f);           // Skala sättigt an den Enden
int markerX = 124 + (int)round(devClamped * 20.8f);
float absDev = fabsf(devClamped);
```

## Zeichnen (pro Frame / bei Wertänderung, gleicher Dirty-Check-Rhythmus wie die Ist-Spannung)

```
if (!out) {
    // Ausgang aus: ganzer Balken + Pfeil einheitlich grau, unabhängig von devClamped.
    fillRoundRect(TRACK, COL_CHIPOFF-artiger Ton);
    markerColor = COL_VGREY;   // hell-grau, wie bei "Ausgang aus"-Ist-Spannung
} else {
    // Ausgang ein: Balken zeigt IMMER alle 3 Zonen vollständig, unabhängig von der Position:
    fillRect(rot-links:   x=20,    w=20.8, TFT_RED)
    fillRect(gelb-links:  x=40.8,  w=41.6, TFT_YELLOW)
    fillRect(gruen:       x=82.4,  w=83.2, GRUEN — neue Konstante nötig, z.B. RGB565 aus #3ECF6B)
    fillRect(gelb-rechts: x=165.6, w=41.6, TFT_YELLOW)
    fillRect(rot-rechts:  x=207.2, w=20.8, TFT_RED)
    // Ecken abrunden: entweder über Clip-Maske (wie im SVG-Mockup) oder indem die äusseren
    // Segmente direkt mit abgerundeten Ecken gezeichnet werden (TFT_eSPI kennt kein natives
    // Clipping wie SVG — pragmatisch: Rahmen-Overlay drüberzeichnen wie bei den Chips, oder auf
    // rx verzichten und den Track eckig lassen, das ist optisch ein vertretbarer Kompromiss)
    markerColor =
        absDev <= DEV_ZONE_GREEN  ? GRUEN :
        absDev <= DEV_ZONE_YELLOW ? TFT_YELLOW :
                                     TFT_RED;
}
// Pfeil (Dreieck) an markerX zeichnen, Farbe = markerColor, Geometrie siehe oben.
// Zonen-Beschriftung ist statisch (Teil von drawBackground(), einmalig).
```

## Update-Rate, Zonen-Grenzen (Variante A)

- Update-Rate: identisch zur Ist-Spannung — selber Dirty-Check-Zyklus in `updateDisplay()`
  (100ms-Task, aber nur neu zeichnen, wenn sich der gerundete `markerX`, die Zone oder `out`
  geändert hat, analog zum bestehenden Muster bei `nVolt`/`nVoltCol`).
- Zonen-Grenzen (±2V / ±4V / ±5V) als **benannte Konstanten** definieren (z.B.
  `DEV_ZONE_GREEN`, `DEV_ZONE_YELLOW`, `DEV_ZONE_MAX`), nicht als verstreute Zahlen-Literale —
  damit ein späteres "einstellbar über die Web-API" eine reine Wertequelle-Änderung ist, keine
  Neuentwicklung der Zeichenlogik. Die Anpassbarkeit selbst ist **nicht** Teil dieses Auftrags.
  Diese Konstanten werden für beide Varianten geteilt.

## Referenz-Mockups Variante A (finaler Stand, SVG 240×320, deckungsgleich mit dem Display)

### A · Zustand 1: Normal (Limit ein), Regelung trifft genau

Pfeil und Zone sind grün, weil eine Abweichung von 0V innerhalb der grünen Zone (±2V) liegt.

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="240" height="320" viewBox="0 0 240 320"><rect width="240" height="320" fill="#000"/><rect x="0" y="0" width="240" height="40" fill="#050505"/><circle cx="17" cy="25" r="2" fill="#FFFFFF"/><path d="M 12.0 20.2 A 7 7 0 0 1 22.0 20.2" fill="none" stroke="#FFFFFF" stroke-width="2.3"/><path d="M 7.6 16.0 A 13 13 0 0 1 26.4 16.0" fill="none" stroke="#FFFFFF" stroke-width="2.3"/><rect x="158" y="5" width="8" height="20" rx="4" fill="none" stroke="#FFFFFF" stroke-width="1.6"/><circle cx="162" cy="30" r="6" fill="#FFFFFF"/><rect x="160" y="15" width="4" height="16" fill="#FFFFFF"/><text x="232" y="26" font-size="20" fill="#FFFFFF" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="end">35&#176;C</text><rect x="12" y="46" width="224" height="106" rx="9" fill="none" stroke="#55606b" stroke-width="2"/><polygon points="45,65 30,93 60,93" fill="#3a3f46" stroke="#3a3f46" stroke-width="4" stroke-linejoin="round"/><rect x="42" y="74" width="6" height="7" fill="#000"/><circle cx="45" cy="87" r="1.5" fill="#000"/><text x="228" y="92" font-size="38" fill="#FFD400" font-family="'Segoe UI',Arial,sans-serif" font-weight="800" text-anchor="end">230 V</text><defs><clipPath id="pa1"><rect x="20" y="122" width="208" height="7" rx="3.5"/></clipPath></defs><g clip-path="url(#pa1)"><rect x="20" y="122" width="20.8" height="7" fill="#FF3222"/><rect x="40.8" y="122" width="41.6" height="7" fill="#FFD400"/><rect x="82.4" y="122" width="83.2" height="7" fill="#3ECF6B"/><rect x="165.6" y="122" width="41.6" height="7" fill="#FFD400"/><rect x="207.2" y="122" width="20.8" height="7" fill="#FF3222"/></g><line x1="124" y1="106" x2="124" y2="136" stroke="#ffffff" stroke-width="1" opacity="0.5"/><polygon points="124,122 117,110 131,110" fill="#3ECF6B"/><text x="40.8" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">-4V</text><text x="82.4" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">-2V</text><text x="165.6" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">2V</text><text x="207.2" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">4V</text><rect x="12" y="170" width="68" height="54" rx="9" fill="#0c0c0c" stroke="#FF3222" stroke-width="3"/><polygon points="49.4,178.0 36.5,199.7 46.4,199.7 42.6,216.0 55.5,193.6 45.6,193.6" fill="#FF3222"/><rect x="90" y="170" width="68" height="54" rx="9" fill="#0c0c0c" stroke="#FFD400" stroke-width="3"/><g transform="translate(114.4,184.2) scale(1.6)"><line x1="0" y1="8" x2="9" y2="8" stroke="#FFD400" stroke-width="1.8"/><path d="M 6 4.8 L 10.5 8 L 6 11.2 Z" fill="#FFD400"/><line x1="12.5" y1="2" x2="12.5" y2="14" stroke="#FFD400" stroke-width="2"/></g><rect x="168" y="170" width="68" height="54" rx="9" fill="#0c0c0c" stroke="#1E63D6" stroke-width="3"/><g transform="translate(192.25,185.0) scale(1.5)"><rect x="0" y="7.04" width="13.12" height="8.64" rx="1.8" fill="#1E63D6"/><path d="M 2.624 7.04 v-2.24 a 3.9359999999999995 3.9359999999999995 0 0 1 7.871999999999999 0 v 2.24" fill="none" stroke="#1E63D6" stroke-width="1.7"/></g><rect x="12" y="252" width="68" height="58" rx="9" fill="#0b0b0b" stroke="#3a3f46" stroke-width="2"/><circle cx="29" cy="269" r="11" fill="#2f3339"/><text x="29" y="274" font-size="15" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">1</text><text x="46.0" y="298" font-size="17" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="500" text-anchor="middle">50 V</text><rect x="90" y="252" width="68" height="58" rx="9" fill="#0b0b0b" stroke="#3a3f46" stroke-width="2"/><circle cx="107" cy="269" r="11" fill="#2f3339"/><text x="107" y="274" font-size="15" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">2</text><text x="124.0" y="298" font-size="17" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="500" text-anchor="middle">150 V</text><rect x="168" y="252" width="68" height="58" rx="9" fill="#0d1f3c" stroke="#1E63D6" stroke-width="3"/><circle cx="185" cy="269" r="11" fill="#1E63D6"/><text x="185" y="274" font-size="15" fill="#fff" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">3</text><text x="202.0" y="298" font-size="17" fill="#FFFFFF" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">230 V</text><rect x="219" y="264.72" width="10.66" height="7.02" rx="1.8" fill="#1E63D6"/><path d="M 221.132 264.72 v-1.82 a 3.198 3.198 0 0 1 6.396 0 v 1.82" fill="none" stroke="#1E63D6" stroke-width="1.7"/></svg>
```

### A · Zustand 2: Ohne Strombegrenzung, Abweichung -3.4V (gelbe Zone)

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="240" height="320" viewBox="0 0 240 320"><rect width="240" height="320" fill="#000"/><rect x="0" y="0" width="240" height="40" fill="#050505"/><circle cx="17" cy="25" r="2" fill="#FFFFFF"/><path d="M 12.0 20.2 A 7 7 0 0 1 22.0 20.2" fill="none" stroke="#FFFFFF" stroke-width="2.3"/><path d="M 7.6 16.0 A 13 13 0 0 1 26.4 16.0" fill="none" stroke="#FFFFFF" stroke-width="2.3"/><rect x="158" y="5" width="8" height="20" rx="4" fill="none" stroke="#FFFFFF" stroke-width="1.6"/><circle cx="162" cy="30" r="6" fill="#FFFFFF"/><rect x="160" y="15" width="4" height="16" fill="#FFFFFF"/><text x="232" y="26" font-size="20" fill="#FFFFFF" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="end">35&#176;C</text><rect x="12" y="46" width="224" height="106" rx="9" fill="none" stroke="#55606b" stroke-width="2"/><polygon points="45,65 30,93 60,93" fill="#FFD400" stroke="#FFD400" stroke-width="4" stroke-linejoin="round"/><rect x="42" y="74" width="6" height="7" fill="#000"/><circle cx="45" cy="87" r="1.5" fill="#000"/><text x="228" y="92" font-size="38" fill="#FF3222" font-family="'Segoe UI',Arial,sans-serif" font-weight="800" text-anchor="end">230 V</text><defs><clipPath id="pa2"><rect x="20" y="122" width="208" height="7" rx="3.5"/></clipPath></defs><g clip-path="url(#pa2)"><rect x="20" y="122" width="20.8" height="7" fill="#FF3222"/><rect x="40.8" y="122" width="41.6" height="7" fill="#FFD400"/><rect x="82.4" y="122" width="83.2" height="7" fill="#3ECF6B"/><rect x="165.6" y="122" width="41.6" height="7" fill="#FFD400"/><rect x="207.2" y="122" width="20.8" height="7" fill="#FF3222"/></g><line x1="124" y1="106" x2="124" y2="136" stroke="#ffffff" stroke-width="1" opacity="0.5"/><polygon points="53,122 46,110 60,110" fill="#FFD400"/><text x="40.8" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">-4V</text><text x="82.4" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">-2V</text><text x="165.6" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">2V</text><text x="207.2" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">4V</text><rect x="12" y="170" width="68" height="54" rx="9" fill="#0c0c0c" stroke="#FF3222" stroke-width="3"/><polygon points="49.4,178.0 36.5,199.7 46.4,199.7 42.6,216.0 55.5,193.6 45.6,193.6" fill="#FF3222"/><rect x="90" y="170" width="68" height="54" rx="9" fill="#080808" stroke="#3a3f46" stroke-width="2"/><g transform="translate(114.4,184.2) scale(1.6)"><line x1="0" y1="8" x2="9" y2="8" stroke="#3a3f46" stroke-width="1.8"/><path d="M 6 4.8 L 10.5 8 L 6 11.2 Z" fill="#3a3f46"/><line x1="12.5" y1="2" x2="12.5" y2="14" stroke="#3a3f46" stroke-width="2"/></g><rect x="168" y="170" width="68" height="54" rx="9" fill="#0c0c0c" stroke="#1E63D6" stroke-width="3"/><g transform="translate(192.25,185.0) scale(1.5)"><rect x="0" y="7.04" width="13.12" height="8.64" rx="1.8" fill="#1E63D6"/><path d="M 2.624 7.04 v-2.24 a 3.9359999999999995 3.9359999999999995 0 0 1 7.871999999999999 0 v 2.24" fill="none" stroke="#1E63D6" stroke-width="1.7"/></g><rect x="12" y="252" width="68" height="58" rx="9" fill="#0b0b0b" stroke="#3a3f46" stroke-width="2"/><circle cx="29" cy="269" r="11" fill="#2f3339"/><text x="29" y="274" font-size="15" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">1</text><text x="46.0" y="298" font-size="17" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="500" text-anchor="middle">50 V</text><rect x="90" y="252" width="68" height="58" rx="9" fill="#0b0b0b" stroke="#3a3f46" stroke-width="2"/><circle cx="107" cy="269" r="11" fill="#2f3339"/><text x="107" y="274" font-size="15" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">2</text><text x="124.0" y="298" font-size="17" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="500" text-anchor="middle">150 V</text><rect x="168" y="252" width="68" height="58" rx="9" fill="#0d1f3c" stroke="#1E63D6" stroke-width="3"/><circle cx="185" cy="269" r="11" fill="#1E63D6"/><text x="185" y="274" font-size="15" fill="#fff" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">3</text><text x="202.0" y="298" font-size="17" fill="#FFFFFF" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">230 V</text><rect x="219" y="264.72" width="10.66" height="7.02" rx="1.8" fill="#1E63D6"/><path d="M 221.132 264.72 v-1.82 a 3.198 3.198 0 0 1 6.396 0 v 1.82" fill="none" stroke="#1E63D6" stroke-width="1.7"/></svg>
```

### A · Zustand 3: Ausgang aus, Strombegrenzung aus (Grenzfall)

Dies ist der einzige Zustand, in dem Balken und Pfeil grau werden — ausschliesslich weil
`out == false`, unabhängig vom rechnerischen Abweichungswert.

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="240" height="320" viewBox="0 0 240 320"><rect width="240" height="320" fill="#000"/><rect x="0" y="0" width="240" height="40" fill="#050505"/><circle cx="17" cy="25" r="2" fill="#FFFFFF"/><path d="M 12.0 20.2 A 7 7 0 0 1 22.0 20.2" fill="none" stroke="#FFFFFF" stroke-width="2.3"/><path d="M 7.6 16.0 A 13 13 0 0 1 26.4 16.0" fill="none" stroke="#FFFFFF" stroke-width="2.3"/><rect x="158" y="5" width="8" height="20" rx="4" fill="none" stroke="#FFFFFF" stroke-width="1.6"/><circle cx="162" cy="30" r="6" fill="#FFFFFF"/><rect x="160" y="15" width="4" height="16" fill="#FFFFFF"/><text x="232" y="26" font-size="20" fill="#FFFFFF" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="end">35&#176;C</text><rect x="12" y="46" width="224" height="106" rx="9" fill="none" stroke="#55606b" stroke-width="2"/><polygon points="45,65 30,93 60,93" fill="#3a3f46" stroke="#3a3f46" stroke-width="4" stroke-linejoin="round"/><rect x="42" y="74" width="6" height="7" fill="#000"/><circle cx="45" cy="87" r="1.5" fill="#000"/><text x="228" y="92" font-size="38" fill="#C6C6C6" font-family="'Segoe UI',Arial,sans-serif" font-weight="800" text-anchor="end">0 V</text><defs><clipPath id="poffA"><rect x="20" y="122" width="208" height="7" rx="3.5"/></clipPath></defs><g clip-path="url(#poffA)"><rect x="20" y="122" width="208" height="7" fill="#2f3339"/></g><line x1="124" y1="106" x2="124" y2="136" stroke="#ffffff" stroke-width="1" opacity="0.5"/><polygon points="124,122 117,110 131,110" fill="#C6C6C6"/><text x="40.8" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">-4V</text><text x="82.4" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">-2V</text><text x="165.6" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">2V</text><text x="207.2" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">4V</text><rect x="12" y="170" width="68" height="54" rx="9" fill="#080808" stroke="#3a3f46" stroke-width="2"/><polygon points="49.4,178.0 36.5,199.7 46.4,199.7 42.6,216.0 55.5,193.6 45.6,193.6" fill="#3a3f46"/><rect x="90" y="170" width="68" height="54" rx="9" fill="#080808" stroke="#3a3f46" stroke-width="2"/><g transform="translate(114.4,184.2) scale(1.6)"><line x1="0" y1="8" x2="9" y2="8" stroke="#3a3f46" stroke-width="1.8"/><path d="M 6 4.8 L 10.5 8 L 6 11.2 Z" fill="#3a3f46"/><line x1="12.5" y1="2" x2="12.5" y2="14" stroke="#3a3f46" stroke-width="2"/></g><rect x="168" y="170" width="68" height="54" rx="9" fill="#080808" stroke="#3a3f46" stroke-width="2"/><g transform="translate(192.25,185.0) scale(1.5)"><rect x="0" y="7.04" width="13.12" height="8.64" rx="1.8" fill="#3a3f46"/><path d="M 2.624 7.04 v-2.24 a 3.9359999999999995 3.9359999999999995 0 0 1 7.871999999999999 0 v 2.24" fill="none" stroke="#3a3f46" stroke-width="1.7"/></g><rect x="12" y="252" width="68" height="58" rx="9" fill="#0b0b0b" stroke="#3a3f46" stroke-width="2"/><circle cx="29" cy="269" r="11" fill="#2f3339"/><text x="29" y="274" font-size="15" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">1</text><text x="46.0" y="298" font-size="17" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="500" text-anchor="middle">50 V</text><rect x="90" y="252" width="68" height="58" rx="9" fill="#0b0b0b" stroke="#3a3f46" stroke-width="2"/><circle cx="107" cy="269" r="11" fill="#2f3339"/><text x="107" y="274" font-size="15" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">2</text><text x="124.0" y="298" font-size="17" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="500" text-anchor="middle">150 V</text><rect x="168" y="252" width="68" height="58" rx="9" fill="#0d1f3c" stroke="#1E63D6" stroke-width="3"/><circle cx="185" cy="269" r="11" fill="#1E63D6"/><text x="185" y="274" font-size="15" fill="#fff" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">3</text><text x="202.0" y="298" font-size="17" fill="#FFFFFF" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">230 V</text><rect x="219" y="264.72" width="10.66" height="7.02" rx="1.8" fill="#1E63D6"/><path d="M 221.132 264.72 v-1.82 a 3.198 3.198 0 0 1 6.396 0 v 1.82" fill="none" stroke="#1E63D6" stroke-width="1.7"/></svg>
```

---

# Variante B — Füllbalken von der Mitte

Kernidee: der Balken-Track bleibt neutral dunkel; eine farbige Füllung wächst von der Mitte
(0V) aus in Richtung der aktuellen Abweichung — die Füllkante zeigt die Position bereits von
selbst. **Kein separater Pfeil/Dreieck-Marker** (anders als Variante A): die Füllung übernimmt
diese Funktion vollständig, ein zusätzliches Symbol wäre redundant.

Das wirft eine Frage auf: Was ist zu sehen, wenn die Regelung exakt trifft (0V Abweichung) und
damit keine Füllung entsteht? Antwort: ein kurzer, sichtbarer **grüner Strich in der Mitte** —
nur dann, wenn der Ausgang **ein** ist. Ein dauerhafter neutraler Center-Tick als reine
Referenzlinie existiert **nicht** — nur dieser Zustands-Strich. Wie bei Variante A gilt:
Ausgangszustand geht vor — Ausgang **aus** → Balken einheitlich grau, ganz ohne Strich oder
Füllung (siehe A · Zustand 3 oben, identisches Prinzip).

## Berechnung

```
float deviation = received_rms_value - setpoint_voltage;   // V
float devClamped = clamp(deviation, -5.0f, 5.0f);           // Skala sättigt an den Enden
int markerX = 124 + (int)round(devClamped * 20.8f);
float absDev = fabsf(devClamped);
bool tooSmallToFill = absDev < DEV_MARK_MIN;   // ~0.3V — rein render-technische Schwelle, ab
                                                // der eine Füllung als 0-1px-Strich kaum noch
                                                // sichtbar wäre; siehe unten
```

## Zeichnen (pro Frame / bei Wertänderung, gleicher Dirty-Check-Rhythmus wie die Ist-Spannung)

```
if (!out) {
    // Ausgang aus: ganzer Track einheitlich grau, keine Füllung, kein grüner Strich.
    fillRoundRect(TRACK, COL_CHIPOFF-artiger Ton);
} else {
    // Ausgang ein: Track-Hintergrund immer neutral dunkel (z.B. 0x1a1a1a-artiger Ton,
    // dunkler als COL_CHIPOFF, damit sich Füllung/Strich klar abheben).
    fillRoundRect(TRACK, dunkler Neutralton);

    if (tooSmallToFill) {
        // Kurzer grüner Strich in der Mitte statt einer kaum sichtbaren Mini-Füllung.
        fillRoundRect(cx=124, TRACK_Y-Bereich, kurzer dicker vertikaler Strich, GRUEN);
    } else {
        Farbe fillColor =
            absDev <= DEV_ZONE_GREEN  ? GRUEN :
            absDev <= DEV_ZONE_YELLOW ? TFT_YELLOW :
                                         TFT_RED;
        int fillX = min(124, markerX);
        int fillW = abs(markerX - 124);
        fillRect(fillX, TRACK_Y, fillW, TRACK_H, fillColor);
    }
}
// Kein Pfeil/Dreieck zeichnen — bewusst weggelassen (siehe oben).
// Zonen-Beschriftung ist statisch (Teil von drawBackground(), einmalig) — dient hier nur als
// Referenz-Skala, da der Track selbst ausserhalb der Füllung keine Zonenfarben zeigt.
```

## Update-Rate, Zonen-Grenzen (Variante B)

- `DEV_MARK_MIN` (~0.3V) ist **nur** eine Rendering-Schwelle für den grünen Strich, **keine**
  Farbentscheidung — sie legt lediglich fest, ab wann die Füllung breit genug ist, um sinnvoll
  gezeichnet zu werden. Als benannte Konstante definieren.
- Update-Rate: identisch zur Ist-Spannung — selber Dirty-Check-Zyklus in `updateDisplay()`
  (100ms-Task, aber nur neu zeichnen, wenn sich der gerundete `markerX`, die Füllfarbe oder
  `out` geändert hat, analog zum bestehenden Muster bei `nVolt`/`nVoltCol`).
- Zonen-Grenzen (`DEV_ZONE_GREEN`/`DEV_ZONE_YELLOW`/`DEV_ZONE_MAX`) sind dieselben benannten
  Konstanten wie bei Variante A (siehe dort) — nicht doppelt definieren.

## Referenz-Mockups Variante B (finaler Stand, SVG 240×320, deckungsgleich mit dem Display)

### B · Zustand 1: Normal (Limit ein), Regelung trifft genau

Kein Pfeil — stattdessen ein kurzer grüner Strich in der Mitte, weil Ausgang ein und Abweichung
~0V (zu klein für eine sinnvoll sichtbare Füllung).

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="240" height="320" viewBox="0 0 240 320"><rect width="240" height="320" fill="#000"/><rect x="0" y="0" width="240" height="40" fill="#050505"/><circle cx="17" cy="25" r="2" fill="#FFFFFF"/><path d="M 12.0 20.2 A 7 7 0 0 1 22.0 20.2" fill="none" stroke="#FFFFFF" stroke-width="2.3"/><path d="M 7.6 16.0 A 13 13 0 0 1 26.4 16.0" fill="none" stroke="#FFFFFF" stroke-width="2.3"/><rect x="158" y="5" width="8" height="20" rx="4" fill="none" stroke="#FFFFFF" stroke-width="1.6"/><circle cx="162" cy="30" r="6" fill="#FFFFFF"/><rect x="160" y="15" width="4" height="16" fill="#FFFFFF"/><text x="232" y="26" font-size="20" fill="#FFFFFF" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="end">35&#176;C</text><rect x="12" y="46" width="224" height="106" rx="9" fill="none" stroke="#55606b" stroke-width="2"/><polygon points="45,65 30,93 60,93" fill="#3a3f46" stroke="#3a3f46" stroke-width="4" stroke-linejoin="round"/><rect x="42" y="74" width="6" height="7" fill="#000"/><circle cx="45" cy="87" r="1.5" fill="#000"/><text x="228" y="92" font-size="38" fill="#FFD400" font-family="'Segoe UI',Arial,sans-serif" font-weight="800" text-anchor="end">230 V</text><rect x="20" y="122" width="208" height="7" rx="3.5" fill="#1a1a1a"/><rect x="121" y="116" width="6" height="14" rx="3" fill="#3ECF6B"/><text x="40.8" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">-4V</text><text x="82.4" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">-2V</text><text x="165.6" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">2V</text><text x="207.2" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">4V</text><rect x="12" y="170" width="68" height="54" rx="9" fill="#0c0c0c" stroke="#FF3222" stroke-width="3"/><polygon points="49.4,178.0 36.5,199.7 46.4,199.7 42.6,216.0 55.5,193.6 45.6,193.6" fill="#FF3222"/><rect x="90" y="170" width="68" height="54" rx="9" fill="#0c0c0c" stroke="#FFD400" stroke-width="3"/><g transform="translate(114.4,184.2) scale(1.6)"><line x1="0" y1="8" x2="9" y2="8" stroke="#FFD400" stroke-width="1.8"/><path d="M 6 4.8 L 10.5 8 L 6 11.2 Z" fill="#FFD400"/><line x1="12.5" y1="2" x2="12.5" y2="14" stroke="#FFD400" stroke-width="2"/></g><rect x="168" y="170" width="68" height="54" rx="9" fill="#0c0c0c" stroke="#1E63D6" stroke-width="3"/><g transform="translate(192.25,185.0) scale(1.5)"><rect x="0" y="7.04" width="13.12" height="8.64" rx="1.8" fill="#1E63D6"/><path d="M 2.624 7.04 v-2.24 a 3.9359999999999995 3.9359999999999995 0 0 1 7.871999999999999 0 v 2.24" fill="none" stroke="#1E63D6" stroke-width="1.7"/></g><rect x="12" y="252" width="68" height="58" rx="9" fill="#0b0b0b" stroke="#3a3f46" stroke-width="2"/><circle cx="29" cy="269" r="11" fill="#2f3339"/><text x="29" y="274" font-size="15" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">1</text><text x="46.0" y="298" font-size="17" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="500" text-anchor="middle">50 V</text><rect x="90" y="252" width="68" height="58" rx="9" fill="#0b0b0b" stroke="#3a3f46" stroke-width="2"/><circle cx="107" cy="269" r="11" fill="#2f3339"/><text x="107" y="274" font-size="15" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">2</text><text x="124.0" y="298" font-size="17" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="500" text-anchor="middle">150 V</text><rect x="168" y="252" width="68" height="58" rx="9" fill="#0d1f3c" stroke="#1E63D6" stroke-width="3"/><circle cx="185" cy="269" r="11" fill="#1E63D6"/><text x="185" y="274" font-size="15" fill="#fff" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">3</text><text x="202.0" y="298" font-size="17" fill="#FFFFFF" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">230 V</text><rect x="219" y="264.72" width="10.66" height="7.02" rx="1.8" fill="#1E63D6"/><path d="M 221.132 264.72 v-1.82 a 3.198 3.198 0 0 1 6.396 0 v 1.82" fill="none" stroke="#1E63D6" stroke-width="1.7"/></svg>
```

### B · Zustand 2: Ohne Strombegrenzung, Abweichung -3.4V (gelbe Zone)

Kein Pfeil — die Füllkante allein zeigt die Position der Abweichung.

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="240" height="320" viewBox="0 0 240 320"><rect width="240" height="320" fill="#000"/><rect x="0" y="0" width="240" height="40" fill="#050505"/><circle cx="17" cy="25" r="2" fill="#FFFFFF"/><path d="M 12.0 20.2 A 7 7 0 0 1 22.0 20.2" fill="none" stroke="#FFFFFF" stroke-width="2.3"/><path d="M 7.6 16.0 A 13 13 0 0 1 26.4 16.0" fill="none" stroke="#FFFFFF" stroke-width="2.3"/><rect x="158" y="5" width="8" height="20" rx="4" fill="none" stroke="#FFFFFF" stroke-width="1.6"/><circle cx="162" cy="30" r="6" fill="#FFFFFF"/><rect x="160" y="15" width="4" height="16" fill="#FFFFFF"/><text x="232" y="26" font-size="20" fill="#FFFFFF" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="end">35&#176;C</text><rect x="12" y="46" width="224" height="106" rx="9" fill="none" stroke="#55606b" stroke-width="2"/><polygon points="45,65 30,93 60,93" fill="#FFD400" stroke="#FFD400" stroke-width="4" stroke-linejoin="round"/><rect x="42" y="74" width="6" height="7" fill="#000"/><circle cx="45" cy="87" r="1.5" fill="#000"/><text x="228" y="92" font-size="38" fill="#FF3222" font-family="'Segoe UI',Arial,sans-serif" font-weight="800" text-anchor="end">230 V</text><rect x="20" y="122" width="208" height="7" rx="3.5" fill="#1a1a1a"/><defs><clipPath id="pb2"><rect x="20" y="122" width="208" height="7" rx="3.5"/></clipPath></defs><g clip-path="url(#pb2)"><rect x="53.3" y="122" width="70.7" height="7" fill="#FFD400"/></g><text x="40.8" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">-4V</text><text x="82.4" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">-2V</text><text x="165.6" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">2V</text><text x="207.2" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">4V</text><rect x="12" y="170" width="68" height="54" rx="9" fill="#0c0c0c" stroke="#FF3222" stroke-width="3"/><polygon points="49.4,178.0 36.5,199.7 46.4,199.7 42.6,216.0 55.5,193.6 45.6,193.6" fill="#FF3222"/><rect x="90" y="170" width="68" height="54" rx="9" fill="#080808" stroke="#3a3f46" stroke-width="2"/><g transform="translate(114.4,184.2) scale(1.6)"><line x1="0" y1="8" x2="9" y2="8" stroke="#3a3f46" stroke-width="1.8"/><path d="M 6 4.8 L 10.5 8 L 6 11.2 Z" fill="#3a3f46"/><line x1="12.5" y1="2" x2="12.5" y2="14" stroke="#3a3f46" stroke-width="2"/></g><rect x="168" y="170" width="68" height="54" rx="9" fill="#0c0c0c" stroke="#1E63D6" stroke-width="3"/><g transform="translate(192.25,185.0) scale(1.5)"><rect x="0" y="7.04" width="13.12" height="8.64" rx="1.8" fill="#1E63D6"/><path d="M 2.624 7.04 v-2.24 a 3.9359999999999995 3.9359999999999995 0 0 1 7.871999999999999 0 v 2.24" fill="none" stroke="#1E63D6" stroke-width="1.7"/></g><rect x="12" y="252" width="68" height="58" rx="9" fill="#0b0b0b" stroke="#3a3f46" stroke-width="2"/><circle cx="29" cy="269" r="11" fill="#2f3339"/><text x="29" y="274" font-size="15" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">1</text><text x="46.0" y="298" font-size="17" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="500" text-anchor="middle">50 V</text><rect x="90" y="252" width="68" height="58" rx="9" fill="#0b0b0b" stroke="#3a3f46" stroke-width="2"/><circle cx="107" cy="269" r="11" fill="#2f3339"/><text x="107" y="274" font-size="15" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">2</text><text x="124.0" y="298" font-size="17" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="500" text-anchor="middle">150 V</text><rect x="168" y="252" width="68" height="58" rx="9" fill="#0d1f3c" stroke="#1E63D6" stroke-width="3"/><circle cx="185" cy="269" r="11" fill="#1E63D6"/><text x="185" y="274" font-size="15" fill="#fff" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">3</text><text x="202.0" y="298" font-size="17" fill="#FFFFFF" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">230 V</text><rect x="219" y="264.72" width="10.66" height="7.02" rx="1.8" fill="#1E63D6"/><path d="M 221.132 264.72 v-1.82 a 3.198 3.198 0 0 1 6.396 0 v 1.82" fill="none" stroke="#1E63D6" stroke-width="1.7"/></svg>
```

### B · Zustand 3: Ausgang aus, Strombegrenzung aus (Grenzfall)

Track wird einheitlich grau — kein Strich, kein Pfeil, keine Füllung.

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="240" height="320" viewBox="0 0 240 320"><rect width="240" height="320" fill="#000"/><rect x="0" y="0" width="240" height="40" fill="#050505"/><circle cx="17" cy="25" r="2" fill="#FFFFFF"/><path d="M 12.0 20.2 A 7 7 0 0 1 22.0 20.2" fill="none" stroke="#FFFFFF" stroke-width="2.3"/><path d="M 7.6 16.0 A 13 13 0 0 1 26.4 16.0" fill="none" stroke="#FFFFFF" stroke-width="2.3"/><rect x="158" y="5" width="8" height="20" rx="4" fill="none" stroke="#FFFFFF" stroke-width="1.6"/><circle cx="162" cy="30" r="6" fill="#FFFFFF"/><rect x="160" y="15" width="4" height="16" fill="#FFFFFF"/><text x="232" y="26" font-size="20" fill="#FFFFFF" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="end">35&#176;C</text><rect x="12" y="46" width="224" height="106" rx="9" fill="none" stroke="#55606b" stroke-width="2"/><polygon points="45,65 30,93 60,93" fill="#3a3f46" stroke="#3a3f46" stroke-width="4" stroke-linejoin="round"/><rect x="42" y="74" width="6" height="7" fill="#000"/><circle cx="45" cy="87" r="1.5" fill="#000"/><text x="228" y="92" font-size="38" fill="#C6C6C6" font-family="'Segoe UI',Arial,sans-serif" font-weight="800" text-anchor="end">0 V</text><rect x="20" y="122" width="208" height="7" rx="3.5" fill="#2f3339"/><text x="40.8" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">-4V</text><text x="82.4" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">-2V</text><text x="165.6" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">2V</text><text x="207.2" y="143" font-size="13" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="600" text-anchor="middle">4V</text><rect x="12" y="170" width="68" height="54" rx="9" fill="#080808" stroke="#3a3f46" stroke-width="2"/><polygon points="49.4,178.0 36.5,199.7 46.4,199.7 42.6,216.0 55.5,193.6 45.6,193.6" fill="#3a3f46"/><rect x="90" y="170" width="68" height="54" rx="9" fill="#080808" stroke="#3a3f46" stroke-width="2"/><g transform="translate(114.4,184.2) scale(1.6)"><line x1="0" y1="8" x2="9" y2="8" stroke="#3a3f46" stroke-width="1.8"/><path d="M 6 4.8 L 10.5 8 L 6 11.2 Z" fill="#3a3f46"/><line x1="12.5" y1="2" x2="12.5" y2="14" stroke="#3a3f46" stroke-width="2"/></g><rect x="168" y="170" width="68" height="54" rx="9" fill="#080808" stroke="#3a3f46" stroke-width="2"/><g transform="translate(192.25,185.0) scale(1.5)"><rect x="0" y="7.04" width="13.12" height="8.64" rx="1.8" fill="#3a3f46"/><path d="M 2.624 7.04 v-2.24 a 3.9359999999999995 3.9359999999999995 0 0 1 7.871999999999999 0 v 2.24" fill="none" stroke="#3a3f46" stroke-width="1.7"/></g><rect x="12" y="252" width="68" height="58" rx="9" fill="#0b0b0b" stroke="#3a3f46" stroke-width="2"/><circle cx="29" cy="269" r="11" fill="#2f3339"/><text x="29" y="274" font-size="15" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">1</text><text x="46.0" y="298" font-size="17" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="500" text-anchor="middle">50 V</text><rect x="90" y="252" width="68" height="58" rx="9" fill="#0b0b0b" stroke="#3a3f46" stroke-width="2"/><circle cx="107" cy="269" r="11" fill="#2f3339"/><text x="107" y="274" font-size="15" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">2</text><text x="124.0" y="298" font-size="17" fill="#8A9099" font-family="'Segoe UI',Arial,sans-serif" font-weight="500" text-anchor="middle">150 V</text><rect x="168" y="252" width="68" height="58" rx="9" fill="#0d1f3c" stroke="#1E63D6" stroke-width="3"/><circle cx="185" cy="269" r="11" fill="#1E63D6"/><text x="185" y="274" font-size="15" fill="#fff" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">3</text><text x="202.0" y="298" font-size="17" fill="#FFFFFF" font-family="'Segoe UI',Arial,sans-serif" font-weight="700" text-anchor="middle">230 V</text><rect x="219" y="264.72" width="10.66" height="7.02" rx="1.8" fill="#1E63D6"/><path d="M 221.132 264.72 v-1.82 a 3.198 3.198 0 0 1 6.396 0 v 1.82" fill="none" stroke="#1E63D6" stroke-width="1.7"/></svg>
```

---

## Nicht Teil dieses Auftrags

- Chips (Ausgang/Limit/Regelung), Kopfzeile — unverändert, nicht anfassen.
- Andere Screens (`drawSettingsScreen`, `drawHomingScreen`, `drawErrorScreen`,
  `drawVmUpdateScreen`, `drawOtaScreen`) — unverändert.
- Web-API-Anpassbarkeit der Zonen-Grenzen — nur strukturell vorbereiten (benannte Konstanten),
  nicht implementieren.
