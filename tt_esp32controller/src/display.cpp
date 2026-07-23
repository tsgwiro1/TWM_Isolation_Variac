// TWM Isolation Variac – TFT-Display: Screens und Update-Task (#10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#include "display.h"
#include <WiFi.h>     // drawHomingScreen zeigt die IP
#include "pins.h"
#include "state.h"
#include "logging.h"
#include "comm.h"     // received_rms_value, vmUpdate*-Status (#32)
#include "motor.h"    // stepper.currentPosition()
#include "actions.h"  // A_* Zustände/Presets für die Anzeige
#include "system.h"   // wiperTemp
#include "icons.h"    // XBM-Symbole für die Kopfzeile (Paket L)

TFT_eSPI tft = TFT_eSPI();
SemaphoreHandle_t tftMutex;  // Mutex zum Schutz des TFT-Displays

// --- Kopfzeilen-Layout (Paket L, #36/#37) ---------------------------------
// [wifi]            ISOLATION VARIAC            [thermo] 34 °C
#define HDR_H          20    // Höhe des Navy-Balkens
#define ICON_Y          2    // y-Position beider Icons (16 px hoch, mittig in 20 px)
#define WIFI_ICON_X     2    // WLAN-Icon linksbündig
#define TEMP_RIGHT    230    // Temperaturgruppe endet hier (rechtsbündig)
// Ab hier darf für die Temperatur freigeräumt werden, ohne den Titel anzuschneiden:
// "ISOLATION VARIAC" ist in Font 2 113 px breit, zentriert auf x=120 reicht es bis 176.
// Die verbleibenden 52 px (178..230) fassen Icon + zwei Ziffern + "°C" (51 px). Passt
// eine Gruppe nicht hinein (dreistellig oder "N/A"), entfällt das Icon — siehe
// drawTempGroup(). So bleibt der Titel unter allen Umständen unversehrt.
#define TEMP_ZONE_X   178

// Welches WLAN-Symbol gerade gilt (#36)
enum WifiIcon : uint8_t { WIFI_ICON_NONE, WIFI_ICON_CONNECTED, WIFI_ICON_AP, WIFI_ICON_UNSET = 255 };

struct displayValues {
	float temp;
	uint8_t wifiIcon;
	int stepperPos;
	uint8_t encoder10x;
	uint8_t outputOn;
	uint8_t limitOn;
	int preset1;
	int preset2;
	int preset3;
  int setup1;
  int setup2;
  float voltage;
  float target_voltage;
};

static displayValues actDispValues;

// Warnzeile im Setup-Screen (GitHub-#3): Plausibilitätswarnungen der Kalibrierung.
static char settingsWarning[40] = "";
static volatile bool settingsWarningDirty = false;

/**
 * @brief Setzt (oder löscht, bei "") die Warnzeile im Setup-Screen (GitHub-#3).
 * Gezeichnet wird sie asynchron vom displayUpdateTask in updateSettingsDisplay().
 * @param msg Warntext (ASCII, max. 39 Zeichen) oder "" zum Löschen.
 */
void setSettingsWarning(const char* msg) {
    strncpy(settingsWarning, msg ? msg : "", sizeof(settingsWarning) - 1);
    settingsWarning[sizeof(settingsWarning) - 1] = '\0';
    settingsWarningDirty = true;
}

/**
 * @brief Sperrt den TFT-Mutex für exklusiven Display-Zugriff.
 * Muss vor einer Sequenz von Zeichenoperationen aufgerufen werden.
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

/**
 * @brief Zeichnet die Temperaturgruppe rechts in der Kopfzeile (#37).
 * Aufbau rechtsbündig: [Thermometer-Icon] [Wert ohne Nachkommastellen] [°] [C].
 * Das Grad-Zeichen kommt als kleiner Kreis, weil Font 2 nur ASCII 32..127 kennt.
 * Die Zone wird vorher geleert — die Gruppenbreite ändert sich mit der Stellenzahl.
 */
static void drawTempGroup(float temp, bool sensorOk) {
    tft.fillRect(TEMP_ZONE_X, 0, 240 - TEMP_ZONE_X, HDR_H, TFT_NAVY);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setTextPadding(0);
    tft.setTextDatum(TL_DATUM);

    const int degR = 2, degGap = 2, iconGap = 3;
    String val   = sensorOk ? String(temp, 0) : String("N/A");   // ohne Nachkommastellen
    int    wVal  = tft.textWidth(val, 2);
    int    wUnit = sensorOk ? (degGap + 2 * degR + 2 + tft.textWidth("C", 2)) : 0;

    // Icon nur zeichnen, solange die Gruppe in die reservierte Zone passt (siehe
    // TEMP_ZONE_X). Sonst hat der Messwert Vorrang vor dem Symbol.
    int  need     = ICON_W + iconGap + wVal + wUnit;
    bool withIcon = (need <= TEMP_RIGHT - TEMP_ZONE_X);
    if (!withIcon) need -= (ICON_W + iconGap);

    int x = TEMP_RIGHT - need;
    if (withIcon) {
        tft.drawXBitmap(x, ICON_Y, icon_thermostat, ICON_W, ICON_H, TFT_WHITE, TFT_NAVY);
        x += ICON_W + iconGap;
    }
    tft.drawString(val, x, 2, 2);
    if (sensorOk) {
        x += wVal + degGap;
        tft.drawCircle(x + degR, 2 + degR + 2, degR, TFT_WHITE);   // "°" (Font 2 hat kein Grad-Zeichen)
        x += 2 * degR + 2;
        tft.drawString("C", x, 2, 2);
    }
}

/**
 * @brief Zeichnet das WLAN-Symbol links in der Kopfzeile (#36).
 * Verbunden -> "wifi", eigener Config-AP offen -> "wifi_find", sonst kein Symbol.
 */
static void drawWifiIcon(uint8_t state) {
    tft.fillRect(WIFI_ICON_X, ICON_Y, ICON_W, ICON_H, TFT_NAVY);
    if (state == WIFI_ICON_CONNECTED) {
        tft.drawXBitmap(WIFI_ICON_X, ICON_Y, icon_wifi, ICON_W, ICON_H, TFT_WHITE, TFT_NAVY);
    } else if (state == WIFI_ICON_AP) {
        tft.drawXBitmap(WIFI_ICON_X, ICON_Y, icon_wifi_find, ICON_W, ICON_H, TFT_WHITE, TFT_NAVY);
    }
    // WIFI_ICON_NONE: Fläche bleibt leer (Funk aus / keine Verbindung)
}

/**
 * @brief Ermittelt das passende WLAN-Symbol aus dem aktuellen Systemzustand (#36).
 */
static uint8_t currentWifiIcon() {
    if (currentSystemState == STATE_WIFIMANAGER_AP) return WIFI_ICON_AP;
    if (WiFi.status() == WL_CONNECTED)              return WIFI_ICON_CONNECTED;
    return WIFI_ICON_NONE;
}

/**
 * @brief Aktualisiert die Werte auf dem Display im normalen Betriebsmodus.
 */
void updateDisplay() {
  tftStartWrite(); // << SPERREN
  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(100);

  // WLAN-Symbol nur bei Zustandswechsel neu zeichnen (#36)
  uint8_t wifiIcon = currentWifiIcon();
  if (wifiIcon != actDispValues.wifiIcon) {
      actDispValues.wifiIcon = wifiIcon;
      drawWifiIcon(wifiIcon);
      tft.setTextDatum(TR_DATUM);
      tft.setTextPadding(100);
  }

	if (wiperTemp != actDispValues.temp){
		actDispValues.temp = wiperTemp;
        drawTempGroup(actDispValues.temp, tempSensorAvailable);
        tft.setTextDatum(TR_DATUM);
        tft.setTextPadding(100);
	}
  if (received_rms_value != actDispValues.voltage) {
		actDispValues.voltage = received_rms_value;
		String voltageString = String(actDispValues.voltage, 0);
    voltageString += "V";
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
	    tft.drawString(voltageString, 230, 40, 4);
	}
	if (setpoint_voltage != actDispValues.target_voltage) {
		actDispValues.target_voltage = setpoint_voltage;
    String voltageString = String(actDispValues.target_voltage, 0);
    voltageString += "V";
		tft.setTextColor(TFT_WHITE, TFT_BLACK);
	    tft.drawString(voltageString, 230, 70, 4);
	}
	if (A_p1->getValuePreset() != actDispValues.preset1) {
		actDispValues.preset1 = A_p1->getValuePreset();
		tft.setTextColor(TFT_WHITE, TFT_BLACK);
	    tft.drawString((String)actDispValues.preset1 + "V", 230, 220, 4);
	}
	if (A_p2->getValuePreset() != actDispValues.preset2) {
		actDispValues.preset2 = A_p2->getValuePreset();
		tft.setTextColor(TFT_WHITE, TFT_BLACK);
	    tft.drawString((String)actDispValues.preset2 + "V", 230, 250, 4);
	}
	if (A_p3->getValuePreset() != actDispValues.preset3) {
		actDispValues.preset3 = A_p3->getValuePreset();
		tft.setTextColor(TFT_WHITE, TFT_BLACK);
	    tft.drawString((String)actDispValues.preset3 + "V", 230, 280, 4);
	}
	if (A_onoff->getState() != actDispValues.outputOn) {
		actDispValues.outputOn = A_onoff->getState();
		if (actDispValues.outputOn) {
			tft.setTextColor(TFT_ORANGE, TFT_BLACK);
		} 
		else {
			tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
		}
		tft.setTextDatum(TC_DATUM);
		tft.drawString(actDispValues.outputOn ? " Output ON " : "Output OFF", 120, 120, 4);
	}
	if (A_limit->getState() != actDispValues.limitOn) {
		actDispValues.limitOn = A_limit->getState();
        if (actDispValues.limitOn) {
            tft.setTextColor(TFT_GREEN, TFT_BLACK);
        }
        else {
            tft.setTextColor(TFT_RED, TFT_BLACK);
        }
		tft.setTextDatum(TC_DATUM);
	    tft.drawString(actDispValues.limitOn ? "    Current Limited    " : "Danger - No Limits!", 120, 160, 4);
	}
  tftEndWrite();   // << FREIGEBEN
}

/**
 * @brief Aktualisiert die Werte auf dem Display im Einstellungsmodus.
 */
void updateSettingsDisplay() {
  tftStartWrite(); // << SPERREN
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
      tft.setTextPadding(220); // überschreibt auch eine vorherige (längere) Warnung
      tft.setTextColor(TFT_ORANGE, TFT_BLACK);
      tft.drawString(settingsWarning, 10, 225, 2);
  }
  tftEndWrite();   // << FREIGEBEN
}

/**
 * @brief Schaltet die Hintergrundbeleuchtung des Displays ein oder aus.
 * @param on true für an, false für aus.
 */
void setScreenBacklight(boolean on) {
  digitalWrite(PIN_DISP_BL, on ? HIGH : LOW);
}

/**
 * @brief Löscht den gesamten Bildschirm und füllt ihn mit Schwarz.
 */
void clearScreen() {
  tftStartWrite(); // << SPERREN
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.fillScreen(TFT_BLACK);
  tftEndWrite();   // << FREIGEBEN
}

/**
 * @brief Zeichnet den "Homing..."-Bildschirm.
 */
void drawHomingScreen() {
  drawBackground();
  tftStartWrite(); // << SPERREN
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(CC_DATUM);
  tft.drawString("Homing...", 120, 140 ,4);
  // GitHub-#13: Beim Start läuft das Homing vor dem WLAN-Aufbau — dann bleibt die
  // Zeile leer statt "0.0.0.0" zu zeigen. Beim Kalibrier-Einstieg im laufenden
  // Betrieb steht die Verbindung und die IP wird wie gewohnt angezeigt.
  if (WiFi.status() == WL_CONNECTED) {
    tft.drawString(WiFi.localIP().toString().c_str(), 120, 180, 2);
  }

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(FW, 230, 310, 2);
  tftEndWrite();   // << FREIGEBEN
}

/**
 * @brief Zeichnet den statischen Hintergrund der Hauptanzeige.
 */
void drawBackground() {
  tftStartWrite(); // << SPERREN
	tft.fillScreen(TFT_BLACK);
	tft.fillRect(0, 0, 240, 20, TFT_NAVY);
	tft.drawFastHLine(10, 110, 220, TFT_GOLD),
	tft.drawFastHLine(10, 200, 220, TFT_GOLD),
	
	// Paket L: Titel zentriert — links sitzt jetzt das WLAN-Symbol (#36),
	// rechts die Temperaturgruppe (#37).
	tft.setTextDatum(TC_DATUM);
	tft.setTextColor(TFT_WHITE, TFT_NAVY);
	tft.drawString("ISOLATION VARIAC", 120, 2, 2);
  tftEndWrite();   // << FREIGEBEN
}

/**
 * @brief Zeichnet die statischen Beschriftungen der Hauptanzeige.
 */
void drawLegend() {
  tftStartWrite(); // << SPERREN
	// Datum explizit setzen: drawBackground() hinterlässt seit Paket L TC_DATUM
	// (zentrierter Titel) — ohne das hier würden die Beschriftungen zentriert
	// auf x=10 landen und links aus dem Display laufen.
	tft.setTextDatum(TL_DATUM);
	tft.setTextColor(TFT_WHITE, TFT_BLACK);
	tft.drawString("Output:", 10, 40, 4);
	tft.drawString("Target:", 10, 70, 4);
	tft.drawString("P1:", 10, 220, 4);
	tft.drawString("P2:", 10, 250, 4);
	tft.drawString("P3:", 10, 280, 4);
  tftEndWrite();   // << FREIGEBEN
}

/**
 * @brief Zeichnet den Fehler-Bildschirm (z.B. bei fehlendem MCP).
 */
void drawErrorScreen() {
  drawBackground();
  tftStartWrite(); // << SPERREN
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextDatum(CC_DATUM);
  tft.drawString("ERROR init MCP", 120, 160, 4);

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(FW, 230, 310, 2);
  tftEndWrite();   // << FREIGEBEN
}

// Cache für den Voltmeter-Update-Screen (#32): nur Änderungen neu zeichnen.
static int  vmUpdScreenLastProgress = -1;
static char vmUpdScreenLastMsg[96]  = "";
static VmUpdateState vmUpdScreenLastState = VMU_IDLE;

/**
 * @brief Zeichnet den statischen Teil des Voltmeter-Update-Screens (#32).
 * Wird einmal beim Eintritt gezeichnet; Fortschritt/Status via updateVmUpdateScreen().
 */
void drawVmUpdateScreen() {
  vmUpdScreenLastProgress = -1;
  vmUpdScreenLastMsg[0] = '\0';
  vmUpdScreenLastState = VMU_IDLE;
  tftStartWrite(); // << SPERREN
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

  tft.drawRect(19, 149, 202, 22, TFT_WHITE); // Rahmen Fortschrittsbalken
  tftEndWrite();   // << FREIGEBEN
}

/**
 * @brief Aktualisiert Fortschrittsbalken, Prozentwert und Statusmeldung des Update-Screens (#32).
 * Statusmeldung bei Erfolg grün, bei Fehler rot.
 */
void updateVmUpdateScreen() {
  int prog = vmUpdateProgress;
  if (prog < 0) prog = 0;
  if (prog > 100) prog = 100;

  tftStartWrite(); // << SPERREN
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
  VmUpdateState st = vmUpdateState; // einmal lesen (Update-Task schreibt parallel)
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
  tftEndWrite();   // << FREIGEBEN
}

/**
 * @brief Zeichnet den statischen Hintergrund des Einstellungs-Bildschirms.
 */
void drawSettingsScreen() {
  tftStartWrite(); // << SPERREN
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
  tftEndWrite();   // << FREIGEBEN
  settingsWarningDirty = true; // Warnzeile nach Komplett-Neuaufbau wieder zeichnen (GitHub-#3)
}

// ********************************************************************************
// Action callback functions
// ********************************************************************************
/**
 * @brief FreeRTOS Task zur periodischen Aktualisierung des TFT-Displays.
 * @param parameter Standard FreeRTOS Task-Parameter (hier ungenutzt).
 */
void displayUpdateTask(void *parameter) {
  bool vmUpdScreenActive = false;   // Voltmeter-Update-Screen ist aktuell gezeichnet (#32)
  uint32_t vmUpdResultSince = 0;    // Zeitpunkt, seit dem das Ergebnis (Erfolg/Fehler) angezeigt wird
  for (;;) {

    if (!hardwareInitialized) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue; // Schleife überspringen, wenn Hardware fehlt
    }

    // #32: Während des Voltmeter-FW-Updates eigener Screen (Fortschritt + Status).
    if (vmUpdateState != VMU_IDLE) {
      if (!vmUpdScreenActive) {
        vmUpdScreenActive = true;
        vmUpdResultSince = 0;
        drawVmUpdateScreen();
      }
      updateVmUpdateScreen();
      if (vmUpdateState == VMU_RUNNING) {
        vmUpdResultSince = 0; // (wieder) am Laufen -> Ergebnis-Timer zurücksetzen
      } else {
        // Erfolg/Fehler: Ergebnis VM_UPDATE_RESULT_MS stehen lassen, dann quittieren.
        if (vmUpdResultSince == 0) {
          vmUpdResultSince = millis();
        } else if (millis() - vmUpdResultSince >= VM_UPDATE_RESULT_MS) {
          vmUpdateState = VMU_IDLE; // -> Rückkehr in den Normalbetrieb (unten)
        }
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (vmUpdScreenActive) {
      // Rückkehr in den Normalbetrieb (Ausgang bleibt AUS): Screen komplett neu aufbauen.
      vmUpdScreenActive = false;
      initDisplayStruct(); // Anzeige-Cache invalidieren -> alle Werte neu zeichnen
      if (currentMode == MODE_NORMAL) {
        drawBackground();
        drawLegend();
      } else { // MODE_SETTINGS
        clearScreen();
        drawSettingsScreen();
      }
    }

    // GitHub-#3: Während der Homing-Referenzfahrt (Kalibrier-Einstieg) zeigt der
    // Screen "Homing..." — den übermalen wir nicht.
    // GitHub-#18: homingScreenActive deckt zusätzlich das Fenster ab, in dem der
    // Modus schon gewechselt hat, homing() aber noch nicht gestartet ist (und die
    // Zeit danach, bis der Settings-Screen fertig aufgebaut ist).
    if (isHomingActive() || homingScreenActive) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (currentMode == MODE_NORMAL) {
        updateDisplay();
    } else { // MODE_SETTINGS
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
    actDispValues.wifiIcon = WIFI_ICON_UNSET;   // erzwingt Neuzeichnen (#36)
    actDispValues.stepperPos = -1;
    actDispValues.encoder10x = -1;
    actDispValues.outputOn = -1;
    actDispValues.limitOn = -1;
    actDispValues.preset1 = -1;
    actDispValues.preset2 = -1;
    actDispValues.preset3 = -1;
    actDispValues.setup1 = -1;
    actDispValues.setup2 = -1;
    actDispValues.voltage = -1;
    actDispValues.target_voltage = -1;
}

