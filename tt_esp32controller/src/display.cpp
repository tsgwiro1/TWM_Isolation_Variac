// TWM Isolation Variac – TFT-Display: Screens und Update-Task (#10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#include "display.h"
#include <WiFi.h>     // drawHomingScreen zeigt die IP
#include <math.h>     // lroundf für die Rundung der Anzeigewerte
#include "pins.h"
#include "state.h"
#include "logging.h"
#include "comm.h"     // received_rms_value, vmUpdate*-Status (#32)
#include "motor.h"    // stepper.currentPosition()
#include "actions.h"  // A_* Zustände/Presets für die Anzeige
#include "system.h"   // wiperTemp
#include "icons.h"    // XBM-Symbole (WLAN) für die Kopfzeile
// Ist-Spannung nutzt einen eigenen grossen Font (Arial Bold @56px, grösser als die max. 24pt
// der TFT_eSPI-FreeFonts). GFXglyph/GFXfont kommen aus TFT_eSPI (via display.h); bei aktivem
// LOAD_GFXFF sind die Standard-FreeFonts schon eingebunden, daher hier nur der eigene Font.
#include "fonts/font_volt56.h"
#include "warn_icon.h"     // generiertes Warndreieck (runde Ecken), RGB565 in Gelb + Dim-Grau
#include "thermo_icon.h"   // generiertes Thermometer-Icon (weiss), RGB565

TFT_eSPI tft = TFT_eSPI();
SemaphoreHandle_t tftMutex;  // Mutex zum Schutz des TFT-Displays

// ============================================================================
//  Normalbetrieb-Screen (Redesign) – 240x320 Portrait
//  Kopfzeile (schwarz) mit WLAN- und Thermometer-Icon + Temperatur, grosse farbcodierte
//  Ist-Spannung (eigener Font), Warndreieck + Regelabweichungsbalken, 3 Chips, 3 Presets.
//  Icons teils Zeichenprimitive, teils generierte Bitmaps (warn_icon.h/thermo_icon.h);
//  Dirty-Check pro Zone, Sprites (Ist/Balken/Temp) gegen Flackern.
// ============================================================================

// Farben (RGB565)
static const uint16_t COL_BLUE    = 0x1A9B;      // dunkleres Blau (mehr Kontrast zur weissen Preset-Nummer)
static const uint16_t COL_CHIPOFF = 0x39E7;      // inaktiver Chip-Rand / inaktives Icon (dezent grau)
static const uint16_t COL_LABEL   = TFT_DARKGREY;// graue Beschriftungen
static const uint16_t COL_VGREY   = TFT_LIGHTGREY;// Spannung bei Ausgang AUS
static const uint16_t COL_FRAME   = 0x52AA;      // grauer Rundrahmen um den Spannungs-/Warnbereich
static const uint16_t COL_GREEN   = 0x3E6D;      // Regelabweichung: grün (Treffer / grüne Zone)
static const uint16_t COL_BARBG   = 0x18C3;      // Balken-Track (Variante B, neutral dunkel)
static const uint16_t COL_BAROFF  = 0x2987;      // Balken-Track bei Ausgang aus (grau)

// Regelabweichungs-Balken (ersetzt die Zielspannungs-Zeile): Geometrie + Zonen
#define BAR_X    20
#define BAR_Y    128          // 4 px höher (inkl. Pfeil/Füllung; Labels folgen in drawBackground)
#define BAR_W    208
#define BAR_H    7            // Variante B (Füllbalken): volle Dicke
#define BAR_HA   4            // Variante A (Zonen+Pfeil): halbe Dicke
#define BAR_CX   124          // 0-V-Mitte
#define BAR_PXV  20.8f        // px pro Volt (208/2/5V)
#define DEV_ZONE_GREEN   2.0f
#define DEV_ZONE_YELLOW  4.0f
#define DEV_ZONE_MAX     5.0f
#define DEV_MARK_MIN     0.3f // Variante B: unterhalb -> grüner Strich statt Mini-Füllung
// Sprite für den Balken (flackerfrei via Push). Deckt x=14..233, y=(BAR_Y-16)..(BAR_Y+9) ab
// (ohne Rahmenrand x=12/13 & 234/235, ohne Zonen-Labels ab y=143).
#define BAR_SPR_X   14
#define BAR_SPR_Y   (BAR_Y - 16)
#define BAR_SPR_W   220
#define BAR_SPR_H   26

// Farbzustand der grossen Spannung
enum VoltColor : uint8_t { VCOL_GREY, VCOL_YELLOW, VCOL_RED };

// Welches WLAN-Symbol gerade gilt (#36)
enum WifiIcon : uint8_t { WIFI_ICON_NONE, WIFI_ICON_CONNECTED, WIFI_ICON_AP, WIFI_ICON_UNSET = 255 };

struct displayValues {
  // Kopfzeile / gemeinsam
  float   temp;
  uint8_t wifiIcon;
  // Settings-Modus (unverändert genutzt)
  int   stepperPos;
  float voltage;
  int   preset1, preset2, preset3;
  int   setup1, setup2;
  // Normalbetrieb (Redesign)
  int      nVolt;      // gerundete Ist-Spannung
  uint8_t  nVoltCol;   // VoltColor
  uint16_t nWarnCol;   // aktuelle Farbe des Achtung-Dreiecks (Dirty-Check, 0 = noch nie gezeichnet)
  uint8_t  nOut, nLimit, nReg;   // Chip-Zustände (3 bedienbare Chips)
  int      nPreset[3]; // Preset-Spannungen
  int8_t   nActive;    // aktives Preset (0..2) oder -1
  uint8_t  nHeld;      // Regelung hält den Presetwert (Schloss)
  // Regelabweichungs-Balken (ersetzt die Zielspannungs-Zeile)
  int      nMarkerX;   // Marker-/Füllposition (px)
  uint16_t nBarCol;    // aktuelle Zonenfarbe
  uint8_t  nBarOut;    // Ausgang-Zustand des Balkens
  uint8_t  nVariant;   // aktive Balken-Variante (0/1)
};

static displayValues actDispValues;

// Warnzeile im Setup-Screen (GitHub-#3): Plausibilitätswarnungen der Kalibrierung.
static char settingsWarning[40] = "";
static volatile bool settingsWarningDirty = false;

/**
 * @brief Setzt (oder löscht, bei "") die Warnzeile im Setup-Screen (GitHub-#3).
 */
void setSettingsWarning(const char* msg) {
    strncpy(settingsWarning, msg ? msg : "", sizeof(settingsWarning) - 1);
    settingsWarning[sizeof(settingsWarning) - 1] = '\0';
    settingsWarningDirty = true;
}

/**
 * @brief Sperrt den TFT-Mutex für exklusiven Display-Zugriff.
 */
void tftStartWrite() {
  if (xSemaphoreTake(tftMutex, portMAX_DELAY) != pdTRUE) {
    logMessage(LOG_ERROR,"FATAL: Could not take TFT Mutex");
  }
}

/**
 * @brief Gibt den TFT-Mutex nach Abschluss der Zeichenoperationen wieder frei.
 */
void tftEndWrite() {
  xSemaphoreGive(tftMutex);
}

// ------------------------------------------------------------------ Icon-Primitive
// Alle Icons zeichnen um den Mittelpunkt (cx, cy).

// Chip-Icons (grösser, gefüllt/kräftig): zentriert um (cx, cy).
static void icoBolt(int cx, int cy, uint16_t c) {          // Blitz (Ausgang)
  tft.fillTriangle(cx + 1, cy - 13, cx - 8, cy + 3, cx + 2, cy - 1, c);
  tft.fillTriangle(cx - 1, cy + 13, cx + 8, cy - 3, cx - 2, cy + 1, c);
}

static void icoLimit(int cx, int cy, uint16_t c) {         // Pfeil auf Anschlag "->|" (Limit)
  tft.fillRect(cx - 13, cy - 1, 15, 3, c);
  tft.fillTriangle(cx + 2, cy - 7, cx + 2, cy + 7, cx + 10, cy, c);
  tft.fillRect(cx + 12, cy - 10, 3, 21, c);
}

static void icoLock(int cx, int cy, uint16_t c) {          // Schloss (Regelung hält / Preset gehalten)
  tft.fillRoundRect(cx - 9, cy - 1, 18, 14, 3, c);         // Körper
  tft.drawRoundRect(cx - 6, cy - 11, 12, 13, 5, c);        // Bügel
  tft.drawRoundRect(cx - 5, cy - 11, 10, 13, 5, c);        // Bügel (2 px)
  tft.fillCircle(cx, cy + 6, 2, TFT_BLACK);                // Schlüsselloch
}

// (Warndreieck wird jetzt als generiertes Icon gepusht — siehe warn_icon.h; die frühere
//  primitive fillRoundTri/icoWarnTri-Zeichnung entfiel wegen unsauberer Ecken.)

// Dickerer Rahmen: t konzentrische Rechtecke.
static void thickRoundRect(int x, int y, int w, int h, int r, int t, uint16_t c) {
  for (int i = 0; i < t; i++) tft.drawRoundRect(x + i, y + i, w - 2 * i, h - 2 * i, (r - i > 1) ? r - i : 1, c);
}

// ------------------------------------------------------------------ Double-Buffering-Sprites
// Grosse Zahlen/Balken/Temperatur werden in Sprites gezeichnet und in einem Rutsch gepusht
// (flackerfrei statt löschen -> neu zeichnen). Sprites werden einmal angelegt.
static TFT_eSprite sprIst  = TFT_eSprite(&tft);
static TFT_eSprite sprBar  = TFT_eSprite(&tft);   // Regelabweichungs-Balken
static TFT_eSprite sprTemp = TFT_eSprite(&tft);   // Kopfzeile: Temperatur (Symbol + Wert)
static int spritesState = 0;   // 0 = uninitialisiert, 1 = ok, 2 = Anlegen fehlgeschlagen

static void ensureSprites() {
  if (spritesState != 0) return;
  sprIst.setColorDepth(16);
  sprBar.setColorDepth(16);
  sprTemp.setColorDepth(16);
  bool ok = (sprIst.createSprite(156, 50) != nullptr)
         && (sprBar.createSprite(BAR_SPR_W, BAR_SPR_H) != nullptr)
         && (sprTemp.createSprite(94, 30) != nullptr);   // nur bis y31 -> überdeckt die obere Rahmenlinie (y36) nicht
  spritesState = ok ? 1 : 2;
}

// ------------------------------------------------------------------ Kopfzeile
/**
 * @brief Ermittelt das passende WLAN-Symbol aus dem aktuellen Systemzustand (#36).
 */
static uint8_t currentWifiIcon() {
    if (currentSystemState == STATE_WIFIMANAGER_AP) return WIFI_ICON_AP;
    if (WiFi.status() == WL_CONNECTED)              return WIFI_ICON_CONNECTED;
    return WIFI_ICON_NONE;
}

static void drawWifi(uint8_t st) {
  tft.fillRect(2, 1, 40, 26, TFT_BLACK);
  // 2 px grösser als zuvor; vertikal auf die Mitte des Temp-Icons zentriert (Symbol y4..22, Mitte 13).
  const int cx = 17, by = 20;
  if (st == WIFI_ICON_CONNECTED || st == WIFI_ICON_AP) {
    tft.fillCircle(cx, by, 2, TFT_WHITE);
    tft.drawSmoothArc(cx, by, 9,  7,  138, 222, TFT_WHITE, TFT_BLACK);
    tft.drawSmoothArc(cx, by, 16, 14, 138, 222, TFT_WHITE, TFT_BLACK);
    if (st == WIFI_ICON_AP) tft.fillCircle(cx + 12, by - 3, 3, TFT_WHITE);   // Such-Indikator (Config-AP)
  }
  // WIFI_ICON_NONE: leer
}

// Temperatur (Symbol + Wert + °C) flackerfrei in ein Sprite; Direktzeichnen als Fallback.
// Koordinaten in Geräte-Pixeln; Offset (OX,OY) verschiebt sie in den Sprite-Ursprung.
static void drawTemp(float t, bool ok) {
  ensureSprites();
  TFT_eSPI* g; int OX, OY;
  if (spritesState == 1) { sprTemp.fillSprite(TFT_BLACK); g = &sprTemp; OX = 146; OY = 1; }
  else { tft.fillRect(146, 1, 94, 30, TFT_BLACK); g = &tft; OX = 0; OY = 0; }
  // Thermometer-Icon (generiert, weiss). pushImage NICHT über den TFT_eSPI*-Zeiger (nicht virtuell)
  // -> auf dem konkreten Objekt aufrufen. SwapBytes: Bitmap liegt als Standard-RGB565 vor.
  if (spritesState == 1) {
    sprTemp.setSwapBytes(true);
    sprTemp.pushImage(159 - OX, 1 - OY, THERMO_W, THERMO_H, thermoIcon);
    sprTemp.setSwapBytes(false);
  } else {
    bool sw = tft.getSwapBytes(); tft.setSwapBytes(true);
    tft.pushImage(159, 1, THERMO_W, THERMO_H, thermoIcon);
    tft.setSwapBytes(sw);
  }
  // Wert (Font 4) + °C, vertikal an Icon/WLAN angeglichen (Mitte y16, etwas tiefer als zuvor)
  g->setTextColor(TFT_WHITE);
  g->setTextPadding(0);
  g->setTextDatum(MR_DATUM);
  String s = ok ? String((int)lroundf(t)) : String("N/A");
  g->drawString(s, 210 - OX, 18 - OY, 4);
  if (ok) {
    g->drawCircle(215 - OX, 9 - OY, 3, TFT_WHITE);   // Grad-Zeichen
    g->setTextDatum(ML_DATUM);
    g->drawString("C", 220 - OX, 18 - OY, 4);
  }
  if (spritesState == 1) sprTemp.pushSprite(146, 1);
}

// ------------------------------------------------------------------ Spannung / Ziel
// Ist-Spannung: grosser, fetter, farbcodierter Wert, rechtsbündig im Rahmen.
// (Zielspannung als Text entfällt — sie bleibt über den blauen Preset sichtbar; die
//  Regelabweichung zeigt der Balken darunter.)
static void drawIstValue(int v, uint16_t col) {
  ensureSprites();
  if (spritesState == 1) {
    sprIst.fillSprite(TFT_BLACK);
    sprIst.setFreeFont(&fontVolt56);
    sprIst.setTextDatum(MR_DATUM);
    sprIst.setTextColor(col, TFT_BLACK);
    sprIst.drawString(String(v) + " V", 154, 25);
    sprIst.pushSprite(74, 45);
  } else {
    tft.fillRect(74, 45, 156, 50, TFT_BLACK);
    tft.setFreeFont(&fontVolt56);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(col);
    tft.setTextPadding(0);
    tft.drawString(String(v) + " V", 228, 70);
    tft.setTextFont(2);
  }
}

// ------------------------------------------------------------------ Chips
// Schalter-Chip = nur Symbol (wie die Taster-LED): leuchtet farbig bei EIN, grau bei AUS.
static void drawToggleChip(int x, int y, int w, int h, int kind, bool on, uint16_t col) {
  uint16_t ic = on ? col : COL_CHIPOFF;
  tft.fillRoundRect(x, y, w, h, 9, TFT_BLACK);
  thickRoundRect(x, y, w, h, 9, on ? 3 : 2, ic);   // dickerer Rahmen, grössere Ecken-Radien
  int cx = x + w / 2, cy = y + h / 2;
  if      (kind == 0) icoBolt(cx, cy, ic);
  else if (kind == 1) icoLimit(cx, cy, ic);
  else                icoLock(cx, cy, ic);   // Regelung = Schloss (hält die Spannung)
}

// ------------------------------------------------------------------ Presets
static void drawPreset(int x, int y, int w, int h, int num, int val, bool active, bool held) {
  tft.fillRoundRect(x, y, w, h, 9, TFT_BLACK);
  thickRoundRect(x, y, w, h, 9, active ? 3 : 2, active ? COL_BLUE : COL_CHIPOFF);   // dickerer Rahmen, grössere Ecken-Radien
  // Nummern-Badge
  tft.fillCircle(x + 16, y + 16, 10, active ? COL_BLUE : COL_CHIPOFF);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(active ? TFT_WHITE : TFT_BLACK);
  tft.setTextPadding(0);
  tft.drawString(String(num), x + 16, y + 16, 2);
  // Wert
  tft.setTextColor(active ? TFT_WHITE : COL_LABEL);
  tft.drawString(String(val) + " V", x + w / 2, y + h - 13, 2);
  // Schloss (Regelung hält den Wert)
  if (held) {
    int lx = x + w - 15, ly = y + 7;
    tft.fillRoundRect(lx, ly + 4, 9, 7, 2, COL_BLUE);
    tft.drawRoundRect(lx + 2, ly, 5, 6, 2, COL_BLUE);
  }
}

// ------------------------------------------------------------------ Regelabweichungs-Balken
// Ersetzt die frühere Zielspannungs-Zeile: zeigt Ist - Ziel auf einer Skala -5V..+5V.
// Zwei Varianten (per Langdruck auf die Regelungstaste umschaltbar).

// Beide Varianten zeichnen in ein Sprite (Ursprung ox/oy) und werden in einem Rutsch gepusht
// -> kein Clear-then-Draw auf dem TFT, damit flackert der bewegte Pfeil nicht mehr.
// Fallback (Sprite-Anlage fehlgeschlagen): Direktzeichnen auf tft mit ox=oy=0 nach Clear.

// Variante A: feste Farbzonen (rot/gelb/grün) + Pfeil-Marker; Linie nur halb so dick (BAR_HA).
static void renderBarA(TFT_eSPI& g, int ox, int oy, bool out, int markerX, uint16_t markerCol) {
  int by = BAR_Y - oy, ay = (BAR_Y - 12) - oy;
  int tickTop = (BAR_Y - 3) - oy;                 // feiner 0-V-Strich, ragt über/unter die Linie
  if (!out) {
    g.fillRoundRect(BAR_X - ox, by, BAR_W, BAR_HA, 2, COL_BAROFF);
    g.drawFastVLine(BAR_CX - ox, tickTop, BAR_HA + 6, TFT_WHITE);
    g.fillTriangle(BAR_CX - ox, by, BAR_CX - ox - 7, ay, BAR_CX - ox + 7, ay, COL_VGREY);
    return;
  }
  // Zonen (vollständig, symmetrisch um die Mitte): rot | gelb | grün | gelb | rot
  g.fillRect(20  - ox, by, 21, BAR_HA, TFT_RED);
  g.fillRect(41  - ox, by, 41, BAR_HA, TFT_YELLOW);
  g.fillRect(82  - ox, by, 84, BAR_HA, COL_GREEN);
  g.fillRect(166 - ox, by, 41, BAR_HA, TFT_YELLOW);
  g.fillRect(207 - ox, by, 21, BAR_HA, TFT_RED);
  g.drawFastVLine(BAR_CX - ox, tickTop, BAR_HA + 6, TFT_WHITE);     // 0-V-Markierung
  int ax = constrain(markerX, BAR_X + 7, BAR_X + BAR_W - 7) - ox;   // Spitze bleibt in den Zonen
  g.fillTriangle(ax, by, ax - 7, ay, ax + 7, ay, markerCol);
}

// Variante B: neutraler Track, farbige Füllung von der Mitte bis zur Abweichung (volle Dicke);
// grüner Strich in der Mitte, solange die Abweichung ~0 ist.
static void renderBarB(TFT_eSPI& g, int ox, int oy, bool out, int markerX, uint16_t fillCol, float absDev) {
  int by = BAR_Y - oy;
  if (!out) {
    g.fillRoundRect(BAR_X - ox, by, BAR_W, BAR_H, 3, COL_BAROFF);
    g.drawFastVLine(BAR_CX - ox, (BAR_Y - 3) - oy, BAR_H + 6, TFT_WHITE);   // feiner 0-V-Strich
    return;
  }
  g.fillRoundRect(BAR_X - ox, by, BAR_W, BAR_H, 3, COL_BARBG);
  if (absDev < DEV_MARK_MIN) {
    g.fillRoundRect(BAR_CX - ox - 3, (BAR_Y - 3) - oy, 6, BAR_H + 6, 3, COL_GREEN);   // Treffer: grüner Strich (0 V)
  } else {
    int fillX = (markerX < BAR_CX) ? markerX : BAR_CX;
    g.fillRect(fillX - ox, by, abs(markerX - BAR_CX), BAR_H, fillCol);
    g.drawFastVLine(BAR_CX - ox, (BAR_Y - 3) - oy, BAR_H + 6, TFT_WHITE);   // feiner 0-V-Strich
  }
}

static void drawBarA(bool out, int markerX, uint16_t markerCol) {
  ensureSprites();
  if (spritesState == 1) {
    sprBar.fillSprite(TFT_BLACK);
    renderBarA(sprBar, BAR_SPR_X, BAR_SPR_Y, out, markerX, markerCol);
    sprBar.pushSprite(BAR_SPR_X, BAR_SPR_Y);
  } else {
    tft.fillRect(14, BAR_Y - 16, 219, 25, TFT_BLACK);
    renderBarA(tft, 0, 0, out, markerX, markerCol);
  }
}

static void drawBarB(bool out, int markerX, uint16_t fillCol, float absDev) {
  ensureSprites();
  if (spritesState == 1) {
    sprBar.fillSprite(TFT_BLACK);
    renderBarB(sprBar, BAR_SPR_X, BAR_SPR_Y, out, markerX, fillCol, absDev);
    sprBar.pushSprite(BAR_SPR_X, BAR_SPR_Y);
  } else {
    tft.fillRect(14, BAR_Y - 16, 219, 25, TFT_BLACK);
    renderBarB(tft, 0, 0, out, markerX, fillCol, absDev);
  }
}

/**
 * @brief Aktualisiert die Werte auf dem Display im normalen Betriebsmodus.
 * Nur geänderte Zonen werden neu gezeichnet (Dirty-Check über actDispValues).
 */
void updateDisplay() {
  tftStartWrite();

  // --- Kopfzeile ---
  uint8_t wifiIcon = currentWifiIcon();
  if (wifiIcon != actDispValues.wifiIcon) {
    actDispValues.wifiIcon = wifiIcon;
    drawWifi(wifiIcon);
  }
  if (wiperTemp != actDispValues.temp) {
    actDispValues.temp = wiperTemp;
    drawTemp(wiperTemp, tempSensorAvailable);
  }

  // --- Zustände einlesen ---
  bool out   = A_onoff->getState();
  bool limit = A_limit->getState();
  bool reg   = A_reg->getState();
  int  volt  = (int)lroundf(received_rms_value);
  uint8_t vcol = !out ? VCOL_GREY : (limit ? VCOL_YELLOW : VCOL_RED);
  bool danger = !limit;                          // Strombegrenzung aus -> Warnung (unabhängig vom Ausgang)
  bool blinkOn = ((millis() / 1000) % 2) == 0;   // hartes Blinken: 1 s an / 1 s aus

  // Preset gilt nur bei explizitem Aufruf als aktiv (LED an), nicht bei zufälliger Wertgleichheit.
  int pv[3] = { A_p1->getValuePreset(), A_p2->getValuePreset(), A_p3->getValuePreset() };
  int8_t active = -1;
  if      (A_p1->getState()) active = 0;
  else if (A_p2->getState()) active = 1;
  else if (A_p3->getState()) active = 2;
  bool held = (active >= 0) && reg;

  // --- Grosse Spannung (Wert- oder Farbwechsel); nur die Mittelzone leeren, Warnspalte links bleibt ---
  if (volt != actDispValues.nVolt || vcol != actDispValues.nVoltCol) {
    actDispValues.nVolt = volt;
    actDispValues.nVoltCol = vcol;
    uint16_t vc = (vcol == VCOL_GREY) ? COL_VGREY : (vcol == VCOL_YELLOW ? TFT_YELLOW : TFT_RED);
    drawIstValue(volt, vc);
  }

  // --- Achtung-Dreieck (Strombegrenzung aus, unabhängig vom Ausgang): immer sichtbar,
  //     dim-grau wenn inaktiv, hart blinkend gelb<->grau wenn aktiv ---
  uint16_t triCol = !danger ? COL_CHIPOFF : (blinkOn ? TFT_YELLOW : COL_CHIPOFF);
  if (triCol != actDispValues.nWarnCol) {
    actDispValues.nWarnCol = triCol;
    // Generiertes Icon (runde Ecken): 52x48 px, deckt sein Feld selbst mit Schwarz -> kein Clear nötig.
    // Zentriert auf die Ist-Spannung (Icon-Mitte x47/y70). Gelb wenn aktiv+Blink, sonst dim-grau.
    // setSwapBytes(true): die Bitmap liegt als Standard-RGB565 vor (sonst kippt die Farbe -> Violett/Weiss).
    bool sw = tft.getSwapBytes();
    tft.setSwapBytes(true);
    tft.pushImage(21, 46, WARN_ICON_W, WARN_ICON_H,
                  (triCol == TFT_YELLOW) ? warnTriYellow : warnTriGrey);
    tft.setSwapBytes(sw);
  }

  // --- Regelabweichungs-Balken (ersetzt die Zielspannungs-Zeile) ---
  float devClamped = constrain(received_rms_value - setpoint_voltage, -DEV_ZONE_MAX, DEV_ZONE_MAX);
  float absDev = fabsf(devClamped);
  int   markerX = BAR_CX + (int)lroundf(devClamped * BAR_PXV);
  uint16_t zoneCol = (absDev <= DEV_ZONE_GREEN) ? COL_GREEN
                   : (absDev <= DEV_ZONE_YELLOW ? TFT_YELLOW : TFT_RED);
  if (displayVariant != actDispValues.nVariant || markerX != actDispValues.nMarkerX ||
      zoneCol != actDispValues.nBarCol || (uint8_t)out != actDispValues.nBarOut) {
    actDispValues.nVariant = displayVariant;
    actDispValues.nMarkerX = markerX;
    actDispValues.nBarCol  = zoneCol;
    actDispValues.nBarOut  = out;
    if (displayVariant == 0) drawBarA(out, markerX, zoneCol);
    else                     drawBarB(out, markerX, zoneCol, absDev);
  }

  // --- 3 Schalter-Chips (spaltengleich über den Presets) ---
  const int CY = 180, CH = 54, CW = 68;
  const int cx3[3] = { 12, 90, 168 };
  if ((uint8_t)out != actDispValues.nOut)    { actDispValues.nOut = out;     drawToggleChip(cx3[0], CY, CW, CH, 0, out,   TFT_RED); }
  if ((uint8_t)limit != actDispValues.nLimit){ actDispValues.nLimit = limit; drawToggleChip(cx3[1], CY, CW, CH, 1, limit, TFT_YELLOW); }
  if ((uint8_t)reg != actDispValues.nReg)    { actDispValues.nReg = reg;     drawToggleChip(cx3[2], CY, CW, CH, 2, reg,   COL_BLUE); }

  // --- Presets (3 Kacheln, gleiche Spalten wie die Chips, bis fast nach unten) ---
  const int PY = 252, PH = 58, PW = 68;
  const int px[3] = { 12, 90, 168 };
  for (int i = 0; i < 3; i++) {
    bool act = (active == i);
    bool wasAct = (actDispValues.nActive == i);
    bool valChanged = (pv[i] != actDispValues.nPreset[i]);
    bool heldChanged = act && ((uint8_t)held != actDispValues.nHeld);
    if (valChanged || act != wasAct || heldChanged) {
      actDispValues.nPreset[i] = pv[i];
      drawPreset(px[i], PY, PW, PH, i + 1, pv[i], act, act && held);
    }
  }
  actDispValues.nActive = active;
  actDispValues.nHeld = (uint8_t)held;

  tftEndWrite();
}

/**
 * @brief Aktualisiert die Werte auf dem Display im Einstellungsmodus.
 */
void updateSettingsDisplay() {
  tftStartWrite();
  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(100);

  if (stepper.currentPosition() != actDispValues.stepperPos) {
      actDispValues.stepperPos = stepper.currentPosition();
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString((String)actDispValues.stepperPos, 220, 40, 4);
  }
  if (received_rms_value != actDispValues.voltage) {
      actDispValues.voltage = received_rms_value;
      String voltageString = String(actDispValues.voltage, 0);
      voltageString += "V";
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(voltageString, 220, 70, 4);
  }
  if (A_p1->getValuePreset() != actDispValues.preset1) {
      actDispValues.preset1 = A_p1->getValuePreset();
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString((String)actDispValues.preset1, 220, 100, 4);
  }
  if (minWiperPos != actDispValues.setup1) {
      actDispValues.setup1 = minWiperPos;
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString((String)actDispValues.setup1, 220, 130, 4);
  }
  if (A_p2->getValuePreset() != actDispValues.preset2) {
      actDispValues.preset2 = A_p2->getValuePreset();
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString((String)actDispValues.preset2, 220, 160, 4);
  }
  if (maxWiperPos != actDispValues.setup2) {
      actDispValues.setup2 = maxWiperPos;
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString((String)actDispValues.setup2, 220, 190, 4);
  }
  if (settingsWarningDirty) { // Warnzeile der Kalibrier-Plausibilität (GitHub-#3)
      settingsWarningDirty = false;
      tft.setTextDatum(TL_DATUM);
      tft.setTextPadding(220);
      tft.setTextColor(TFT_ORANGE, TFT_BLACK);
      tft.drawString(settingsWarning, 10, 225, 2);
  }
  tftEndWrite();
}

/**
 * @brief Schaltet die Hintergrundbeleuchtung des Displays ein oder aus.
 */
void setScreenBacklight(boolean on) {
  digitalWrite(PIN_DISP_BL, on ? HIGH : LOW);
}

/**
 * @brief Löscht den gesamten Bildschirm und füllt ihn mit Schwarz.
 */
void clearScreen() {
  tftStartWrite();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.fillScreen(TFT_BLACK);
  tftEndWrite();
}

/**
 * @brief Zeichnet den "Homing..."-Bildschirm.
 */
void drawHomingScreen() {
  tftStartWrite();
  tft.fillScreen(TFT_BLACK);   // eigener sauberer Hintergrund (nicht der Normalbetrieb-Screen)
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(CC_DATUM);
  tft.drawString("Homing...", 120, 140 ,4);
  if (WiFi.status() == WL_CONNECTED) {
    tft.drawString(WiFi.localIP().toString().c_str(), 120, 180, 2);
  }
  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(FW, 230, 310, 2);
  tftEndWrite();
}

/**
 * @brief Zeichnet den statischen Hintergrund der Hauptanzeige (Redesign).
 * Schwarze Kopfzeile mit Gold-Trennlinie, statische Beschriftung "IST-SPANNUNG".
 * WLAN/Temperatur/Werte kommen dynamisch aus updateDisplay().
 */
void drawBackground() {
  tftStartWrite();
  tft.fillScreen(TFT_BLACK);
  // Rahmen um Spannungs-/Warnbereich; links/rechts an den äusseren Chips ausgerichtet (x=12..236),
  // Radius wie bei den Chips. Keine "Ist"/"Ziel"-Labels mehr.
  thickRoundRect(12, 36, 224, 126, 9, 2, COL_FRAME);   // 10 px höher: 12 px Luft unter dem Temp-Icon
  // Statische Zonen-Skala unter dem Regelabweichungs-Balken (-4V/-2V/+2V/+4V).
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_LABEL, TFT_BLACK);
  tft.setTextPadding(0);
  tft.drawString("-4V",  41, 147, 2);
  tft.drawString("-2V",  82, 147, 2);
  tft.drawString("+2V", 166, 147, 2);
  tft.drawString("+4V", 207, 147, 2);
  tftEndWrite();
}

/**
 * @brief Zeichnet die statischen Beschriftungen der Hauptanzeige (Redesign).
 */
void drawLegend() {
  // Keine statische Beschriftung mehr — kein "Presets"-Text (Kacheln sind selbsterklärend).
  // (Bleibt als leerer Aufruf im displayUpdateTask erhalten.)
}

/**
 * @brief Zeichnet den Fehler-Bildschirm (z.B. bei fehlendem MCP).
 */
void drawErrorScreen() {
  tftStartWrite();
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextDatum(CC_DATUM);
  tft.drawString("ERROR init MCP", 120, 160, 4);
  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(FW, 230, 310, 2);
  tftEndWrite();
}

// Cache für den Voltmeter-Update-Screen (#32): nur Änderungen neu zeichnen.
static int  vmUpdScreenLastProgress = -1;
static char vmUpdScreenLastMsg[96]  = "";
static VmUpdateState vmUpdScreenLastState = VMU_IDLE;

/**
 * @brief Zeichnet den statischen Teil des Voltmeter-Update-Screens (#32).
 */
void drawVmUpdateScreen() {
  vmUpdScreenLastProgress = -1;
  vmUpdScreenLastMsg[0] = '\0';
  vmUpdScreenLastState = VMU_IDLE;
  tftStartWrite();
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 240, 20, TFT_NAVY);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.drawString("ISOLATION VARIAC", 10, 2, 2);
  tft.setTextDatum(CC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("Voltmeter-Update", 120, 70, 4);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString("Variac gesperrt - Ausgang AUS", 120, 100, 2);
  tft.drawRect(19, 149, 202, 22, TFT_WHITE);
  tftEndWrite();
}

// --- OTA-Screen (GitHub-#26) ------------------------------------------------
static int otaScreenLastProgress = -1;

/**
 * @brief Zeichnet den statischen Teil des OTA-Screens (GitHub-#26).
 */
void drawOtaScreen() {
  otaScreenLastProgress = -1;
  tftStartWrite();
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 240, 20, TFT_NAVY);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.drawString("ISOLATION VARIAC", 10, 2, 2);
  tft.setTextDatum(CC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString(otaIsFilesystem ? "Filesystem-Update" : "Firmware-Update", 120, 70, 4);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString("Variac gesperrt - Ausgang AUS", 120, 100, 2);
  tft.drawRect(19, 149, 202, 22, TFT_WHITE);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.drawString("Geraet nicht ausschalten!", 120, 235, 2);
  tftEndWrite();
}

/**
 * @brief Aktualisiert Fortschrittsbalken und Prozentwert des OTA-Screens (GitHub-#26).
 */
void updateOtaScreen() {
  int prog = otaProgress;
  if (prog < 0) prog = 0;
  if (prog > 100) prog = 100;
  if (prog == otaScreenLastProgress) return;
  otaScreenLastProgress = prog;

  tftStartWrite();
  int w = (198 * prog) / 100;
  tft.fillRect(21, 151, w, 18, TFT_DARKGREEN);
  tft.fillRect(21 + w, 151, 198 - w, 18, TFT_BLACK);
  tft.setTextDatum(CC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(100);
  tft.drawString(String(prog) + " %", 120, 195, 4);
  tft.setTextPadding(0);
  tftEndWrite();
}

/**
 * @brief Aktualisiert Fortschrittsbalken, Prozentwert und Statusmeldung des Update-Screens (#32).
 */
void updateVmUpdateScreen() {
  int prog = vmUpdateProgress;
  if (prog < 0) prog = 0;
  if (prog > 100) prog = 100;

  tftStartWrite();
  if (prog != vmUpdScreenLastProgress) {
    vmUpdScreenLastProgress = prog;
    int w = (198 * prog) / 100;
    tft.fillRect(21, 151, w, 18, TFT_DARKGREEN);
    tft.fillRect(21 + w, 151, 198 - w, 18, TFT_BLACK);
    tft.setTextDatum(CC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextPadding(100);
    tft.drawString(String(prog) + " %", 120, 195, 4);
    tft.setTextPadding(0);
  }
  VmUpdateState st = vmUpdateState;
  if (strncmp(vmUpdScreenLastMsg, vmUpdateMessage, sizeof(vmUpdScreenLastMsg)) != 0
      || st != vmUpdScreenLastState) {
    vmUpdScreenLastState = st;
    strncpy(vmUpdScreenLastMsg, vmUpdateMessage, sizeof(vmUpdScreenLastMsg) - 1);
    vmUpdScreenLastMsg[sizeof(vmUpdScreenLastMsg) - 1] = '\0';
    tft.setTextDatum(CC_DATUM);
    if (st == VMU_SUCCESS)    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    else if (st == VMU_ERROR) tft.setTextColor(TFT_RED, TFT_BLACK);
    else                      tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextPadding(238);
    tft.drawString(vmUpdScreenLastMsg, 120, 235, 2);
    tft.setTextPadding(0);
  }
  tftEndWrite();
}

/**
 * @brief Zeichnet den statischen Hintergrund des Einstellungs-Bildschirms.
 */
void drawSettingsScreen() {
  tftStartWrite();
  tft.fillRect(0, 0, 240, 20, TFT_NAVY);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.drawString("** SETUP **", 10, 2, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Enc:", 10, 40, 4);
  tft.drawString("Out:", 10, 70, 4);
  tft.drawString("P1:", 10, 100, 4);
  tft.drawString("P1S:", 10, 130, 4);
  tft.drawString("P2:", 10, 160, 4);
  tft.drawString("P2S:", 10, 190, 4);
  tftEndWrite();
  settingsWarningDirty = true;
}

// ********************************************************************************
// Display-Task
// ********************************************************************************
/**
 * @brief FreeRTOS Task zur periodischen Aktualisierung des TFT-Displays.
 */
void displayUpdateTask(void *parameter) {
  bool vmUpdScreenActive = false;
  bool otaScreenActive = false;
  uint32_t vmUpdResultSince = 0;
  for (;;) {

    if (!hardwareInitialized) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    // GitHub-#26: Während eines OTA-Updates eigener Screen.
    if (otaActive) {
      if (!otaScreenActive) {
        otaScreenActive = true;
        drawOtaScreen();
      }
      updateOtaScreen();
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // #32: Während des Voltmeter-FW-Updates eigener Screen.
    if (vmUpdateState != VMU_IDLE) {
      if (!vmUpdScreenActive) {
        vmUpdScreenActive = true;
        vmUpdResultSince = 0;
        drawVmUpdateScreen();
      }
      updateVmUpdateScreen();
      if (vmUpdateState == VMU_RUNNING) {
        vmUpdResultSince = 0;
      } else {
        if (vmUpdResultSince == 0) {
          vmUpdResultSince = millis();
        } else if (millis() - vmUpdResultSince >= VM_UPDATE_RESULT_MS) {
          vmUpdateState = VMU_IDLE;
        }
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (vmUpdScreenActive || otaScreenActive) {
      // Rückkehr in den Normalbetrieb: Screen komplett neu aufbauen.
      vmUpdScreenActive = false;
      otaScreenActive = false;
      initDisplayStruct();
      if (currentMode == MODE_NORMAL) {
        drawBackground();
        drawLegend();
      } else {
        clearScreen();
        drawSettingsScreen();
      }
    }

    // GitHub-#3/#18: Während der Homing-Referenzfahrt steht "Homing..." — nicht übermalen.
    if (isHomingActive() || homingScreenActive) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (currentMode == MODE_NORMAL) {
        updateDisplay();
    } else {
        updateSettingsDisplay();
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // Display 10x pro Sekunde aktualisieren
  }
}

/**
 * @brief Setzt die interne Struktur für die Display-Werte zurück.
 * Erzwingt ein Neuzeichnen aller Elemente beim nächsten Update.
 */
void initDisplayStruct() {
    actDispValues.temp = -1;
    actDispValues.wifiIcon = WIFI_ICON_UNSET;
    actDispValues.stepperPos = -1;
    actDispValues.voltage = -1;
    actDispValues.preset1 = -1;
    actDispValues.preset2 = -1;
    actDispValues.preset3 = -1;
    actDispValues.setup1 = -1;
    actDispValues.setup2 = -1;
    actDispValues.nVolt = -1;
    actDispValues.nVoltCol = 255;
    actDispValues.nWarnCol = 0;         // 0 = noch nie gezeichnet -> erste echte Farbe erzwingt Zeichnen
    actDispValues.nOut = 255;
    actDispValues.nLimit = 255;
    actDispValues.nReg = 255;
    actDispValues.nPreset[0] = -1;
    actDispValues.nPreset[1] = -1;
    actDispValues.nPreset[2] = -1;
    actDispValues.nActive = -2;
    actDispValues.nHeld = 255;
    actDispValues.nMarkerX = -9999;
    actDispValues.nBarCol = 0;
    actDispValues.nBarOut = 255;
    actDispValues.nVariant = 255;
}
