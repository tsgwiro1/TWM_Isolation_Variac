// TWM Isolation Variac – Tasten/LEDs/Relais, Encoder und Benutzereingaben (#10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#include "actions.h"
#include "pins.h"
#include "state.h"
#include "logging.h"
#include "config.h"   // saveConfiguration nach Preset-/Settings-Änderung
#include "motor.h"    // setWiper*, stepper
#include "comm.h"     // isVoltageDataFresh, received_rms_value, vmUpdateState (#32)
#include "display.h"  // Settings-Screen beim Modus-Wechsel

MCP23017 mcp = MCP23017(MCP23017_ADDR);

int lastEncPos = 0;
int encSpeed = ENCLOWSPEED;
ESP32Encoder encoder;

// Actions
Action* A_onoff;
Action* A_limit;
Action* A_reg;
Action* A_p1;
Action* A_p2;
Action* A_p3;
Action* A_x10;

Action* g[3];

/**
 * @brief Deaktiviert alle aktiven Preset-Tasten (schaltet ihre LEDs aus).
 * Wird aufgerufen, wenn eine manuelle Änderung (Encoder, Web-Interface) erfolgt.
 */
void resetPresetActions() {
    if (A_p1->getState()) A_p1->off();
    if (A_p2->getState()) A_p2->off();
    if (A_p3->getState()) A_p3->off();
}

/**
 * @brief Ruft die handle()-Methode für alle globalen Action-Objekte auf.
 * Dies prüft den Zustand aller angeschlossenen Tasten.
 */
void handleAllActions() {
    A_onoff->handle();
    A_limit->handle();
    A_reg->handle();
    A_p1->handle();
    A_p2->handle();
    A_p3->handle();
    A_x10->handle();
}

/**
 * @brief Liest den aktuellen Zählerstand des Hardware-Encoders.
 * @return int Der rohe Zählerstand.
 */
int getEncoderCount() {
    return encoder.getCount();
}

/**
 * @brief Callback für einfache Relais-Aktionen (z.B. ON/OFF, LIMIT).
 * Schaltet den Zustand der Action bei einem Tastendruck um.
 * @param act Zeiger auf das auslösende Action-Objekt.
 * @param event Der Typ des Tasten-Events.
 */
void cb_RelaisAction(Action* act, ButtonEvent event) {
    if (event == ButtonEvent::PRESSED) {
        act->toggle();
        if (act == A_onoff) logMessage(LOG_WARN, "HARDWARE: Output Relay -> %s", act->getState() ? "ON" : "OFF");
        if (act == A_limit) logMessage(LOG_WARN, "HARDWARE: Current Limit -> %s", act->getState() ? "ACTIVE" : "INACTIVE");
    }
}

/**
 * @brief Callback für Preset-Aktionen im Normalbetrieb.
 * Bei kurzem Druck: Ruft den Preset-Wert ab.
 * Bei langem Druck: Speichert die aktuelle Spannung als neuen Preset-Wert.
 * @param act Zeiger auf das auslösende Action-Objekt.
 * @param event Der Typ des Tasten-Events.
 */
void cb_ValueAction(Action* act, ButtonEvent event) {
    // Finde heraus, welche Preset-Taste (1, 2 oder 3) diesen Callback ausgelöst hat
    int presetNum = 0;
    if (act == A_p1) presetNum = 1;
    else if (act == A_p2) presetNum = 2;
    else if (act == A_p3) presetNum = 3;

    if (event == ButtonEvent::RELEASED) {
        act->on();
        setpoint_voltage = (float)act->getValuePreset();
        isRecallPreset = true;
        logMessage(LOG_INFO, "BUTTON: Load Preset P%d -> %d V", presetNum, act->getValuePreset());
    }
    else if (event == ButtonEvent::LONGPRESSED) {
        if (!isVoltageDataFresh()) {
            logMessage(LOG_WARN, "BUTTON: Store Preset P%d skipped - no fresh voltmeter data", presetNum);
            return;
        }
        int v = constrain((int)round(received_rms_value), MIN_VOLTAGE_TARGET, maxVoltageTarget());
        act->setValuePreset(v);
        act->ledOn();
        saveConfiguration();
        logMessage(LOG_INFO, "BUTTON: Store Preset P%d -> %d V", presetNum, v);
    }
}

// GitHub-#3: Plausibilitätsschwellen für die Kalibrier-Stützwerte (fest im Code).
// Am Min-Punkt liegen bauartbedingt typisch 3–8 V an, am Max-Punkt 250–270 V.
#define CAL_MIN_WARN_ABOVE_V   10.0f
#define CAL_MAX_WARN_BELOW_V  250.0f
#define CAL_MAX_WARN_ABOVE_V  270.0f

/**
 * @brief Plausibilitätsprüfung des Voltmeters beim Speichern eines Kalibrierpunkts (GitHub-#3).
 * Warnt nur (Live-Log + Warnzeile im Setup-Screen), blockiert das Speichern nicht.
 * @param act A_p1 (Min-Punkt) oder A_p2 (Max-Punkt).
 */
static void checkCalibrationPlausibility(Action* act) {
    char warn[40] = "";
    if (!isVoltageDataFresh()) {
        // Der Spannungs-Stützwert wird nur bei frischem Messwert mitgeführt —
        // ohne frische Daten wird also ein womöglich veralteter Wert gespeichert.
        snprintf(warn, sizeof(warn), "Kein VM-Messwert - Wert veraltet?");
        logMessage(LOG_WARN, "CAL: No fresh voltmeter reading at store - stored voltage may be stale");
    } else {
        float storedV;
        portENTER_CRITICAL(&calibMux);
        storedV = (act == A_p1) ? minVoltageAtMinPos : maxVoltageAtMaxPos;
        portEXIT_CRITICAL(&calibMux);
        if (act == A_p1 && storedV > CAL_MIN_WARN_ABOVE_V) {
            snprintf(warn, sizeof(warn), "Min %.1fV (>10V) - VM pruefen!", storedV);
            logMessage(LOG_WARN, "CAL: Min point stored at %.1f V (expected < %.0f V) - check voltmeter calibration", storedV, CAL_MIN_WARN_ABOVE_V);
        } else if (act == A_p2 && (storedV < CAL_MAX_WARN_BELOW_V || storedV > CAL_MAX_WARN_ABOVE_V)) {
            snprintf(warn, sizeof(warn), "Max %.0fV (250-270V) - VM pruefen!", storedV);
            logMessage(LOG_WARN, "CAL: Max point stored at %.1f V (expected %.0f-%.0f V) - check voltmeter calibration", storedV, CAL_MAX_WARN_BELOW_V, CAL_MAX_WARN_ABOVE_V);
        }
    }
    setSettingsWarning(warn); // "" löscht eine frühere Warnung wieder
}

/**
 * @brief Callback für Preset-Aktionen im Einstellungsmodus.
 * Bei kurzem Druck: Fährt die gespeicherte Stepper-Position an.
 * Bei langem Druck: Speichert die aktuelle Stepper-Position.
 * @param act Zeiger auf das auslösende Action-Objekt.
 * @param event Der Typ des Tasten-Events.
 */
void cb_SettingsValueAction(Action* act, ButtonEvent event) {
if (event == ButtonEvent::RELEASED) {
        act->on();
        // Hole den gespeicherten Wert aus dem Action-Objekt und fahre dorthin.
        // GitHub-#3: nur innerhalb sicherer Bereiche (P1 = Min zwischen
        // MINWIPERLIMIT und 0, P2 = Max oberhalb des Min-Punkts) und außerhalb
        // von 0..2000 gedrosselt (Nähe der mechanischen Anschläge).
        int targetPosition = act->getValuePreset();
        if (act == A_p1)      targetPosition = constrain(targetPosition, MINWIPERLIMIT, 0);
        else if (act == A_p2) targetPosition = constrain(targetPosition, minWiperPos + 1, MAXWIPERLIMIT);
        setCalibrationApproachSpeed(targetPosition);
        setWiperAbsolut(targetPosition);
        logMessage(LOG_INFO,"Moving to preset position: %d", targetPosition);
    }
    else if (event == ButtonEvent::LONGPRESSED) {
        int v = constrain((int)stepper.currentPosition(), MINWIPERLIMIT, MAXWIPERLIMIT);
        act->setValuePreset(v);
        checkCalibrationPlausibility(act); // GitHub-#3: warnt nur, blockiert nicht
        saveConfiguration();
        logMessage(LOG_INFO,"Stored new setting %d to config (NVS) and Action object", v);
    }
}

/**
 * @brief Callback für die x10-Funktion des Encoders.
 * Schaltet die Encoder-Geschwindigkeit zwischen langsam und schnell um.
 * @param act Zeiger auf das auslösende Action-Objekt.
 * @param event Der Typ des Tasten-Events.
 */
void cb_x10Action(Action* act, ButtonEvent event) {
    if (event == ButtonEvent::PRESSED) {
        act->toggle();
        encSpeed == ENCLOWSPEED ? encSpeed = ENCHIGHSPEED : encSpeed = ENCLOWSPEED;
        logMessage(LOG_INFO, "HARDWARE: Encoder Speed -> %s", (encSpeed == ENCHIGHSPEED) ? "x10 (Coarse)" : "x1 (Fine)");
    }
}

/**
 * @brief Callback für die Taste zur Aktivierung/Deaktivierung der Regelung.
 * @param act Zeiger auf das auslösende Action-Objekt.
 * @param event Der Typ des Tasten-Events.
 */
void cb_RegAction(Action* act, ButtonEvent event) {
    if (event == ButtonEvent::PRESSED) {
          act->toggle();
          if (act->getState()) {
              // REG ein: schnelle Anfahrt auf den aktuellen Sollwert, danach Halten
              isRecallPreset = true;
          } else {
              // REG aus: automatische Regelung stoppen (Position halten)
              is_regulation_active = false;
              regPhase = RP_IDLE;
          }
          logMessage(LOG_INFO, "HARDWARE: Voltage Regulation -> %s", act->getState() ? "ON" : "OFF");
    }
}

/**
 * @brief Callback für die ON/OFF-Taste im Einstellungsmodus.
 * Schaltet nur die LED ein — der Ausgang ist im Setup-Modus fest an.
 * (Hieß früher irreführend cb_SettingsHomingAction, ohne je ein Homing
 * auszuführen; das Homing macht seit GitHub-#3 der Modus-Einstieg selbst.)
 * @param act Zeiger auf das auslösende Action-Objekt.
 * @param event Der Typ des Tasten-Events.
 */
void cb_SettingsOnOffAction(Action* act, ButtonEvent event) {
    if (event == ButtonEvent::PRESSED) {
        act->on();
    }
}

// ********************************************************************************
// ISR Functions
// ********************************************************************************
/**
 * @brief FreeRTOS Task zur Verarbeitung aller Benutzereingaben (Encoder, Tasten).
 * @param parameter Standard FreeRTOS Task-Parameter (hier ungenutzt).
 */
void userInputTask(void *parameter) {
  for (;;) { // Endlosschleife für den Task

    if (!hardwareInitialized) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue; // Schleife überspringen, wenn Hardware fehlt
    }

    // #32: Während des Voltmeter-FW-Updates ist der Variac gesperrt (keine Bedienung).
    // Encoder-Position synchron halten, damit nach der Freigabe kein Sprung entsteht.
    if (vmUpdateState != VMU_IDLE) {
      lastEncPos = getEncoderCount();
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // Prüfe, ob ein Modus-Wechsel per API angefordert wurde
    if (requestEnterSettingsMode) {
      requestEnterSettingsMode = false; // Flag sofort zurücksetzen
      logMessage(LOG_INFO,"Executing switch to SETTINGS MODE...");
      // GitHub-#3: Vor der Kalibrierung definierten Zustand herstellen — Regelung
      // aus und Referenzfahrt auf den Endschalter. Ohne Homing würde eine falsche
      // Referenz die Min-/Max-Anfahrten in den mechanischen Anschlag schicken.
      is_regulation_active = false;
      // GitHub-#18: Anzeige sperren, BEVOR der Modus wechselt. Sonst sieht der
      // displayUpdateTask bereits MODE_SETTINGS, während homing() sein eigenes Flag
      // noch nicht gesetzt hat, und malt den Settings-Screen über "Homing...".
      homingScreenActive = true;
      currentMode = MODE_SETTINGS;
      clearScreen();
      drawHomingScreen();
      homing(); // blockiert diesen Task; stepperTask + Display pausieren derweil
      initSettingsActions();
      clearScreen();
      drawSettingsScreen();
      initDisplayStruct(); // Anzeige-Cache invalidieren -> Werte neu zeichnen
      homingScreenActive = false;   // fertig aufgebaut -> Display-Task darf wieder
      cb_SettingsValueAction(A_p1, ButtonEvent::RELEASED); // Min-Punkt anfahren
    }

    if (currentMode == MODE_NORMAL) {
      // 1. Alle Actions behandeln
      handleAllActions();

      // 2. Encoder auslesen
      int newEncPos = getEncoderCount();
      if (lastEncPos != newEncPos) {
        is_regulation_active = false; // Manuelle Steuerung hat Vorrang
        int dPos = newEncPos - lastEncPos;
        setWiperRelativ(dPos * encSpeed);
        resetPresetActions();
        lastEncPos = newEncPos;
        last_encoder_change_time = millis();
      }
    } else { // MODE_SETTINGS
      A_onoff->handle();
      A_p1->handle();
      A_p2->handle();
      A_x10->handle();

      int newEncPos = getEncoderCount();
      if (lastEncPos != newEncPos) {
          int dPos = newEncPos - lastEncPos;
          lastEncPos = newEncPos;

          // Messwert/Frische VOR den kritischen Abschnitten auswerten (#5)
          bool  fresh = isVoltageDataFresh();
          float rms   = received_rms_value;

          int pos = wiperPos + dPos * encSpeed;
          pos = constrain(pos, MINWIPERLIMIT, MAXWIPERLIMIT);

          if (A_p1->getState()) {
              pos = constrain(pos, MINWIPERLIMIT, 0);
              portENTER_CRITICAL(&calibMux);
              minWiperPos = pos;
              if (fresh) minVoltageAtMinPos = rms;
              portEXIT_CRITICAL(&calibMux);
          }
          if (A_p2->getState()) {
              pos = constrain(pos, minWiperPos + 1, MAXWIPERLIMIT);
              portENTER_CRITICAL(&calibMux);
              maxWiperPos = pos;
              if (fresh) maxVoltageAtMaxPos = rms;
              portEXIT_CRITICAL(&calibMux);
          }

          // GitHub-#3: nahe den mechanischen Anschlägen (außerhalb 0..2000) gedrosselt
          setCalibrationApproachSpeed(pos);

          // wiperPos-Update + moveTo atomar (#5); im Settings-Modus ist dieser Task
          // der einzige Beweger, aber run() im Stepper-Task läuft parallel.
          portENTER_CRITICAL(&stepperMux);
          wiperPos = pos;
          stepper.moveTo(wiperPos);
          portEXIT_CRITICAL(&stepperMux);
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(20)); // Kurze Pause, ca. 50 mal pro Sekunde
  }
}

/**
 * @brief Initialisiert die Action-Objekte für den normalen Betriebsmodus.
 * Weist die korrekten Callbacks und Relais-Pins zu.
 */
void initActions() {
    A_onoff->setRelais(PIN_RELAIS_ONOFF);
    A_onoff->setCallBack(cb_RelaisAction);
    A_limit->setRelais(PIN_RELAIS_LIMIT, true);
    A_limit->setCallBack(cb_RelaisAction);
    A_p1->setCallBack(cb_ValueAction);
    A_p1->setGroup(g);
    A_p2->setCallBack(cb_ValueAction);
    A_p2->setGroup(g);
    A_p3->setCallBack(cb_ValueAction);
    A_p3->setGroup(g);
    A_x10->setCallBack(cb_x10Action);
    A_reg->setCallBack(cb_RegAction);

	A_onoff->init(0);
	A_limit->init(1);
	A_reg->init(0);
	A_p1->init(0);
	A_p2->init(0);
	A_p3->init(0);
	A_x10->init(0);
    if (A_x10->getState()) {
        encSpeed = ENCHIGHSPEED;
    }
    else {
        encSpeed = ENCLOWSPEED;
    }
}

/**
 * @brief Initialisiert die Action-Objekte für den Einstellungsmodus.
 * Weist die speziellen Callbacks für die Kalibrierung zu.
 */
void initSettingsActions() {
    A_onoff->setCallBack(cb_SettingsOnOffAction);
    A_p1->setCallBack(cb_SettingsValueAction);
    A_p1->setGroup(g);
    A_p2->setCallBack(cb_SettingsValueAction);
    A_p2->setGroup(g);
    A_x10->setCallBack(cb_x10Action);
    A_onoff->init(1);
    A_p1->init(0);
    A_p2->init(0);
    A_x10->init(0);
    if (A_x10->getState()) {
        encSpeed = ENCHIGHSPEED;
    }
    else {
        encSpeed = ENCLOWSPEED;
    }
    // Lade die gespeicherten Stepper-Positionen in die Action-Objekte
    A_p1->setValuePreset(minWiperPos);
    logMessage(LOG_INFO, "Loaded minWiperPos %d into A_p1", minWiperPos);

    A_p2->setValuePreset(maxWiperPos);
    logMessage(LOG_INFO, "Loaded maxWiperPos %d into A_p2", maxWiperPos);

    mcp.digitalWrite(PIN_RELAIS_ONOFF, HIGH);
    mcp.digitalWrite(PIN_RELAIS_LIMIT, HIGH);
}

/**
 * Prüft, ob ein Gerät an der angegebenen I2C-Adresse antwortet.
 * @param addr Die I2C-Adresse, die geprüft werden soll.
 * @return true, wenn das Gerät antwortet (ACK), ansonsten false.
 */
bool checkI2CDevice(byte addr) {
  Wire.beginTransmission(addr);
  byte error = Wire.endTransmission();
  if (error == 0) {
    return true; // Gerät hat geantwortet (ACK)
  }
  return false; // Gerät nicht gefunden (NACK)
}

