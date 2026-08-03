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
// FreeFonts (FreeSansBold12pt7b/24pt7b) sind bei aktivem LOAD_GFXFF bereits von TFT_eSPI
// eingebunden — nicht erneut includen (sonst Doppeldefinition). Direkt via &FreeSansBold… nutzen.

TFT_eSPI tft = TFT_eSPI();
SemaphoreHandle_t tftMutex;  // Mutex zum Schutz des TFT-Displays

// ============================================================================
//  Normalbetrieb-Screen (Redesign, Basis Mockup C) – 240x320 Portrait
//  Kopfzeile (schwarz) mit WLAN + Temperatur, grosse farbcodierte Ist-Spannung,
//  Ziel darunter, 5 Status-Chips (Icon + EIN/AUS bzw. Warn-Symbol), 3 Presets.
//  Icons als Zeichenprimitive; Dirty-Check pro Zone (flimmerfrei ohne Framebuffer).
// ============================================================================
#define HDR_H 40                      // Höhe der schwarzen Kopfzeile

// Farben (RGB565)
static const uint16_t COL_BLUE    = 0x1A9B;      // dunkleres Blau (mehr Kontrast zur weissen Preset-Nummer)
static const uint16_t COL_CHIPOFF = 0x39E7;      // inaktiver Chip-Rand / inaktives Icon (dezent grau)
static const uint16_t COL_LABEL   = TFT_DARKGREY;// graue Beschriftungen
static const uint16_t COL_VGREY   = TFT_LIGHTGREY;// Spannung bei Ausgang AUS
static const uint16_t COL_FRAME   = 0x52AA;      // grauer Rundrahmen um den Spannungs-/Warnbereich
static const uint16_t COL_ZIEL    = 0x9492;      // mittleres Grau für die Zielspannung

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
  int     nVolt;      // gerundete Ist-Spannung
  uint8_t nVoltCol;   // VoltColor
  int     nTarget;    // Ziel (V)
  uint8_t nOut, nLimit, nReg;   // Chip-Zustände (3 bedienbare Chips)
  uint8_t nWarnD, nWarnH;       // Warnsymbole (0=inaktiv, 1=hell, 2=dim) für den Blink-Puls
  int     nPreset[3]; // Preset-Spannungen
  int8_t  nActive;    // aktives Preset (0..2) oder -1
  uint8_t nHeld;      // Regelung hält den Presetwert (Schloss)
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

// Gefülltes Dreieck mit abgerundeten Ecken (Radius r) — Bogen tangential an beide Kanten.
// Hexagon der Tangentenpunkte füllen + Kreis je Ecke.
static void fillRoundTri(int X0, int Y0, int X1, int Y1, int X2, int Y2, float r, uint16_t col) {
  float vx[3] = { (float)X0, (float)X1, (float)X2 };
  float vy[3] = { (float)Y0, (float)Y1, (float)Y2 };
  float tx[6], ty[6], ox[3], oy[3];
  for (int i = 0; i < 3; i++) {
    int p = (i + 2) % 3, n = (i + 1) % 3;
    float d1x = vx[p] - vx[i], d1y = vy[p] - vy[i];
    float d2x = vx[n] - vx[i], d2y = vy[n] - vy[i];
    float l1 = sqrtf(d1x*d1x + d1y*d1y), l2 = sqrtf(d2x*d2x + d2y*d2y);
    d1x/=l1; d1y/=l1; d2x/=l2; d2y/=l2;
    float half = acosf(d1x*d2x + d1y*d2y) / 2.0f;
    float t = r / tanf(half), ins = r / sinf(half);
    float bx = d1x + d2x, by = d1y + d2y, bl = sqrtf(bx*bx + by*by);
    bx/=bl; by/=bl;
    tx[2*i]   = vx[i] + d1x*t; ty[2*i]   = vy[i] + d1y*t;
    tx[2*i+1] = vx[i] + d2x*t; ty[2*i+1] = vy[i] + d2y*t;
    ox[i] = vx[i] + bx*ins; oy[i] = vy[i] + by*ins;
  }
  for (int i = 1; i < 5; i++)
    tft.fillTriangle((int)tx[0], (int)ty[0], (int)tx[i], (int)ty[i], (int)tx[i+1], (int)ty[i+1], col);
  for (int i = 0; i < 3; i++)
    tft.fillCircle((int)(ox[i] + 0.5f), (int)(oy[i] + 0.5f), (int)(r + 0.5f), col);
}

static void icoWarnTri(int cx, int cy, int s, uint16_t fill, bool bolt) {  // Warndreieck, Ecken leicht abgerundet
  int by = (int)(s * 0.85f);
  fillRoundTri(cx, cy - s, cx - s, cy + by, cx + s, cy + by, 5.0f, fill);
  if (bolt) {
    tft.fillTriangle(cx + 3, cy - (int)(s * 0.55f), cx - 5, cy + (int)(s * 0.12f), cx + 1, cy + (int)(s * 0.12f), TFT_BLACK);
    tft.fillTriangle(cx - 3, cy + (int)(s * 0.62f), cx + 5, cy - (int)(s * 0.12f), cx - 1, cy - (int)(s * 0.12f), TFT_BLACK);
  } else {
    // fettes Ausrufezeichen in Schwarz (auf gelbem Dreieck)
    tft.fillRect(cx - 3, cy - (int)(s * 0.42f), 6, (int)(s * 0.5f), TFT_BLACK);
    tft.fillCircle(cx, cy + (int)(s * 0.5f), 3, TFT_BLACK);
  }
}

// Dickerer Rahmen: t konzentrische Rechtecke.
static void thickRoundRect(int x, int y, int w, int h, int r, int t, uint16_t c) {
  for (int i = 0; i < t; i++) tft.drawRoundRect(x + i, y + i, w - 2 * i, h - 2 * i, (r - i > 1) ? r - i : 1, c);
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
  tft.fillRect(3, 4, 32, 26, TFT_BLACK);
  const int cx = 17, by = 25;
  if (st == WIFI_ICON_CONNECTED || st == WIFI_ICON_AP) {
    tft.fillCircle(cx, by, 2, TFT_WHITE);
    tft.drawSmoothArc(cx, by, 8,  6,  138, 222, TFT_WHITE, TFT_BLACK);
    tft.drawSmoothArc(cx, by, 14, 12, 138, 222, TFT_WHITE, TFT_BLACK);
    if (st == WIFI_ICON_AP) tft.fillCircle(cx + 11, by - 3, 3, TFT_WHITE);   // Such-Indikator (Config-AP)
  }
  // WIFI_ICON_NONE: leer
}

static void drawTemp(float t, bool ok) {
  tft.fillRect(146, 2, 94, 40, TFT_BLACK);
  // Thermometer: Röhre umrandet, unten Quecksilbersäule + Kolben gefüllt
  int cx = 162, top = 5, tubeW = 8, tubeH = 20, bulbCy = 30, bulbR = 6;
  tft.drawRoundRect(cx - tubeW / 2, top, tubeW, tubeH, tubeW / 2, TFT_WHITE);   // Röhren-Umriss
  tft.fillCircle(cx, bulbCy, bulbR, TFT_WHITE);                                  // Kolben gefüllt
  int mercTop = top + tubeH / 2;                                                 // ab Mitte gefüllt
  tft.fillRect(cx - (tubeW / 2 - 2), mercTop, tubeW - 4, bulbCy - mercTop + 1, TFT_WHITE);
  // Wert grösser (Font 4)
  tft.setTextColor(TFT_WHITE);
  tft.setTextPadding(0);
  tft.setTextDatum(MR_DATUM);
  String s = ok ? String((int)lroundf(t)) : String("N/A");
  tft.drawString(s, 210, 20, 4);
  if (ok) {
    tft.drawCircle(215, 11, 3, TFT_WHITE);        // Grad-Zeichen (grösser)
    tft.setTextDatum(ML_DATUM);
    tft.drawString("C", 220, 20, 4);
  }
}

// ------------------------------------------------------------------ Spannung / Ziel
// Double-Buffering gegen Flackern: die grossen Zahlen werden in einen Sprite gezeichnet und
// in einem Rutsch gepusht (statt löschen -> neu beschriften). Sprites werden einmal angelegt.
static TFT_eSprite sprIst  = TFT_eSprite(&tft);
static TFT_eSprite sprZiel = TFT_eSprite(&tft);
static int spritesState = 0;   // 0 = uninitialisiert, 1 = ok, 2 = Anlegen fehlgeschlagen

static void ensureSprites() {
  if (spritesState != 0) return;
  sprIst.setColorDepth(16);
  sprZiel.setColorDepth(16);
  bool ok = (sprIst.createSprite(136, 44) != nullptr) && (sprZiel.createSprite(136, 36) != nullptr);
  spritesState = ok ? 1 : 2;
}

// Ist-Spannung: grosser, fetter, farbcodierter Wert, rechtsbündig im Rahmen.
static void drawIstValue(int v, uint16_t col) {
  ensureSprites();
  if (spritesState == 1) {
    sprIst.fillSprite(TFT_BLACK);
    sprIst.setFreeFont(&FreeSansBold24pt7b);
    sprIst.setTextDatum(MR_DATUM);
    sprIst.setTextColor(col, TFT_BLACK);
    sprIst.drawString(String(v) + " V", 132, 22);
    sprIst.pushSprite(96, 52);
  } else {
    tft.fillRect(96, 52, 136, 44, TFT_BLACK);
    tft.setFreeFont(&FreeSansBold24pt7b);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(col);
    tft.setTextPadding(0);
    tft.drawString(String(v) + " V", 228, 74);
    tft.setTextFont(2);
  }
}

// Zielspannung: kleiner, grau und NICHT fett, rechtsbündig darunter.
static void drawZielValue(int v) {
  ensureSprites();
  if (spritesState == 1) {
    sprZiel.fillSprite(TFT_BLACK);
    sprZiel.setFreeFont(&FreeSans18pt7b);
    sprZiel.setTextDatum(MR_DATUM);
    sprZiel.setTextColor(COL_ZIEL, TFT_BLACK);
    sprZiel.drawString(String(v) + " V", 132, 18);
    sprZiel.pushSprite(96, 100);
  } else {
    tft.fillRect(96, 100, 136, 36, TFT_BLACK);
    tft.setFreeFont(&FreeSans18pt7b);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(COL_ZIEL);
    tft.setTextPadding(0);
    tft.drawString(String(v) + " V", 228, 118);
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

static uint16_t dim565(uint16_t c) {
  uint16_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  return ((r >> 1) << 11) | ((g >> 1) << 5) | (b >> 1);   // halbe Helligkeit -> dim-Farbe
}

// Rahmenloses Warnsymbol links an der Spannung: aktiv pulsierend Farbe<->dim-Farbe, inaktiv dim-grau.
static void drawWarn(int cx, int cy, int s, bool bolt, bool active, bool blinkOn) {
  int topY = cy - s - 1, botY = cy + (int)(s * 0.85f) + 2;   // enge Box, damit die beiden Dreiecke sich nicht gegenseitig löschen
  tft.fillRect(cx - s - 1, topY, 2 * s + 2, botY - topY, TFT_BLACK);
  uint16_t base = bolt ? TFT_YELLOW : TFT_RED;
  uint16_t col = !active ? COL_CHIPOFF : (blinkOn ? base : dim565(base));
  icoWarnTri(cx, cy, s, col, bolt);
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
  int  target = (int)lroundf(setpoint_voltage);
  uint8_t vcol = !out ? VCOL_GREY : (limit ? VCOL_YELLOW : VCOL_RED);
  bool danger = !limit;                          // Strombegrenzung aus -> Warnung (unabhängig vom Ausgang)
  bool blinkOn = ((millis() / 1000) % 2) == 0;   // hartes Blinken: 1 s an / 1 s aus

  // Preset ist aktiv, wenn der Zielwert einem Preset entspricht — auch bei Ausgang aus.
  int pv[3] = { A_p1->getValuePreset(), A_p2->getValuePreset(), A_p3->getValuePreset() };
  int8_t active = -1;
  for (int i = 0; i < 3; i++) {
    if (abs(target - pv[i]) <= 2) { active = (int8_t)i; break; }
  }
  bool held = (active >= 0) && reg;

  // --- Grosse Spannung (Wert- oder Farbwechsel); nur die Mittelzone leeren, Warnspalte links bleibt ---
  if (volt != actDispValues.nVolt || vcol != actDispValues.nVoltCol) {
    actDispValues.nVolt = volt;
    actDispValues.nVoltCol = vcol;
    uint16_t vc = (vcol == VCOL_GREY) ? COL_VGREY : (vcol == VCOL_YELLOW ? TFT_YELLOW : TFT_RED);
    drawIstValue(volt, vc);
  }

  // --- Zielwert (grau, gleich gross, rechtsbündig darunter) ---
  if (target != actDispValues.nTarget) {
    actDispValues.nTarget = target;
    drawZielValue(target);
  }

  // --- Achtung-Dreieck (Strombegrenzung aus): nur wenn aktiv, gross, hartes Blinken ---
  uint8_t triShown = (danger && blinkOn) ? 1 : 0;
  if (triShown != actDispValues.nWarnD) {
    actDispValues.nWarnD = triShown;
    tft.fillRect(25, 63, 70, 72, TFT_BLACK);   // Dreieck-Feld im Rahmen leeren (ohne Rahmenrand)
    if (triShown) icoWarnTri(60, 100, 34, TFT_YELLOW, false);   // gelbes Warndreieck, schwarzes "!"
  }

  // --- 3 Schalter-Chips (spaltengleich über den Presets) ---
  const int CY = 170, CH = 54, CW = 68;
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
  thickRoundRect(12, 46, 224, 106, 9, 2, COL_FRAME);
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
    actDispValues.nTarget = -1;
    actDispValues.nOut = 255;
    actDispValues.nLimit = 255;
    actDispValues.nReg = 255;
    actDispValues.nWarnD = 255;
    actDispValues.nWarnH = 255;
    actDispValues.nPreset[0] = -1;
    actDispValues.nPreset[1] = -1;
    actDispValues.nPreset[2] = -1;
    actDispValues.nActive = -2;
    actDispValues.nHeld = 255;
}
