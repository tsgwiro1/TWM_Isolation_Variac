/*************************************************************
Copyright(c) 2025 Roger Widmer & Michael Tanner

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
************************************************************ */

/*
    Name:       tt_esp32controller.ino
    Created:	  27.09.2025
    Author:     LondonWS\roger
*/

// version
#ifdef SIM
#define FW  "Firmware V3.3.0 (SIM)"
#else
#define FW  "Firmware V3.3.0"
#endif

// Includes for Libraries
#include <Arduino.h>
#include <FS.h>          // <-- DIESE ZEILE HINZUFÜGEN
#include <LittleFS.h>
using namespace fs;
#include "Action.h"
#include <AccelStepper.h>
#include <Wire.h>
#include <MCP23017.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TFT_eSPI.h> 
#include <SPI.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>
#include <Ticker.h>
#include <ESP32Encoder.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>

// Hardware definitions ESP32-S3
#define PIN_STEP 16
#define PIN_DIR 15
#define PIN_EN 7
#define PIN_ENCCLK 4
#define PIN_ENCDT 5
#define PIN_ENCSW 6
#define PIN_SCL 9
#define PIN_SDA 8
#define PIN_TX 18
#define PIN_RX 17
#define PIN_FANPWM 36
#define PIN_T_ONOFF 42
#define PIN_T_LIMIT 41
#define PIN_T_REG 40
#define PIN_T_P1 39
#define PIN_T_P2 38
#define PIN_T_P3 37
#define PIN_DISP_RESET 47
//#define PIN_MOSI 11
//#define PIN_MISO 13
//#define PIN_SCLK 12
#define PIN_CS1 10
#define PIN_DC 14
#define PIN_SW1 48
#define PIN_ONEWIRE 21
#define PIN_DISP_BL 35
#define PIN_ESP_STATUS 46
#define PIN_RX_STEPPER 2
#define PIN_TX_STEPPER 1

// Hardware definition MCP23017
#define PIN_RELAIS_ONOFF 8
#define PIN_RELAIS_LIMIT 9
#define PIN_LED_ONOFF 2
#define PIN_LED_LIMIT 1
#define PIN_LED_REG 0
#define PIN_LED_P1 3
#define PIN_LED_P2 4
#define PIN_LED_P3 5
#define PIN_LED_x10 6

// File und Standardwerte für die Settings
#define CONFIG_FILE "/config.json"
#define DEFAULT_WHIPER_MIN 0
#define DEFAULT_WHIPER_MAX 2000
#define DEFAULT_VOLTAGE_PRESET 0
#define DEFAULT_VOLTAGE_AT_MIN_POS 3.0f
#define DEFAULT_VOLTAGE_AT_MAX_POS 255.0f
#define DEFAULT_VOLTAGE_COARSE_MOVE 20.0f
#define DEFAULT_DEBUG true


// Function prototyps
void isr_stepper();

// Webserver
AsyncWebServer server(80); // Server auf Port 80 erstellen
AsyncWebSocket ws("/ws");

// Objects and Variable definitions
// Port Expander and I2C
#define MCP23017_ADDR 0x20
MCP23017 mcp = MCP23017(MCP23017_ADDR);

// Stepper
AccelStepper stepper(AccelStepper::DRIVER, PIN_STEP, PIN_DIR);
Ticker stepperTicker;
SemaphoreHandle_t stepperSemaphore;

#define STEPPERMAXSPEED             1000
#define STEPPERACCELERATION         1500
#define STEPPERHOMINGSPEED          150
#define STEPPERHOMINGSPEED2         10
#define STEPPERHOMINGRETRACT        25

// Encoder
#define ENCMIDPOINT 32767
#define ENCLOWSPEED 2
#define ENCHIGHSPEED 10
int lastEncPos = 0;
int encSpeed = ENCLOWSPEED;
ESP32Encoder encoder;

// System
#define CRITICAL_STACK_THRESHOLD 256 // Warnung bei weniger als 256 Bytes freiem Stack
volatile bool hardwareInitialized = false;  // "Wächter"-Variable, die den Zustand der Hardware-Initialisierung speichert
enum SystemMode { MODE_NORMAL, MODE_SETTINGS };
volatile SystemMode currentMode = MODE_NORMAL; // Standardmässig im Normalbetrieb starten
volatile bool requestEnterSettingsMode = false;
SemaphoreHandle_t tftMutex;  // Mutex zum Schutz des TFT-Displays
Ticker rebootTicker;
uint32_t lastStackCheck = 0;
const char* hostname = "twm_variac";

// Log-Level Definition
#define LOG_FILE "/system.log"
#define MAX_LOG_SIZE 20480 // Maximale Grösse der Log-Datei in Bytes (z.B. 20 KB)
enum LogLevel { LOG_INFO, LOG_WARN, LOG_ERROR };
const char* logLevelStrings[] = { "INFO", "WARN", "ERROR" };
volatile bool debugEnabled = true; // Steuert die Log-Ausgabe

// RAM-Puffer für die Log-Historie (ca. 4 KB)
String logHistory = "";
const int MAX_LOG_HISTORY = 4096;

// #4: Logging thread-safe — logMessage() formatiert nur noch und legt den Eintrag in eine
// Queue; ein einzelner Logger-Task übernimmt Serial, RAM-Historie, WebSocket und Flash-Write.
// (String/LittleFS/ws aus mehreren Tasks gleichzeitig war das größte Concurrency-Risiko.)
struct LogEntry {
  LogLevel level;
  char msg[300];                       // fertig formatierte Zeile inkl. Zeitstempel + '\n'
};
QueueHandle_t     logQueue = NULL;
SemaphoreHandle_t logHistoryMutex = NULL;   // schützt logHistory (Logger-Task vs. WS-Connect)
volatile uint32_t logDroppedCount = 0;      // wegen voller Queue verworfene Meldungen

// Transformer Whiper
#define MINWHIPERLIMIT -50
#define MAXWHIPERLIMIT 2500
volatile int whiperPos = 0;

volatile int minWhiperPos = 0;
volatile int maxWhiperPos = 2000;
volatile uint32_t last_encoder_change_time = 0;

// #5: Schutz geteilter Zustände.
// - calibMux: die 4 Kalibrierwerte (minWhiperPos/maxWhiperPos/minVoltageAtMinPos/
//   maxVoltageAtMaxPos) werden immer als SATZ geschrieben/gelesen — ohne Schutz könnte die
//   Regelungs-Mathematik einen halb-aktualisierten Satz sehen (falsche Anfahrposition).
// - stepperMux: serialisiert das whiperPos-Read-Modify-Write und alle AccelStepper-Aufrufe
//   (moveTo vs. run() aus verschiedenen Tasks — AccelStepper ist nicht thread-safe).
// Einzelne 32-bit-Skalare (setpoint_voltage, received_rms_value, …) bleiben bewusst nur
// volatile: ausgerichtete 32-bit-Zugriffe sind auf dem ESP32 atomar, und es gibt auf ihnen
// keine zusammengesetzten Read-Modify-Write-Sequenzen.
portMUX_TYPE calibMux   = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE stepperMux = portMUX_INITIALIZER_UNLOCKED;

// Sollwert-/Preset-Grenzen. Untergrenze fix 0; effektive Obergrenze liefert
// maxVoltageTarget() = min(kalibriertes Max, MAX_VOLTAGE_TARGET).
// MAX_VOLTAGE_TARGET ist die absolute Sicherheits-Obergrenze (Schutz bei defekter Kalibrierung).
#define MIN_VOLTAGE_TARGET  0
#define MAX_VOLTAGE_TARGET  260
volatile bool isRecallPreset = false;   // löst eine Vorsteuer-Anfahrt auf setpoint_voltage aus

// Phasen der neuen Spannungsregelung (Vorsteuerung + gain-Korrektur + Halten)
enum RegPhase {
  RP_IDLE,         // keine automatische Regelung
  RP_FEEDFORWARD,  // beschleunigte Anfahrt auf die geschätzte Zielposition
  RP_CORRECT,      // gain-gerechte Einzelkorrektur(en) bis im Deadband
  RP_HOLD          // Ziel erreicht, Sollwert halten (Drift-Trim)
};
volatile RegPhase regPhase = RP_IDLE;

// Voltage regulation
volatile bool is_regulation_active = false; 
volatile float setpoint_voltage = 0.0f;
volatile float minVoltageAtMinPos = 3.0f;
volatile float maxVoltageAtMaxPos = 255.0f;
volatile float voltageThresholdCoarseMove = 20.0f;  // Abstand SOLL / IST damit grob angefahren wird vor der Regelung - Verwendet im Zusammenhang mit Presets

// --- Parameter der Spannungsregelung (#17) ---
#define REG_DEADBAND_V           1.0f   // innerhalb +/- dieses Fehlers wird nicht korrigiert
#define REG_CORRECTION_DAMPING   0.8f    // Anteil der berechneten Korrektur (Schutz vor Überschwingen)
#define REG_SETTLE_MS            150    // Wartezeit nach Stepper-Stopp, bis gemessen wird
#define REG_DRIFT_PERSIST_MS     800    // im Halten: so lange außerhalb Deadband, bevor nachkorrigiert wird
#define REG_MAX_CORRECTION_STEPS 150    // Klemme je Einzelkorrektur (Schutz gegen Ausreißer-Messung)
#define REG_MAX_CORRECTIONS      5      // max. Korrekturiterationen pro Anfahrt
#define REG_FEEDFORWARD_UNDERSHOOT_V 5.0f // Vorsteuerung stoppt um diese Spannung kurz vor dem Ziel (in Fahrtrichtung)

// Periodizität für Regelung und Preset Handling
#define REGULATION_LOOP_PERIOD 100

// Actions
Action* A_onoff;
Action* A_limit;
Action* A_reg;
Action* A_p1;
Action* A_p2;
Action* A_p3;
Action* A_x10;

Action* g[3];

// Fan
#define MINFANPWM 10
#define MAXFANPWM 100
#define MINFANTEMP 30
#define MAXFANTEMP 60
#define NOSENSORFANSPEED 75
#define FAN_PWM_CHANNEL 0
#define FAN_PWM_FREQ 25000
#define FAN_PWM_RESOLUTION 8 // 8-bit = 0-255

// Temperature Sensor
OneWire oneWire(PIN_ONEWIRE);
DallasTemperature sensors(&oneWire);
DeviceAddress tempSensorAdr;
volatile float whiperTemp = 0.0;
volatile boolean tempSensorAvailable = false;

// Display
TFT_eSPI tft = TFT_eSPI();

struct displayValues {
	float temp;
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

displayValues actDispValues;

// Kommunikation mit dem Voltmeter über Serial1 (USART1-Link)
// RMS-Frame     (Voltmeter -> Controller): 0xAA HI LO CHK 0xBB        (Spannung*10)
// Antwort-Frame (Voltmeter -> Controller): 0xB5 CMD LEN [payload] CHK 0xBB
// Befehl-Frame  (Controller -> Voltmeter): 0xA5 CMD LEN [payload] CHK 0xBB
const byte RMS_SOF      = 0xAA;
const byte RSP_SOF      = 0xB5;
const byte LINK_CMD_SOF = 0xA5;
const byte FRAME_EOF    = 0xBB;
#define VM_CMD_GET_VERSION  0x01
#define VM_CMD_GET_STATUS   0x02
#define VM_CMD_SET_FACTOR   0x10
#define VM_CMD_SET_OFFSET   0x11
#define VM_CMD_RECAL        0x20
#define VM_CMD_CAL3_MEASURE 0x21
#define VM_CMD_CAL3_FINISH  0x22
#define VM_CMD_REBOOT       0x30
#define VM_CMD_RESET_DEFAULTS 0x31
#define VM_CMD_ENTER_BOOTLOADER 0x40

enum RxPhase {
  RXP_SOF,
  RXP_RMS_PAYLOAD, RXP_RMS_CHK, RXP_RMS_EOF,        // RMS-Frame
  RXP_RSP_CMD, RXP_RSP_LEN, RXP_RSP_DATA, RXP_RSP_CHK, RXP_RSP_EOF  // Antwort-Frame
};
RxPhase rxPhase = RXP_SOF;
byte rmsBuf[2];
byte rmsIdx = 0;
uint8_t rspCmd = 0, rspLen = 0, rspIdx = 0, rspChk = 0;
uint8_t rspBuf[64];
// Vom Voltmeter empfangene Antwort (für den Befehls-Link)
volatile bool    voltmeterResponseReady = false;
volatile uint8_t voltmeterResponseCmd   = 0;
volatile uint8_t voltmeterResponseLen   = 0;
uint8_t          voltmeterResponsePayload[64];

// --- Voltmeter-FW-Update (#30, AN3155-Host) ---
#define VM_FW_PATH      "/voltmeter_fw.bin"
#define VM_FLASH_BASE   0x08000000UL
#define VM_FW_MAX_SIZE  (124u * 1024u)   // F103CB hat 128 KB Flash, etwas Reserve
#define VM_FLASH_PAGE   1024u            // F103CB: 1 KB Flash-Page
#define VM_EEPROM_PAGE  127u             // letzte Page = emuliertes EEPROM (Kalibrierung) -> NICHT löschen
#define BL_BLOCK        256              // AN3155 Write-Memory: max. 256 Byte/Block
#define VM_UPDATE_RESULT_MS 5000         // Ergebnis (Erfolg/Fehler) so lange auf dem LCD zeigen (#32)
#define BL_ACK          0x79
#define BL_NACK         0x1F
#define BL_INIT         0x7F
enum VmUpdateState { VMU_IDLE, VMU_RUNNING, VMU_SUCCESS, VMU_ERROR };
volatile VmUpdateState vmUpdateState   = VMU_IDLE;
volatile int           vmUpdateProgress = 0;     // 0..100 %
volatile bool          vmUpdateRequested = false; // vom Web gesetzt, vom Update-Task abgearbeitet
volatile bool          vmUpdateSkipEnter = false; // Diagnose: ENTER_BOOTLOADER überspringen (VM bereits via BOOT0 im ROM-Loader)
char                   vmUpdateMessage[96] = "";  // letzte Status-/Fehlermeldung (Single-Writer: Update-Task)

// 'volatile', weil diese Variablen in der loop() und im Interrupt-Kontext verwendet werden
volatile bool new_value_available = false;
volatile float received_rms_value = 0.0;

// Datenfrische: Zeitpunkt des letzten gültigen Messwerts vom Voltmeter.
// received_rms_value wird nur verwendet, wenn er aktuell ist (Schutz bei Kabelbruch/Ausfall).
volatile uint32_t last_rms_received_time = 0;
#define RMS_TIMEOUT_MS 250   // Voltmeter sendet alle ~40 ms; nach 250 ms gilt der Wert als veraltet


// Definition der möglichen Systemzustände für die Status-LED
enum SystemState {
  STATE_WIFI_CONNECTING,
  STATE_WIFIMANAGER_AP,
  STATE_NORMAL_OPERATION,
  STATE_OTA_UPDATE,
  STATE_ERROR
};

// Globale, volatile Variable zur Steuerung des aktuellen Zustands
volatile SystemState currentSystemState = STATE_WIFI_CONNECTING;


// --- Funktions-Prototypen ---
void parseByte(byte b);
void logMessage(LogLevel level, const char* format, ...);

// ********************************************************************************
// Settings and Homing
// ********************************************************************************

/**
 * @brief Setzt eine Standard Konfiguration, damit das System lauffähig ist.
 * Es werden alle globalen Variabeln initialisiert, welche sonst aus dem config.json File gesetzt werden.
 */
void applyDefaultConfiguration() {
    // Standardwerte in die laufenden Variablen laden (Kalibrier-Satz atomar, #5)
    portENTER_CRITICAL(&calibMux);
    minWhiperPos = DEFAULT_WHIPER_MIN;
    maxWhiperPos = DEFAULT_WHIPER_MAX;
    minVoltageAtMinPos = DEFAULT_VOLTAGE_AT_MIN_POS;
    maxVoltageAtMaxPos = DEFAULT_VOLTAGE_AT_MAX_POS;
    portEXIT_CRITICAL(&calibMux);
    voltageThresholdCoarseMove = DEFAULT_VOLTAGE_COARSE_MOVE;
    debugEnabled = DEFAULT_DEBUG;
    if (hardwareInitialized) {
      A_p1->setValuePreset(DEFAULT_VOLTAGE_PRESET);
      A_p2->setValuePreset(DEFAULT_VOLTAGE_PRESET);
      A_p3->setValuePreset(DEFAULT_VOLTAGE_PRESET);
    }
}

/**
 * @brief Validiert und wendet eine neue Konfiguration an.
 * Prüft jeden Wert aus dem übergebenen JSON-Dokument auf Gültigkeit.
 * @param doc Das JsonObject, das die neue Konfiguration enthält.
 * @return String Ein JSON-String mit Fehlermeldungen. Ist leer bei Erfolg.
 */
String applyAndValidateConfig(JsonObject doc) {
  StaticJsonDocument<512> errorDoc;
  JsonObject errors = errorDoc.to<JsonObject>();

  // Temporäre Variablen, um Abhängigkeiten korrekt zu prüfen
  int tempMinPos = minWhiperPos;
  int tempMaxPos = maxWhiperPos;
  float tempMinVoltage = minVoltageAtMinPos;
  float tempMaxVoltage = maxVoltageAtMaxPos;
  float tempVoltageThresholdCoarseMove = voltageThresholdCoarseMove;
  bool tempDebugEnabled = debugEnabled;

  bool calibrationHasErrors = false;

  // --- Kalibrierung validieren ---
  if (doc.containsKey("calibration")) {
    JsonObject calibration = doc["calibration"];

    if (!calibration["min_pos"].is<int>()) {
        errors["calibration_min_pos"] = "must be an integer";
        calibrationHasErrors = true;
    } else {
      if (calibration.containsKey("min_pos")) {
        int val = calibration["min_pos"];
        if (val > 0 || val < MINWHIPERLIMIT) {
          errors["calibration_min_pos"] = "must be between " + String(MINWHIPERLIMIT) + " and 0";
          calibrationHasErrors = true;
        } else {
          tempMinPos = val;
        }
      }
    }

    // max_pos validieren (nur wenn min_pos gültig ist)
    if (calibration.containsKey("max_pos") && !errors.containsKey("calibration_min_pos")) {
      if (!calibration["max_pos"].is<int>()) {
        errors["calibration_max_pos"] = "must be an integer";
        calibrationHasErrors = true;
      } else {      
        int val = calibration["max_pos"];
        if (val <= tempMinPos) {
          errors["calibration_max_pos"] = "must be greater than min_pos (" + String(tempMinPos) + ")";
          calibrationHasErrors = true;
        } else if (val > MAXWHIPERLIMIT) {
          errors["calibration_max_pos"] = "cannot be greater than " + String(MAXWHIPERLIMIT);
          calibrationHasErrors = true;
        } else {
          tempMaxPos = val;
        }
      }
    }

    // min_voltage validieren
    if (calibration.containsKey("min_voltage")) {
      if (!calibration["min_voltage"].is<float>()) {
        errors["calibration_min_voltage"] = "must be a float";
        calibrationHasErrors = true;
      } else {
        float val = calibration["min_voltage"];
        if (val < 0) {
          errors["calibration_min_voltage"] = "cannot be negative";
          calibrationHasErrors = true;
        } else {
          tempMinVoltage = val;
        }
      }
    }

    // max_voltage validieren (nur wenn min_voltage gültig ist)
    if (calibration.containsKey("max_voltage") && !errors.containsKey("calibration_min_voltage")) {
      if (!calibration["max_voltage"].is<float>()) {
        errors["calibration_max_voltage"] = "must be a float";
        calibrationHasErrors = true;
      } else {
        float val = calibration["max_voltage"];
        if (val <= tempMinVoltage) {
          errors["calibration_max_voltage"] = "must be greater than min_voltage (" + String(tempMinVoltage, 1) + ")";
          calibrationHasErrors = true;
        } else {
          tempMaxVoltage = val;
        }
      }
    }
  }

  // --- Presets validieren ---
  if (doc.containsKey("presets")) {
    if (calibrationHasErrors) {
      errors["presets"] = "Cannot validate presets due to errors in calibration section.";
    } else {
      JsonObject presets = doc["presets"];
      if (presets.containsKey("p1") && presets["p1"].is<int>()) {
        if (presets["p1"] < 0 || presets["p1"] > (int)tempMaxVoltage) {
          errors["preset_p1"] = "must be between 0 and " + String((int)tempMaxVoltage);
        }
      } else {
        errors["preset_p1"] = "must be an integer";
      }
      if (presets.containsKey("p2") && presets["p2"].is<int>()) {
        if (presets["p2"] < 0 || presets["p2"] > (int)tempMaxVoltage) {
          errors["preset_p2"] = "must be between 0 and " + String((int)tempMaxVoltage);
        }
      } else {
        errors["preset_p2"] = "must be an integer";
      }
      if (presets.containsKey("p3") && presets["p3"].is<int>()) {
        if (presets["p3"] < 0 || presets["p3"] > (int)tempMaxVoltage) {
          errors["preset_p3"] = "must be between 0 and " + String((int)tempMaxVoltage);
        }
      } else {
        errors["preset_p3"] = "must be an integer";
      }
    }
  }

  // --- System validieren (und anwenden, da unkritisch) ---
  if (doc.containsKey("system")) {
    JsonObject system = doc["system"];

    // debug_enabled validieren
    if (system.containsKey("debug_enabled")) {
        // Prüfe, ob der Wert ein Boolean ist
        if (system["debug_enabled"].is<bool>()) {
            tempDebugEnabled = system["debug_enabled"];
        } else {
            // Wenn nicht, füge eine Fehlermeldung hinzu
            errors["debug_enabled"] = "must be a boolean (true or false)";
        }
    }
    // coarse_move_threshold validieren
    if (system.containsKey("coarse_move_threshold")) {
      if (!system["coarse_move_threshold"].is<float>()) {
        errors["coarse_move_threshold"] = "must be a float";
      } else {       
        float val = system["coarse_move_threshold"];
        if (val < 0) {
            errors["coarse_move_threshold"] = "cannot be negative";
        } else {
            tempVoltageThresholdCoarseMove = val;
        }
      }
    }
  }

  // --- Finale Entscheidung ---
  if (errors.size() == 0) {
    // KEINE FEHLER: Wende die validierten Werte auf die globalen Variablen an.
    // Kalibrier-Satz atomar schreiben (#5) — Leser (Regelung) sehen nie einen Mischzustand.
    portENTER_CRITICAL(&calibMux);
    minWhiperPos = tempMinPos;
    maxWhiperPos = tempMaxPos;
    minVoltageAtMinPos = tempMinVoltage;
    maxVoltageAtMaxPos = tempMaxVoltage;
    portEXIT_CRITICAL(&calibMux);
    voltageThresholdCoarseMove = tempVoltageThresholdCoarseMove;
    debugEnabled = tempDebugEnabled;
    
    // Wende die validierten Presets an
    if (doc.containsKey("presets") && hardwareInitialized) {
      JsonObject presets = doc["presets"];
      if (presets.containsKey("p1")) A_p1->setValuePreset(presets["p1"]);
      if (presets.containsKey("p2")) A_p2->setValuePreset(presets["p2"]);
      if (presets.containsKey("p3")) A_p3->setValuePreset(presets["p3"]);
    }
    logMessage(LOG_INFO, "Configuration successfully validated and applied.");
    return ""; // Leerer String signalisiert Erfolg
  } else {
    // FEHLER: Gib den JSON-String mit den Fehlermeldungen zurück
    String errorJson;
    serializeJson(errorDoc, errorJson);
    logMessage(LOG_ERROR, "Configuration validation failed: %s", errorJson.c_str());
    return errorJson;
  }
}

/**
 * @brief Validiert die übergebene Stepper-Konfiguration.
 * Prüft jeden Wert aus dem JSON-Dokument auf Gültigkeit.
 * @param doc Das JsonObject, das die neue Konfiguration enthält.
 * @return String Ein JSON-String mit Fehlermeldungen. Ist leer bei Erfolg.
 */
String applyAndValidateStepperConfig(JsonObject doc) {
  StaticJsonDocument<512> errorDoc;
  JsonObject errors = errorDoc.to<JsonObject>();

  // --- StallGuard Threshold (Param 174) ---
  // Gültiger Bereich: -64 bis 63
  if (doc.containsKey("stallguard_threshold")) {
    if (!doc["stallguard_threshold"].is<int>()) {
      errors["stallguard_threshold"] = "must be an integer";
    } else {
      int val = doc["stallguard_threshold"];
      if (val < -64 || val > 63) {
        errors["stallguard_threshold"] = "must be between -64 and 63";
      }
    }
  } else {
    errors["stallguard_threshold"] = "is missing";
  }

  // --- CoolStep Speed Threshold (Param 182) ---
  // Gültiger Bereich: 0 bis 1048575
  if (doc.containsKey("coolstep_speed_threshold")) {
    if (!doc["coolstep_speed_threshold"].is<int>()) {
      errors["coolstep_speed_threshold"] = "must be an integer";
    } else {
      long val = doc["coolstep_speed_threshold"];
      if (val < 0 || val > 1048575) {
        errors["coolstep_speed_threshold"] = "must be between 0 and 1048575";
      }
    }
  } else {
    errors["coolstep_speed_threshold"] = "is missing";
  }

  // --- CoolStep Hysteresis Start (Param 172) ---
  // Gültiger Bereich: 0 bis 15
  if (doc.containsKey("coolstep_hyst_start")) {
    if (!doc["coolstep_hyst_start"].is<int>()) {
      errors["coolstep_hyst_start"] = "must be an integer";
    } else {
      int val = doc["coolstep_hyst_start"];
      if (val < 0 || val > 15) {
        errors["coolstep_hyst_start"] = "must be between 0 and 15";
      }
    }
  } else {
    errors["coolstep_hyst_start"] = "is missing";
  }

  // --- CoolStep Min Current (Param 168) ---
  // Gültiger Wert: 0 oder 1
  if (doc.containsKey("coolstep_min_current")) {
    if (!doc["coolstep_min_current"].is<int>()) {
      errors["coolstep_min_current"] = "must be an integer (0 or 1)";
    } else {
      int val = doc["coolstep_min_current"];
      if (val != 0 && val != 1) {
        errors["coolstep_min_current"] = "must be 0 (1/2 current) or 1 (1/4 current)";
      }
    }
  } else {
    errors["coolstep_min_current"] = "is missing";
  }

  // --- Finale Entscheidung ---
  if (errors.size() == 0) {
    logMessage(LOG_INFO, "Stepper configuration successfully validated.");
    return ""; // Leerer String signalisiert Erfolg
  } else {
    // FEHLER: Gib den JSON-String mit den Fehlermeldungen zurück
    String errorJson;
    serializeJson(errorDoc, errorJson);
    logMessage(LOG_ERROR, "Stepper configuration validation failed: %s", errorJson.c_str());
    return errorJson;
  }
}

/**
 * @brief Speichert die aktuellen Konfigurationswerte in die config.json-Datei.
 */
void saveConfiguration() {
  StaticJsonDocument<512> doc;

  // Fülle das Dokument mit den aktuellen Werten aus den globalen Variablen
  doc["system"]["debug_enabled"] = debugEnabled;
  doc["system"]["coarse_move_threshold"] = voltageThresholdCoarseMove;

  doc["calibration"]["min_pos"] = minWhiperPos;
  doc["calibration"]["max_pos"] = maxWhiperPos;
  doc["calibration"]["min_voltage"] = serialized(String(minVoltageAtMinPos, 1));
  doc["calibration"]["max_voltage"] = serialized(String(maxVoltageAtMaxPos, 1));
  
  if (hardwareInitialized && currentMode == MODE_NORMAL) {
    doc["presets"]["p1"] = A_p1->getValuePreset();
    doc["presets"]["p2"] = A_p2->getValuePreset();
    doc["presets"]["p3"] = A_p3->getValuePreset();
  } else {
    doc["presets"]["p1"] = 0;
    doc["presets"]["p2"] = 0;
    doc["presets"]["p3"] = 0;
  }

  File configFile = LittleFS.open(CONFIG_FILE, "w");
  if (!configFile) {
    logMessage(LOG_ERROR, "Failed to open config file for writing.");
    return;
  }

  serializeJson(doc, configFile);
  configFile.close();
  logMessage(LOG_INFO, "Configuration saved to LittleFS.");
}

/**
 * @brief Lädt alle Konfigurationswerte aus der config.json-Datei.
 * Wenn die Datei nicht existiert oder fehlerhaft ist, werden Standardwerte angewendet und eine neue Datei erstellt.
 * @return true falls die Konfiguration erfolgreich geladen und angewendet werden konnte.
 */
boolean loadConfiguration() {
  File configFile = LittleFS.open(CONFIG_FILE, "r");
  if (!configFile) {
    logMessage(LOG_WARN, "Config file not found. Applying defaults and creating new file.");
    applyDefaultConfiguration(); // Setzt globale Variablen auf Standardwerte
    saveConfiguration();       // Speichert diese Standardwerte in eine neue Datei
    return false;
  }

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();

  if (error) {
    logMessage(LOG_ERROR, "Failed to parse config file, applying defaults. Error: %s", error.c_str());
    applyDefaultConfiguration();
    saveConfiguration(); // Speichere eine saubere Datei, um die korrupte zu überschreiben
    return false;
  }

  // Rufe die zentrale Validierungs-Funktion auf
  String validationError = applyAndValidateConfig(doc.as<JsonObject>());
  
  if (!validationError.isEmpty()) {
    // Wenn die geladene Konfiguration ungültig ist, wende stattdessen die Standardwerte an
    logMessage(LOG_ERROR, "Loaded config is invalid, applying defaults.");
    applyDefaultConfiguration();
    return false;
  }
  return true;
}
/**
 * @brief Initialisiert und startet den Ticker, der als "Wecker" für den stepperTask dient.
 */
void initStepperCallback() {
  // Der Ticker läuft mit 10kHz (alle 100 Mikrosekunden) und ruft die extrem kurze ISR auf.
  stepperTicker.attach_us(100, isr_stepper);
}

/**
 * @brief Führt die Homing-Sequenz für den Schrittmotor durch.
 * Fährt den Motor an den mechanischen Anschlag (via Endschalter PIN_SW1),
 * kalibriert die Position auf 0 und lädt die gespeicherten Limits und Presets.
 */
void homing() {
    stepperTicker.detach(); // Stoppt den Ticker/Wecker vor der manuellen Bewegung

    // find endstop fast - low side
    stepper.setSpeed(STEPPERHOMINGSPEED * -1.0);
    while (digitalRead(PIN_SW1)) {
        stepper.runSpeed();
    }
    // retract from endstop
    stepper.setSpeed(STEPPERHOMINGSPEED);
    while (!digitalRead(PIN_SW1)) {
        stepper.runSpeed();
    }
    stepper.move(STEPPERHOMINGRETRACT);
    while (stepper.distanceToGo()) {
        stepper.runSpeedToPosition();
    }
    // approach endstop slower to find exact position
    stepper.setSpeed(STEPPERHOMINGSPEED2 * -1.0);
    while (digitalRead(PIN_SW1)) {
        stepper.runSpeed();
    }
    stepper.setCurrentPosition(0);
    whiperPos = 0;
    initStepperCallback(); // Startet den Ticker/Wecker wieder für den normalen Betrieb
}

// ********************************************************************************
// Hardware and System control functions
// ********************************************************************************
/**
 * @brief Bewegt den Schleifer um eine relative Anzahl von Schritten.
 * @param delta Die Anzahl der Schritte, um die bewegt werden soll (positiv oder negativ).
 */
void setWhiperRelativ(int delta) {
    // Delta-Anwendung läuft komplett unter stepperMux (in setWhiperMove), damit sich
    // gleichzeitige relative Bewegungen aus mehreren Tasks nicht gegenseitig verlieren. (#5)
    setWhiperMove(delta, true);
}

/**
 * @brief Bewegt den Schleifer zu einer absoluten Position.
 * Die Position wird durch minWhiperPos und maxWhiperPos begrenzt.
 * @param value Die absolute Zielposition.
 */
void setWhiperAbsolut(int value) {
    setWhiperMove(value, false);
}

/**
 * @brief Gemeinsamer Kern für relative/absolute Schleifer-Bewegung (#5).
 * whiperPos-Read-Modify-Write und stepper.moveTo() laufen atomar unter stepperMux
 * (serialisiert gegen stepper.run() im Stepper-Task und gegen andere Aufrufer).
 */
void setWhiperMove(int value, bool relative) {
    int minPos, maxPos;
    float minV, maxV;
    getCalibration(minPos, maxPos, minV, maxV);

    portENTER_CRITICAL(&stepperMux);
    if (relative) value += whiperPos;
    whiperPos = constrain(value, minPos, maxPos);
    stepper.moveTo(whiperPos);
    portEXIT_CRITICAL(&stepperMux);
}

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
 * @brief Setzt die Lüftergeschwindigkeit über PWM.
 * @param v Die Geschwindigkeit in Prozent (0-100).
 */
void setFanSpeed(uint8_t v) {
    v = constrain(v, MINFANPWM, MAXFANPWM);
    // Konvertiere den 0-100% Wert auf die 8-bit Auflösung (0-255)
    uint32_t dutyCycle = map(v, 0, 100, 0, 255);
    ledcWrite(FAN_PWM_CHANNEL, dutyCycle);
}

/**
 * @brief Liest die Temperatur vom OneWire-Sensor.
 * @return float Die aktuelle Temperatur in Grad Celsius.
 */
float getTemperature() {
    float t = whiperTemp;
    if (sensors.isConversionComplete()) {
        t = sensors.getTempC(tempSensorAdr);
    }
    sensors.requestTemperatures();
    return t;
}

/**
 * @brief Passt die Lüftergeschwindigkeit basierend auf der Temperatur an.
 * @param temp Die aktuelle Temperatur in Grad Celsius.
 */
void updateFan(float temp) {
    uint8_t p;
    if (temp < MINFANTEMP) {
        p = MINFANPWM;
    }
    else if (temp > MAXFANTEMP) {
        p = MAXFANPWM;
    }
    else {
        p = map(temp, MINFANTEMP, MAXFANTEMP, MINFANPWM, MAXFANPWM);
    }
    setFanSpeed(p);
}

/**
 * @brief Liest den aktuellen Zählerstand des Hardware-Encoders.
 * @return int Der rohe Zählerstand.
 */
int getEncoderCount() {
    return encoder.getCount();
}

/**
 * @brief Parst ein einzelnes Byte aus dem seriellen Datenstrom.
 * @param b Das zu verarbeitende Byte.
 */
void parseByte(byte b) {
  switch (rxPhase) {
    case RXP_SOF:
      if (b == RMS_SOF)      { rmsIdx = 0; rxPhase = RXP_RMS_PAYLOAD; }
      else if (b == RSP_SOF) { rspIdx = 0; rspChk = 0; rxPhase = RXP_RSP_CMD; }
      break;

    // --- RMS-Frame (Spannungswert) ---
    case RXP_RMS_PAYLOAD:
      rmsBuf[rmsIdx++] = b;
      if (rmsIdx >= 2) rxPhase = RXP_RMS_CHK;
      break;
    case RXP_RMS_CHK:
      rxPhase = (b == (byte)(rmsBuf[0] + rmsBuf[1])) ? RXP_RMS_EOF : RXP_SOF;
      break;
    case RXP_RMS_EOF:
      if (b == FRAME_EOF) {
        uint16_t int_value = ((uint16_t)rmsBuf[0] << 8) | rmsBuf[1];
        received_rms_value = (float)int_value / 10.0f;
        new_value_available = true;
      }
      rxPhase = RXP_SOF;
      break;

    // --- Antwort-Frame (Voltmeter -> Controller) ---
    case RXP_RSP_CMD:
      rspCmd = b; rspChk = b; rxPhase = RXP_RSP_LEN;
      break;
    case RXP_RSP_LEN:
      rspLen = b; rspChk += b; rspIdx = 0;
      if (rspLen > sizeof(rspBuf)) rxPhase = RXP_SOF;              // ungültige Länge
      else rxPhase = (rspLen > 0) ? RXP_RSP_DATA : RXP_RSP_CHK;
      break;
    case RXP_RSP_DATA:
      rspBuf[rspIdx++] = b; rspChk += b;
      if (rspIdx >= rspLen) rxPhase = RXP_RSP_CHK;
      break;
    case RXP_RSP_CHK:
      rxPhase = (b == rspChk) ? RXP_RSP_EOF : RXP_SOF;
      break;
    case RXP_RSP_EOF:
      if (b == FRAME_EOF) {
        voltmeterResponseCmd = rspCmd;
        voltmeterResponseLen = rspLen;
        memcpy(voltmeterResponsePayload, rspBuf, rspLen);
        voltmeterResponseReady = true;
      }
      rxPhase = RXP_SOF;
      break;
  }
}

/**
 * @brief Sendet einen Befehls-Frame an das Voltmeter: 0xA5 CMD LEN [payload] CHK 0xBB.
 */
void sendVoltmeterCommand(uint8_t cmd, const uint8_t* payload, uint8_t len) {
  uint8_t chk = cmd + len;
  Serial1.write(LINK_CMD_SOF);
  Serial1.write(cmd);
  Serial1.write(len);
  for (uint8_t i = 0; i < len; i++) { Serial1.write(payload[i]); chk += payload[i]; }
  Serial1.write((uint8_t)(chk & 0xFF));
  Serial1.write(FRAME_EOF);
}

/**
 * @brief Sendet einen Befehl ans Voltmeter und wartet auf die passende Antwort.
 * Die Antwort wird vom communicationTask geparst (setzt voltmeterResponse*).
 * @return true, wenn rechtzeitig eine Antwort mit demselben CMD kam.
 */
bool voltmeterRequest(uint8_t cmd, const uint8_t* payload, uint8_t len, uint32_t timeoutMs) {
  voltmeterResponseReady = false;
  sendVoltmeterCommand(cmd, payload, len);
  uint32_t t0 = millis();
  while (!voltmeterResponseReady && (millis() - t0) < timeoutMs) {
    delay(5); // yieldet -> communicationTask verarbeitet den Antwort-Frame
  }
  return voltmeterResponseReady && voltmeterResponseCmd == cmd;
}

/**
 * @brief Liest die 4 Kalibrierwerte als konsistenten Satz (#5).
 * Schreiber (Web-Config, /api/calibration/save, Settings-Modus) schreiben unter demselben
 * calibMux — Leser sehen dadurch nie einen halb-aktualisierten Satz.
 */
void getCalibration(int& minPos, int& maxPos, float& minV, float& maxV) {
  portENTER_CRITICAL(&calibMux);
  minPos = minWhiperPos;
  maxPos = maxWhiperPos;
  minV = minVoltageAtMinPos;
  maxV = maxVoltageAtMaxPos;
  portEXIT_CRITICAL(&calibMux);
}

/**
 * @brief Schätzt die Stepper-Position für eine gegebene Zielspannung.
 * Verwendet eine lineare Interpolation zwischen den kalibrierten Minimal-/Maximalwerten.
 * @param target_voltage Die gewünschte Ausgangsspannung.
 * @return int Die geschätzte absolute Stepper-Position.
 */
int estimatePositionForVoltage(float target_voltage) {
  int minPos, maxPos;
  float minV, maxV;
  getCalibration(minPos, maxPos, minV, maxV);

  // Begrenze die Zielspannung auf den physikalisch möglichen Bereich
  if (target_voltage < minV) target_voltage = minV;
  if (target_voltage > maxV) target_voltage = maxV;

  // Lineare Konvertierung (mit minPos als Offset: bei target = minV ergibt sich minPos,
  // nicht 0 — minWhiperPos ist kalibrierungsbedingt negativ).
  int estimated_pos = minPos + (target_voltage - minV) * (maxPos - minPos) / (maxV - minV);

  return estimated_pos;
}

/**
 * @brief Liefert die effektive obere Sollwert-/Preset-Grenze in Volt.
 * Einzige Quelle für das Spannungs-Maximum: der real erreichbare, kalibrierte Wert
 * (maxVoltageAtMaxPos), zusätzlich durch die absolute Sicherheits-Obergrenze
 * MAX_VOLTAGE_TARGET gedeckelt. Untergrenze ist überall MIN_VOLTAGE_TARGET (0).
 * @return int Die maximal erlaubte Zielspannung.
 */
int maxVoltageTarget() {
  int calMax = (int)lround(maxVoltageAtMaxPos);
  return calMax < MAX_VOLTAGE_TARGET ? calMax : MAX_VOLTAGE_TARGET;
}

/**
 * @brief Prüft, ob ein aktueller (frischer) Messwert vom Voltmeter vorliegt.
 * @return true, wenn innerhalb von RMS_TIMEOUT_MS ein gültiger RMS-Wert empfangen wurde.
 */
bool isVoltageDataFresh() {
  return last_rms_received_time != 0 && (millis() - last_rms_received_time) < RMS_TIMEOUT_MS;
}

/**
 * @brief Liefert die Prozess-Verstärkung in Volt pro Schritt aus der Kalibrierung.
 * Kehrwert dient als Umrechnung Spannungsfehler -> Korrekturschritte.
 * @return float Volt pro Schritt, oder 0 wenn die Kalibrierung ungültig ist.
 */
float voltsPerStep() {
  int minPos, maxPos;
  float minV, maxV;
  getCalibration(minPos, maxPos, minV, maxV);
  int span = maxPos - minPos;
  if (span == 0) return 0.0f;
  return (maxV - minV) / (float)span;
}

/**
 * @brief Initialisiert den Webserver und definiert alle Routen (URLs).
 */
void initWebServer() {
  // Liefere alle statischen Dateien (.html, .css, .js) automatisch aus dem Root-Verzeichnis von LittleFS
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // API-Route für die Datenabfrage ("/data"): Liefere IST/SOLL und Tasten-Zustände als JSON
  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request){
    StaticJsonDocument<256> doc;

    doc["ist"] = received_rms_value;
    doc["soll"] = setpoint_voltage;
    doc["ist_fresh"] = isVoltageDataFresh();

    // Zustände der Action-Objekte hinzufügen (benötigt Zeiger-Architektur)
    if (hardwareInitialized) {
      doc["state_onoff"] = (bool)A_onoff->getState();
      doc["state_limit"] = (bool)A_limit->getState();
      doc["state_reg"]   = (bool)A_reg->getState();
      doc["state_p1"]    = (bool)A_p1->getState();
      doc["state_p2"]    = (bool)A_p2->getState();
      doc["state_p3"]    = (bool)A_p3->getState();
    }
    
    String jsonString;
    serializeJson(doc, jsonString);
    request->send(200, "application/json", jsonString);
  });

  // API-Route zum Setzen des Sollwerts ("/api/setpoint?voltage=...")
  server.on("/api/setpoint", HTTP_GET, [] (AsyncWebServerRequest *request) {
    if (!hardwareInitialized) {
      request->send(503, "application/json", "{\"status\":\"error\",\"message\":\"Hardware not ready\"}");
      return;
    }
    if (request->hasParam("voltage")) {
      float new_voltage = request->getParam("voltage")->value().toFloat();
      logMessage(LOG_INFO, "API: New setpoint received -> %.1f V", new_voltage);
      resetPresetActions();
      setpoint_voltage = constrain(new_voltage, (float)MIN_VOLTAGE_TARGET, (float)maxVoltageTarget());
      isRecallPreset = true;
      request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Setpoint updated\"}");
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing 'voltage' parameter\"}");
    }
  });

  // API-Route zum Auslösen von Aktionen ("/api/command?action=...")
  server.on("/api/command", HTTP_GET, [] (AsyncWebServerRequest *request) {
    if (!hardwareInitialized) {
      request->send(503, "application/json", "{\"status\":\"error\",\"message\":\"Hardware not ready\"}");
      return;
    }
    if (request->hasParam("action")) {
      String action = request->getParam("action")->value();
      logMessage(LOG_INFO, "API: Command received -> %s", action.c_str());
      
      if (action == "toggle_output") { A_onoff->toggle(); }
      else if (action == "toggle_limit") { A_limit->toggle(); }
      else if (action == "toggle_regulation") { A_reg->toggle(); }
      else if (action == "recall_p1") { cb_ValueAction(A_p1, ButtonEvent::RELEASED); }
      else if (action == "recall_p2") { cb_ValueAction(A_p2, ButtonEvent::RELEASED); }
      else if (action == "recall_p3") { cb_ValueAction(A_p3, ButtonEvent::RELEASED); }
      else if (action == "enter_settings") { requestEnterSettingsMode = true; is_regulation_active = false; } 
      else {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Unknown action\"}");
        return;
      }
      request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Command executed\"}");
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing 'action' parameter\"}");
    }
  });

  // API-Route für den kompletten Gerätestatus
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
    StaticJsonDocument<512> doc; // Etwas mehr Platz für die zusätzlichen Daten

    doc["voltage_actual"] = received_rms_value;
    doc["voltage_fresh"] = isVoltageDataFresh();
    doc["voltage_setpoint"] = setpoint_voltage;
    doc["temperature"] = whiperTemp;
    doc["stepper_position"] = whiperPos;
    doc["is_hardware_ok"] = hardwareInitialized;
    doc["fw_version"] = FW; 
    
    JsonObject states = doc.createNestedObject("states");
    if (hardwareInitialized) {
      states["output_on"] = (bool)A_onoff->getState();
      states["limit_on"] = (bool)A_limit->getState();
      states["regulation_on"] = (bool)A_reg->getState();
    }
    
    String jsonString;
    serializeJson(doc, jsonString);
    request->send(200, "application/json", jsonString);
  });

  // API-Route: Voltmeter-Version über den seriellen Link abfragen (Paket J)
  server.on("/api/voltmeter/version", HTTP_GET, [](AsyncWebServerRequest *request){
    if (voltmeterRequest(VM_CMD_GET_VERSION, nullptr, 0, 400)) {
      char ver[65];
      uint8_t n = voltmeterResponseLen < 64 ? voltmeterResponseLen : 64;
      memcpy(ver, voltmeterResponsePayload, n);
      ver[n] = '\0';
      StaticJsonDocument<128> doc;
      doc["status"] = "success";
      doc["version"] = ver;
      String out; serializeJson(doc, out);
      request->send(200, "application/json", out);
    } else {
      request->send(504, "application/json", "{\"status\":\"error\",\"message\":\"No response from voltmeter\"}");
    }
  });

  // API-Route: Voltmeter-Status (Skalierungsfaktor, Spannungs-Offset, ADC-Nullpunkt)
  server.on("/api/voltmeter/status", HTTP_GET, [](AsyncWebServerRequest *request){
    if (voltmeterRequest(VM_CMD_GET_STATUS, nullptr, 0, 400) && voltmeterResponseLen >= 12) {
      float factor, voff, adcz;
      memcpy(&factor, voltmeterResponsePayload + 0, 4);
      memcpy(&voff,   voltmeterResponsePayload + 4, 4);
      memcpy(&adcz,   voltmeterResponsePayload + 8, 4);
      StaticJsonDocument<192> doc;
      doc["status"] = "success";
      doc["scaling_factor"] = factor;
      doc["voltage_offset"] = voff;
      doc["adc_zero_offset"] = adcz;
      String out; serializeJson(doc, out);
      request->send(200, "application/json", out);
    } else {
      request->send(504, "application/json", "{\"status\":\"error\",\"message\":\"No response from voltmeter\"}");
    }
  });

  // API-Route: Skalierungsfaktor des Voltmeters setzen (+ EEPROM speichern)
  server.on("/api/voltmeter/factor", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->hasParam("value")) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing 'value' parameter\"}");
      return;
    }
    float v = request->getParam("value")->value().toFloat();
    uint8_t b[4];
    memcpy(b, &v, 4);
    if (voltmeterRequest(VM_CMD_SET_FACTOR, b, 4, 400)) {
      bool ok = (voltmeterResponseLen >= 1 && voltmeterResponsePayload[0] == 1);
      if (ok) {
        request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Factor set\"}");
      } else {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Factor rejected (range 100..1000)\"}");
      }
    } else {
      request->send(504, "application/json", "{\"status\":\"error\",\"message\":\"No response from voltmeter\"}");
    }
  });

  // API-Route: Spannungs-Offset des Voltmeters setzen (+ EEPROM speichern)
  server.on("/api/voltmeter/offset", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->hasParam("value")) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing 'value' parameter\"}");
      return;
    }
    float v = request->getParam("value")->value().toFloat();
    uint8_t b[4];
    memcpy(b, &v, 4);
    if (voltmeterRequest(VM_CMD_SET_OFFSET, b, 4, 400)) {
      bool ok = (voltmeterResponseLen >= 1 && voltmeterResponsePayload[0] == 1);
      if (ok) {
        request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Offset set\"}");
      } else {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Offset rejected (range -50..50)\"}");
      }
    } else {
      request->send(504, "application/json", "{\"status\":\"error\",\"message\":\"No response from voltmeter\"}");
    }
  });

  // API-Route: Auto-Zero-Kalibrierung des Voltmeters starten (läuft danach mehrere Sekunden)
  server.on("/api/voltmeter/autozero", HTTP_GET, [](AsyncWebServerRequest *request){
    if (voltmeterRequest(VM_CMD_RECAL, nullptr, 0, 400)) {
      request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Auto-zero started (takes a few seconds)\"}");
    } else {
      request->send(504, "application/json", "{\"status\":\"error\",\"message\":\"No response from voltmeter\"}");
    }
  });

  // API-Route: 3-Punkt-Kalibrierung – einen Punkt messen (index + anliegende Referenzspannung)
  server.on("/api/voltmeter/cal3/measure", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->hasParam("index") || !request->hasParam("voltage")) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing 'index' or 'voltage'\"}");
      return;
    }
    uint8_t payload[5];
    payload[0] = (uint8_t)request->getParam("index")->value().toInt();
    float v = request->getParam("voltage")->value().toFloat();
    memcpy(payload + 1, &v, 4);
    // Messung mittelt ~2 s -> längerer Timeout.
    if (voltmeterRequest(VM_CMD_CAL3_MEASURE, payload, 5, 3000)) {
      bool ok = (voltmeterResponseLen >= 1 && voltmeterResponsePayload[0] == 1);
      request->send(ok ? 200 : 400, "application/json",
                    ok ? "{\"status\":\"success\",\"message\":\"Point measured\"}"
                       : "{\"status\":\"error\",\"message\":\"Invalid point\"}");
    } else {
      request->send(504, "application/json", "{\"status\":\"error\",\"message\":\"No response from voltmeter\"}");
    }
  });

  // API-Route: Voltmeter neu starten (Soft-Reset)
  server.on("/api/voltmeter/reboot", HTTP_GET, [](AsyncWebServerRequest *request){
    if (voltmeterRequest(VM_CMD_REBOOT, nullptr, 0, 400)) {
      request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Voltmeter rebooting\"}");
    } else {
      request->send(504, "application/json", "{\"status\":\"error\",\"message\":\"No response from voltmeter\"}");
    }
  });

  // API-Route: Voltmeter-Kalibrierung auf Standardwerte zurücksetzen
  server.on("/api/voltmeter/reset-defaults", HTTP_GET, [](AsyncWebServerRequest *request){
    if (voltmeterRequest(VM_CMD_RESET_DEFAULTS, nullptr, 0, 400)) {
      request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Calibration reset to defaults\"}");
    } else {
      request->send(504, "application/json", "{\"status\":\"error\",\"message\":\"No response from voltmeter\"}");
    }
  });

  // API-Route: 3-Punkt-Kalibrierung abschließen – Regression rechnen + speichern
  server.on("/api/voltmeter/cal3/finish", HTTP_GET, [](AsyncWebServerRequest *request){
    if (voltmeterRequest(VM_CMD_CAL3_FINISH, nullptr, 0, 600) && voltmeterResponseLen >= 9) {
      bool ok = (voltmeterResponsePayload[0] == 1);
      if (ok) {
        float factor, voff;
        memcpy(&factor, voltmeterResponsePayload + 1, 4);
        memcpy(&voff,   voltmeterResponsePayload + 5, 4);
        StaticJsonDocument<160> doc;
        doc["status"] = "success";
        doc["scaling_factor"] = factor;
        doc["voltage_offset"] = voff;
        String out; serializeJson(doc, out);
        request->send(200, "application/json", out);
      } else {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Need at least 2 measured points\"}");
      }
    } else {
      request->send(504, "application/json", "{\"status\":\"error\",\"message\":\"No response from voltmeter\"}");
    }
  });

  // --- Voltmeter-FW-Update (#30) ---
  // Upload der .bin nach LittleFS (POST multipart). Antwort kommt nach dem Upload.
  server.on("/api/voltmeter/update/upload", HTTP_POST,
    [](AsyncWebServerRequest *request){
      request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Upload complete\"}");
    },
    [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final){
      static File up;
      if (vmUpdateState == VMU_RUNNING) return; // während eines laufenden Updates nichts annehmen
      if (index == 0) up = LittleFS.open(VM_FW_PATH, "w");
      if (up) up.write(data, len);
      if (final && up) up.close();
    });

  // Update starten: prüft Datei, stößt den Update-Task an.
  server.on("/api/voltmeter/update/start", HTTP_GET, [](AsyncWebServerRequest *request){
    if (vmUpdateState == VMU_RUNNING) {
      request->send(409, "application/json", "{\"status\":\"error\",\"message\":\"Update already running\"}");
      return;
    }
    if (!LittleFS.exists(VM_FW_PATH)) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"No firmware uploaded\"}");
      return;
    }
    // Diagnose: ?skipenter=1 überspringt ENTER_BOOTLOADER (VM bereits via BOOT0 im ROM-Loader).
    vmUpdateSkipEnter = request->hasParam("skipenter") && request->getParam("skipenter")->value() == "1";
    vmUpdSet(VMU_RUNNING, 0, "Update gestartet...");
    vmUpdateRequested = true;
    request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Update started\"}");
  });

  // Update-Fortschritt/Status abfragen (UI pollt diese Route).
  server.on("/api/voltmeter/update/status", HTTP_GET, [](AsyncWebServerRequest *request){
    const char* st = vmUpdateState == VMU_RUNNING ? "running"
                   : vmUpdateState == VMU_SUCCESS ? "success"
                   : vmUpdateState == VMU_ERROR   ? "error" : "idle";
    StaticJsonDocument<192> doc;
    doc["state"]    = st;
    doc["progress"] = vmUpdateProgress;
    doc["message"]  = vmUpdateMessage;
    String out; serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  // Version der auf LittleFS liegenden .bin (aus dem Magic-Tag). (#33)
  server.on("/api/voltmeter/update/fileversion", HTTP_GET, [](AsyncWebServerRequest *request){
    StaticJsonDocument<128> doc;
    char ver[48];
    if (LittleFS.exists(VM_FW_PATH) && readVmFwFileVersion(ver, sizeof(ver))) {
      doc["status"]  = "success";
      doc["version"] = ver;
    } else {
      doc["status"] = "none"; // keine Datei oder kein Tag
    }
    String out; serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  // API-Route zum Speichern der aktuellen oder übergebenen Spannung auf einem Preset
  server.on("/api/presets/save", HTTP_GET, [](AsyncWebServerRequest *request) {
    // 1. Zuerst prüfen, ob die Hardware überhaupt bereit ist
    if (!hardwareInitialized) {
        request->send(503, "application/json", "{\"status\":\"error\",\"message\":\"Hardware not ready\"}");
        return;
    }

    // 2. Prüfen, ob der 'preset'-Parameter vorhanden ist
    if (request->hasParam("preset")) {
        int presetNum = request->getParam("preset")->value().toInt();
        float voltageToSave;

        // Prüfe, ob eine Spannung explizit mitgeliefert wurde
        if (request->hasParam("voltage")) {
            voltageToSave = request->getParam("voltage")->value().toFloat();
        } else if (isVoltageDataFresh()) {
            voltageToSave = received_rms_value; // Fallback auf aktuellen Messwert
        } else {
            request->send(503, "application/json", "{\"status\":\"error\",\"message\":\"No fresh voltmeter data\"}");
            return;
        }

        int v = constrain((int)round(voltageToSave), MIN_VOLTAGE_TARGET, maxVoltageTarget());
        Action* targetAction = nullptr;

        switch(presetNum) {
            case 1: targetAction = A_p1; break;
            case 2: targetAction = A_p2; break;
            case 3: targetAction = A_p3; break;
            default:
                // Fehler: Ungültige Preset-Nummer
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid preset number. Use 1, 2, or 3.\"}");
                return;
        }
        // Werte aktualisieren und speichern
        targetAction->setValuePreset(v);
        saveConfiguration();
        logMessage(LOG_INFO, "API: Preset %d stored -> %d V", presetNum, v);
        // Erfolgs-Antwort im JSON-Format
        request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Preset saved\"}");
    } else {
        // Fehler: Fehlender 'preset'-Parameter
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing 'preset' parameter\"}");
    }
  });

  // API-Route zum Abrufen der gespeicherten Preset-Werte
  server.on("/api/presets", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!hardwareInitialized) {
      request->send(503, "application/json", "{\"error\":\"Hardware not ready\"}");
      return;
    }
    
    StaticJsonDocument<128> doc;
    doc["p1"] = A_p1->getValuePreset();
    doc["p2"] = A_p2->getValuePreset();
    doc["p3"] = A_p3->getValuePreset();
    
    String jsonString;
    serializeJson(doc, jsonString);
    request->send(200, "application/json", jsonString);
  });

  // API-Route zum Speichern der Kalibrierungswerte
  server.on("/api/calibration/save", HTTP_GET, [](AsyncWebServerRequest *request) {
    // 1. Zuerst prüfen, ob die Hardware überhaupt bereit ist
    if (!hardwareInitialized) {
        request->send(503, "application/json", "{\"status\":\"error\",\"message\":\"Hardware not ready\"}");
        return;
    }

    // 2. Prüfen, ob der 'limit'-Parameter vorhanden ist
    if (request->hasParam("limit")) {
        String limitType = request->getParam("limit")->value();
        int valueToSave;

        // Prüfe, ob ein Wert explizit mitgegeben wurde
        if (request->hasParam("value")) {
            valueToSave = request->getParam("value")->value().toInt();
        } else {
            valueToSave = stepper.currentPosition();
        }

        if (limitType == "min") {
            portENTER_CRITICAL(&calibMux);
            minWhiperPos = valueToSave;
            portEXIT_CRITICAL(&calibMux);
            logMessage(LOG_WARN, "API: NEW MIN LIMIT calibrated -> %d steps", valueToSave);
            saveConfiguration();
        }
        else if (limitType == "max") {
            portENTER_CRITICAL(&calibMux);
            maxWhiperPos = valueToSave;
            portEXIT_CRITICAL(&calibMux);
            logMessage(LOG_WARN, "API: NEW MAX LIMIT calibrated -> %d steps", valueToSave);
            saveConfiguration();
        }
        else {
            // Fehler: Ungültiger limit-Typ
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid limit type. Use 'min' or 'max'.\"}");
            return;
        }
		
        // Erfolgs-Antwort im JSON-Format
        request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Calibration point saved\"}");

    } else {
        // Fehler: Fehlender 'limit'-Parameter
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing 'limit' parameter\"}");
    }
  });

  // API-Route zum Auslesen der Kalibrierungswerte
  server.on("/api/calibration", HTTP_GET, [](AsyncWebServerRequest *request){
    StaticJsonDocument<128> doc;
    doc["min_pos"] = minWhiperPos;
    doc["max_pos"] = maxWhiperPos;
    
    String jsonString;
    serializeJson(doc, jsonString);
    request->send(200, "application/json", jsonString);
  });

  // API-Route für einen Neustart des Geräts
  server.on("/api/reboot", HTTP_GET, [](AsyncWebServerRequest *request){
    // Sende die Bestätigung an den Client.
    request->send(200, "text/plain", "Rebooting in 200ms...");
    
    logMessage(LOG_INFO, "API: Reboot requested!");

    // Verabschiede alle WebSocket-Clients sauber
    ws.closeAll();

    // Starte einen einmaligen Timer, der den Neustart nach 200ms auslöst.
    // Die Funktion kehrt sofort zurück, damit die HTTP-Antwort in der Zwischenzeit gesendet werden kann.
    rebootTicker.once_ms(200, [](){ ESP.restart(); });
  });

  // API-Route zum Abrufen der Log-Dateien
  server.on("/api/log", HTTP_GET, [](AsyncWebServerRequest *request){
    String log_filename = LOG_FILE; // z.B. "/system.log"
    String download_filename = "system.log";

    if (request->hasParam("old")) {
        log_filename = "/system.log.old";
        download_filename = "system.log.old";
    }

    if (LittleFS.exists(log_filename)) {
      
      // Prüfe, ob der Download-Parameter gesetzt ist
      if (request->hasParam("download")) {
        // Sende die Datei als Anhang (löst den Download im Browser aus)
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, log_filename, "text/plain");
        response->addHeader("Content-Disposition", "attachment; filename=" + download_filename);
        request->send(response);
      } else {
        // Sende die Datei normal zur Anzeige im Browser
        request->send(LittleFS, log_filename, "text/plain");
      }

    } else {
      // Fehler: Datei nicht gefunden, sende JSON-Antwort
      request->send(404, "application/json", "{\"status\":\"error\",\"message\":\"Log file not found\"}");
    }
  });

  // API-Route zum Löschen einer Datei
  server.on("/api/files/delete", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("filename")) {
      String filename = request->getParam("filename")->value();
      
      // Kleiner Security-Check: Erlaube nur das Löschen von Dateien im Root-Verzeichnis
      if (!filename.startsWith("/") || filename.indexOf('/', 1) != -1) {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid filename\"}");
        return;
      }
      
      if (LittleFS.exists(filename)) {
        if (LittleFS.remove(filename)) {
          logMessage(LOG_WARN, "API: File deleted -> %s", filename.c_str());
          request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"File deleted\"}");
        } else {
          request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to delete file\"}");
        }
      } else {
        request->send(404, "application/json", "{\"status\":\"error\",\"message\":\"File not found\"}");
      }
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing 'filename' parameter\"}");
    }
  });

  // API-Route, um alle Dateien im LittleFS aufzulisten
  server.on("/api/files", HTTP_GET, [](AsyncWebServerRequest *request){
    // Ein JSON-Dokument erstellen. 1024 Bytes sollte für ca. 20-25 Dateien reichen.
    StaticJsonDocument<1024> doc;
    JsonArray files = doc.to<JsonArray>();

    File root = LittleFS.open("/");
    File file = root.openNextFile();

    while(file){
      if (!file.isDirectory()) {
        JsonObject fileObj = files.createNestedObject();
        fileObj["name"] = String(file.name());
        fileObj["size"] = file.size();
      }
      file = root.openNextFile();
    }

    String jsonString;
    serializeJson(doc, jsonString);
    request->send(200, "application/json", jsonString);
  });

  // API-Route, zum Auslesen der Konfigurationsdatei inkl. Download Option
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request){
    if (LittleFS.exists(CONFIG_FILE)) {
      
      // Prüfe, ob der Download-Parameter gesetzt ist
      if (request->hasParam("download")) {
        // JA: Sende die Datei als Anhang (löst den Download im Browser aus)
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, CONFIG_FILE, "application/json");
        response->addHeader("Content-Disposition", "attachment; filename=\"config.json\"");
        request->send(response);
      } else {
        // NEIN: Sende die Datei normal zur Anzeige im Browser
        request->send(LittleFS, CONFIG_FILE, "application/json");
      }
      
    } else {
      request->send(404, "application/json", "{\"status\":\"error\",\"message\":\"Config file not found\"}");
    }
  });

  // Handler für das Schreiben/Aktualisieren der Konfiguration
  AsyncCallbackJsonWebHandler* handler = new AsyncCallbackJsonWebHandler("/api/config", [](AsyncWebServerRequest *request, JsonVariant &json) {
    JsonObject doc;
    // Prüfe, ob der Body valides JSON ist
    if (json.is<JsonObject>()) {
      doc = json.as<JsonObject>();
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON body\"}");
      return;
    }
    
    // Rufe die zentrale Validierungs-Funktion auf
    String validationErrorJson = applyAndValidateConfig(doc);

    if (validationErrorJson.isEmpty()) {
      // Erfolg: Speichere die neuen, validierten Werte und sende Erfolgsmeldung
      saveConfiguration();
      logMessage(LOG_INFO, "Configuration updated via API.");
      request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Configuration updated and saved\"}");
    } else {
      // Fehler: Sende eine 400 Bad Request Antwort mit dem detaillierten Fehler-JSON
      String response = "{\"status\":\"error\",\"validation_errors\":" + validationErrorJson + "}";
      request->send(400, "application/json", response);
    }
  });
  server.addHandler(handler);
 
  // Handler für nicht gefundene Seiten (404)
  server.onNotFound([](AsyncWebServerRequest *request){
    // Prüfe, ob die Anfrage an die API gerichtet war
    if (request->url().startsWith("/api/")) {
      // Wenn ja, antworte mit JSON
      request->send(404, "application/json", "{\"status\":\"error\",\"message\":\"Endpoint not found\"}");
    } else {
      // Wenn nein (z.B. eine fehlende .css Datei), antworte mit einfachem Text
      request->send(404, "text/plain", "Not found");
    }
  });

  // Event-Handler für den WebSocket
  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len){
      if (type == WS_EVT_CONNECT) {
          // Ein neuer Client (Browser) hat sich verbunden!
          // Schicke ihm sofort die gesamte gespeicherte Historie aus dem RAM.
          // Snapshot unter Mutex — der Logger-Task (#4) verändert logHistory parallel.
          String snapshot;
          if (xSemaphoreTake(logHistoryMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            snapshot = logHistory;
            xSemaphoreGive(logHistoryMutex);
          }
          client->text(snapshot);
      }
  });

  // WebSocket an den Server binden
  server.addHandler(&ws);

  // Starte den Server
  server.begin();
  logMessage(LOG_INFO,"Web server started.");
}


/**
 * @brief Schreibt eine Log-Meldung auf Serial und in ein Log-File auf LittleFS.
 * Verwendet printf-ähnliche Formatierung.
 * @param level Das Log-Level (LOG_INFO, LOG_WARN, LOG_ERROR).
 * @param format Der Format-String.
 * @param ... Die variablen Argumente für den Format-String.
 */
void logMessage(LogLevel level, const char* format, ...) {
    LogEntry entry;
    entry.level = level;

    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    snprintf(entry.msg, sizeof(entry.msg), "[%lu][%s] %s\n", millis(), logLevelStrings[level], buf);

    // Nur formatieren + einreihen; die Verarbeitung (Serial/Historie/WS/Flash) macht
    // ausschliesslich der Logger-Task (#4). Kein Warten: volle Queue -> Meldung verwerfen.
    if (logQueue != NULL) {
      if (xQueueSend(logQueue, &entry, 0) != pdTRUE) {
        logDroppedCount++;
      }
    } else {
      // Fallback ganz früh im Boot (bevor der Logger-Task existiert): nur Serial.
      if (debugEnabled) Serial.print(entry.msg);
    }
}

/**
 * @brief Verarbeitet einen Log-Eintrag: Serial, RAM-Historie, WebSocket, Flash (nur WARN+).
 * Läuft NUR im Logger-Task (#4) — dadurch sind String/LittleFS/ws-Zugriffe serialisiert.
 */
void processLogEntry(const LogEntry& entry) {
    // Gib die Meldung auf Serial aus, wenn Debugging aktiviert ist
    if (debugEnabled) {
      Serial.print(entry.msg);
    }

    // RAM-Historie unter Mutex (der WS-Connect-Handler liest sie aus einem anderen Task)
    if (xSemaphoreTake(logHistoryMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      logHistory += entry.msg;
      // Wenn der Puffer zu gross wird, die ältere Hälfte abschneiden
      if (logHistory.length() > MAX_LOG_HISTORY) {
          logHistory = logHistory.substring(logHistory.length() - 2048);
          // Die erste, unvollständige Zeile nach dem Abschneiden bereinigen
          int firstNewLine = logHistory.indexOf('\n');
          if (firstNewLine != -1) {
              logHistory = logHistory.substring(firstNewLine + 1);
          }
      }
      xSemaphoreGive(logHistoryMutex);
    }

    // Live-Log an alle offenen Browser-Fenster senden!
    ws.textAll(entry.msg);

    // Schreibe nur Warnungen und Fehler ins Log-File, um den Flash zu schonen
    if (entry.level >= LOG_WARN) {
      // Öffne die Datei im "Append"-Modus.
      // Der Modus "FILE_APPEND" erstellt die Datei automatisch, falls sie nicht existiert.
      File logFile = LittleFS.open(LOG_FILE, FILE_APPEND);

      if (logFile) {
        logFile.print(entry.msg);

        // Log-Rotation: Wenn die Datei zu gross wird, alte löschen und neue anfangen
        if (logFile.size() > MAX_LOG_SIZE) {
          logFile.close();
          // Lösche zuerst das alte Backup, falls es existiert
          if (LittleFS.exists("/system.log.old")) {
            LittleFS.remove("/system.log.old");
          }
          // Benenne die aktuelle Log-Datei in .old um
          LittleFS.rename(LOG_FILE, "/system.log.old");
        } else {
          logFile.close();
        }
      } else {
        // Dieser Fall sollte selten auftreten, ist aber eine gute Absicherung
        if (debugEnabled) {
          Serial.println("Failed to open log file for writing.");
        }
      }
    }
}

/**
 * @brief FreeRTOS Task: einziger Konsument der Log-Queue (#4).
 * Meldet zusätzlich, wenn Einträge wegen voller Queue verworfen wurden.
 */
void loggerTask(void *parameter) {
  LogEntry entry;
  for (;;) {
    if (xQueueReceive(logQueue, &entry, portMAX_DELAY) == pdTRUE) {
      processLogEntry(entry);

      // Verworfene Meldungen nachmelden (Zähler ist nur ungefähr — bewusst einfach gehalten)
      uint32_t dropped = logDroppedCount;
      if (dropped > 0) {
        logDroppedCount = 0;
        LogEntry note;
        note.level = LOG_WARN;
        snprintf(note.msg, sizeof(note.msg), "[%lu][WARN] LOGGER: %lu Meldung(en) verworfen (Queue voll)\n",
                 millis(), (unsigned long)dropped);
        processLogEntry(note);
      }
    }
  }
}

// ********************************************************************************
// Display functions
// ********************************************************************************
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
 * @brief Aktualisiert die Werte auf dem Display im normalen Betriebsmodus.
 */
void updateDisplay() {
  tftStartWrite(); // << SPERREN
  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(100);

	if (whiperTemp != actDispValues.temp){
		actDispValues.temp = whiperTemp;
		tft.setTextColor(TFT_WHITE, TFT_NAVY);
        tft.setTextDatum(TR_DATUM);
        if (tempSensorAvailable) {
		    tft.drawString((String)actDispValues.temp+"C", 230, 2, 2);
        }
        else {
		    tft.drawString("N/A", 230, 2, 2);
        }
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
  if (minWhiperPos != actDispValues.setup1) {
      actDispValues.setup1 = minWhiperPos;
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString((String)actDispValues.setup1, 220, 130, 4);
  }
  if (A_p2->getValuePreset() != actDispValues.preset2) {
      actDispValues.preset2 = A_p2->getValuePreset();
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString((String)actDispValues.preset2, 220, 160, 4);
  }
  if (maxWhiperPos != actDispValues.setup2) {
      actDispValues.setup2 = maxWhiperPos;
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString((String)actDispValues.setup2, 220, 190, 4);
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
  tft.drawString(WiFi.localIP().toString().c_str(), 120, 180, 2);

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
	
	tft.setTextDatum(TL_DATUM);
	tft.setTextColor(TFT_WHITE, TFT_NAVY);
	tft.drawString("ISOLATION VARIAC", 10, 2, 2);
  tftEndWrite();   // << FREIGEBEN
}

/**
 * @brief Zeichnet die statischen Beschriftungen der Hauptanzeige.
 */
void drawLegend() {
  tftStartWrite(); // << SPERREN
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
}

// ********************************************************************************
// Action callback functions
// ********************************************************************************
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
        // Hole den gespeicherten Wert aus dem Action-Objekt und fahre dorthin
        int targetPosition = act->getValuePreset();
        setWhiperAbsolut(targetPosition);
        logMessage(LOG_INFO,"Moving to preset position: %d", targetPosition);
    }
    else if (event == ButtonEvent::LONGPRESSED) {
        int v = constrain((int)stepper.currentPosition(), MINWHIPERLIMIT, MAXWHIPERLIMIT);
        act->setValuePreset(v);
        saveConfiguration();
        logMessage(LOG_INFO,"Stored new setting %d to config.json and Action object", v);
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
 * @brief Callback für die Homing-Taste im Einstellungsmodus.
 * @param act Zeiger auf das auslösende Action-Objekt.
 * @param event Der Typ des Tasten-Events.
 */
void cb_SettingsHomingAction(Action* act, ButtonEvent event) {
    if (event == ButtonEvent::PRESSED) {
        act->on();
    }
}

// ********************************************************************************
// ISR Functions
// ********************************************************************************
/**
 * @brief Extrem kurze ISR, die nur ein Semaphore an den stepperTask gibt.
 * Dies geschieht alle 100 Mikrosekunden.
 */
void ICACHE_RAM_ATTR isr_stepper() {
  // Wecke den stepperTask auf. Wichtig: FromISR-Version verwenden!
  xSemaphoreGiveFromISR(stepperSemaphore, NULL);
}


// ********************************************************************************
// RTOS Task Functions
// ********************************************************************************
// Task-Handle für eventuelle spätere Steuerung (optional)
TaskHandle_t h_userInputTask;
TaskHandle_t h_motorControlTask;
TaskHandle_t h_displayUpdateTask;
TaskHandle_t h_sensorAndFanTask;
TaskHandle_t h_communicationTask;
TaskHandle_t h_stepperTask;
TaskHandle_t h_voltmeterUpdateTask;
TaskHandle_t h_loggerTask;

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
      currentMode = MODE_SETTINGS;
      initSettingsActions(); 
      clearScreen();
      drawSettingsScreen();
      cb_SettingsValueAction(A_p1, ButtonEvent::RELEASED);
    }

    if (currentMode == MODE_NORMAL) {
      // 1. Alle Actions behandeln
      handleAllActions();

      // 2. Encoder auslesen
      int newEncPos = getEncoderCount();
      if (lastEncPos != newEncPos) {
        is_regulation_active = false; // Manuelle Steuerung hat Vorrang
        int dPos = newEncPos - lastEncPos;
        setWhiperRelativ(dPos * encSpeed);
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

          int pos = whiperPos + dPos * encSpeed;
          pos = constrain(pos, MINWHIPERLIMIT, MAXWHIPERLIMIT);

          if (A_p1->getState()) {
              pos = constrain(pos, MINWHIPERLIMIT, 0);
              portENTER_CRITICAL(&calibMux);
              minWhiperPos = pos;
              if (fresh) minVoltageAtMinPos = rms;
              portEXIT_CRITICAL(&calibMux);
          }
          if (A_p2->getState()) {
              pos = constrain(pos, minWhiperPos + 1, MAXWHIPERLIMIT);
              portENTER_CRITICAL(&calibMux);
              maxWhiperPos = pos;
              if (fresh) maxVoltageAtMaxPos = rms;
              portEXIT_CRITICAL(&calibMux);
          }

          // whiperPos-Update + moveTo atomar (#5); im Settings-Modus ist dieser Task
          // der einzige Beweger, aber run() im Stepper-Task läuft parallel.
          portENTER_CRITICAL(&stepperMux);
          whiperPos = pos;
          stepper.moveTo(whiperPos);
          portEXIT_CRITICAL(&stepperMux);
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(20)); // Kurze Pause, ca. 50 mal pro Sekunde
  }
}

/**
 * @brief FreeRTOS Task zur Handhabung der Motorsteuerungslogik.
 * Beinhaltet die Preset-Anfahrlogik und die PID-Spannungsregelung.
 * @param parameter Standard FreeRTOS Task-Parameter (hier ungenutzt).
 */
void motorControlTask(void *parameter) {
  for (;;) {

    if (!hardwareInitialized) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue; // Schleife überspringen, wenn Hardware fehlt
    }

    // Neuer Sollwert (Preset / API / REG-ein): Vorsteuerung auf die geschätzte Zielposition
    static uint32_t settleStart = 0;
    static uint32_t driftStart = 0;
    static int      correctionCount = 0;

    if (isRecallPreset) {
      isRecallPreset = false;
      int targetPos = estimatePositionForVoltage(setpoint_voltage);

      // Vorsteuerung bewusst kurz vor dem Ziel stoppen (in Fahrtrichtung), damit die
      // anschließende gedämpfte Korrektur den Sollwert von EINER Seite anfährt -> kein
      // Überschießen. Bei kleinen Fahrten wird die Richtung nie umgekehrt (clamp auf curPos).
      float gain = voltsPerStep();
      int margin = (gain > 0.0f) ? (int)(REG_FEEDFORWARD_UNDERSHOOT_V / gain) : 0;
      int curPos = stepper.currentPosition();
      if (targetPos > curPos)      targetPos = max(curPos, targetPos - margin); // hoch -> tiefer stoppen
      else if (targetPos < curPos) targetPos = min(curPos, targetPos + margin); // runter -> höher stoppen

      setWhiperAbsolut(targetPos);
      is_regulation_active = true;
      regPhase = RP_FEEDFORWARD;
      settleStart = 0;
      correctionCount = 0;
      logMessage(LOG_INFO, "MOTOR: Feedforward -> %d steps (Soll %.1f V, Marge %d Schritte)", targetPos, setpoint_voltage, margin);
    }

    if (is_regulation_active) {
      float gain = voltsPerStep();

      switch (regPhase) {

        // Anfahrt/Korrektur: warten bis Stepper steht + frischer Messwert, dann messen & korrigieren
        case RP_FEEDFORWARD:
        case RP_CORRECT: {
          if (stepper.distanceToGo() != 0) { settleStart = 0; break; }
          if (settleStart == 0) settleStart = millis();
          if (millis() - settleStart < REG_SETTLE_MS || !isVoltageDataFresh()) break;

          float error = setpoint_voltage - received_rms_value;
          if (fabs(error) <= REG_DEADBAND_V || correctionCount >= REG_MAX_CORRECTIONS || gain <= 0.0f) {
            logMessage(LOG_INFO, "MOTOR: Target reached (Ist %.1f V, Soll %.1f V, %d Korrektur(en))",
                       received_rms_value, setpoint_voltage, correctionCount);
            if (A_reg->getState()) {
              regPhase = RP_HOLD;
              driftStart = 0;
            } else {
              is_regulation_active = false;   // REG aus -> One-shot, anhalten
              regPhase = RP_IDLE;
            }
          } else {
            int steps = constrain((int)(error / gain * REG_CORRECTION_DAMPING),
                                  -REG_MAX_CORRECTION_STEPS, REG_MAX_CORRECTION_STEPS);
            setWhiperRelativ(steps);
            correctionCount++;
            regPhase = RP_CORRECT;
            settleStart = 0;
          }
          break;
        }

        // Halten: nur bei REG ein; korrigiert erst nach anhaltender Abweichung (Drift)
        case RP_HOLD: {
          if (!A_reg->getState()) { regPhase = RP_IDLE; break; }
          if (!isVoltageDataFresh()) { driftStart = 0; break; }

          float error = setpoint_voltage - received_rms_value;
          if (fabs(error) > REG_DEADBAND_V && gain > 0.0f) {
            if (driftStart == 0) {
              driftStart = millis();
            } else if (millis() - driftStart >= REG_DRIFT_PERSIST_MS) {
              int steps = constrain((int)(error / gain * REG_CORRECTION_DAMPING),
                                    -REG_MAX_CORRECTION_STEPS, REG_MAX_CORRECTION_STEPS);
              setWhiperRelativ(steps);
              logMessage(LOG_INFO, "MOTOR: Drift correction %d steps (Ist %.1f V, Soll %.1f V)",
                         steps, received_rms_value, setpoint_voltage);
              correctionCount = 0;
              regPhase = RP_CORRECT;   // nach Korrektur neu settlen
              settleStart = 0;
              driftStart = 0;
            }
          } else {
            driftStart = 0;
          }
          break;
        }

        // Leerlauf: wird REG eingeschaltet, aktuellen Sollwert halten
        case RP_IDLE:
        default:
          if (A_reg->getState()) { regPhase = RP_HOLD; driftStart = 0; }
          break;
      }
    } else {
      regPhase = RP_IDLE;   // manuelle Bedienung -> keine automatische Bewegung
    }

    vTaskDelay(pdMS_TO_TICKS(REGULATION_LOOP_PERIOD)); 
  }
}

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

    if (currentMode == MODE_NORMAL) {
        updateDisplay();
    } else { // MODE_SETTINGS
        updateSettingsDisplay();
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // Display 10x pro Sekunde aktualisieren
  }
}

/**
 * @brief FreeRTOS Task zur periodischen Messung der Temperatur und Steuerung des Lüfters.
 * @param parameter Standard FreeRTOS Task-Parameter (hier ungenutzt).
 */
void sensorAndFanTask(void *parameter) {
  static bool temp_warning_active = false;
  for (;;) {
    if (tempSensorAvailable) {
      whiperTemp = getTemperature();
      updateFan(whiperTemp);
      if (whiperTemp >= MAXFANTEMP && !temp_warning_active) {
          logMessage(LOG_ERROR, "SYSTEM: ALARM - Temperature critical (%.1f C)! Fan at 100%%.", whiperTemp);
          temp_warning_active = true;
      } 
      // Wieder abgekühlt? (Hysterese von 5 Grad)
      else if (whiperTemp < (MAXFANTEMP - 5.0) && temp_warning_active) {
          logMessage(LOG_INFO, "SYSTEM: CLEARED - Temperature back to normal (%.1f C).", whiperTemp);
          temp_warning_active = false;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); // Einmal pro Sekunde messen
  }
}

#ifdef SIM
// ---- Simulationsmodus: Voltmeter durch ein Streckenmodell ersetzen ----
// Läuft auf dem echten Controller-Board (MCP/TFT/Encoder real), nur die
// "gemessene" Spannung wird aus der Stepper-Position berechnet.
#define SIM_PLANT_GAIN    1.03f   // Plant weicht bewusst leicht von der Kalibrierung ab
#define SIM_PLANT_OFFSET  -2.0f   // ... damit die Vorsteuerung nicht "perfekt" trifft
#define SIM_LAG_ALPHA     0.25f   // First-Order-Lag pro 40-ms-Schritt (~150 ms Zeitkonstante)
#define SIM_NOISE_MV      300     // Mess-Rauschen: +/- in Millivolt

/**
 * @brief Berechnet im Simulationsmodus die "gemessene" RMS-Spannung aus der
 * aktuellen Stepper-Position (lineares Modell + Lag + Abweichung + Rauschen).
 */
void simUpdateMeasuredVoltage() {
  static float sim_voltage = -1.0f;

  int pos = stepper.currentPosition();

  // Ideale Spannung gemäss Kalibrierung (linear), auf den physikalischen Bereich begrenzt
  float idealV = minVoltageAtMinPos;
  if (maxWhiperPos != minWhiperPos) {
    idealV = minVoltageAtMinPos + (float)(pos - minWhiperPos) *
             (maxVoltageAtMaxPos - minVoltageAtMinPos) / (float)(maxWhiperPos - minWhiperPos);
  }
  idealV = constrain(idealV, minVoltageAtMinPos, maxVoltageAtMaxPos);

  // Plant weicht leicht ab (Gain/Offset)
  float plantV = idealV * SIM_PLANT_GAIN + SIM_PLANT_OFFSET;

  if (sim_voltage < 0.0f) sim_voltage = plantV;            // Initialisierung
  sim_voltage += (plantV - sim_voltage) * SIM_LAG_ALPHA;   // First-Order-Lag

  float noise = (float)random(-SIM_NOISE_MV, SIM_NOISE_MV + 1) / 1000.0f;
  received_rms_value = sim_voltage + noise;
}
#endif

/**
 * @brief FreeRTOS Task zur Verarbeitung der seriellen Kommunikation.
 * Parst eingehende Daten und aktualisiert den Systemzustand entsprechend.
 * @param parameter Standard FreeRTOS Task-Parameter (hier ungenutzt).
 */
void communicationTask(void *parameter) {
  for (;;) {
#ifdef SIM
    // Simulationsmodus: alle 40 ms (wie das echte Voltmeter) einen neuen Wert erzeugen
    static uint32_t lastSimUpdate = 0;
    if (millis() - lastSimUpdate >= 40) {
      lastSimUpdate = millis();
      simUpdateMeasuredVoltage();
      new_value_available = true;
    }
#else
    while (Serial1.available() > 0) {
      byte newByte = Serial1.read();
      parseByte(newByte);
    }
#endif

    if (new_value_available) {
      new_value_available = false; // Flag zurücksetzen
      last_rms_received_time = millis(); // Datenfrische markieren (gilt für Real- und SIM-Pfad)
      static bool was_manual = false; // wird aktuell manuell am Encoder gedreht

      // Prüfen, ob der Benutzer gerade den Encoder bedient oder kurz zuvor bedient hat
      bool user_is_adjusting = (millis() - last_encoder_change_time < 1000);

      if (user_is_adjusting) {
        // PHASE 1 & 2: Manuelle Steuerung & Beruhigung

        // Regelung ist und bleibt aus
        is_regulation_active = false;

        // Solange der Stepper noch in Bewegung ist, bleibt manuell aktiv
        if (stepper.isRunning()) {
          last_encoder_change_time  = millis();
        }
        
        // SYNCHRONISIERUNG: Der Sollwert folgt dem realen Istwert.
        setpoint_voltage = received_rms_value;

        if (!was_manual) {
            logMessage(LOG_INFO, "MOTOR: Mode MANUAL (Encoder active)");
            was_manual = true;
        }      
      } else {
        // Der Benutzer hat den Encoder seit mehr als 1 Sekunde (1000 ms) nicht mehr berührt und der Stepper hat sein Ziel erreicht.
        
        // Wenn die Regelung noch nicht aktiv war, ist dies der Moment, sie zu starten.
        if (!is_regulation_active && last_encoder_change_time != 0) {
          
          // PHASE 3: Automatik-Regelung wird aktiviert
          is_regulation_active = true;
          
          // Zeitstempel zurücksetzen, damit dieser Block nicht erneut ausgeführt wird.
          last_encoder_change_time = 0; 
          
          logMessage(LOG_INFO, "MOTOR: Mode AUTOMATIC (Setpoint fixed at %.1f V)", setpoint_voltage);
          was_manual = false;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10)); // Sehr oft prüfen
  }
}

// ********************************************************************************
// Voltmeter-FW-Update über den ROM-UART-Bootloader (AN3155), #30
// ********************************************************************************
// Während dieser Sequenz ist der communicationTask suspendiert und Serial1 läuft
// vorübergehend auf 8E1 (der STM32-Bootloader nutzt gerade Parität). Wir lesen die
// Bootloader-Bytes hier roh (ohne den Frame-Parser). Recovery bei Fehlflash: ST-Link.

// Liest ein einzelnes Byte direkt von Serial1 mit Timeout. -1 = Timeout.
static int blReadByte(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (Serial1.available() == 0) {
    if (millis() - t0 >= timeoutMs) return -1;
    delay(1); // yieldet
  }
  return Serial1.read();
}

// Wartet auf ein ACK (0x79) vom Bootloader.
static bool blWaitAck(uint32_t timeoutMs) {
  return blReadByte(timeoutMs) == BL_ACK;
}

// Sendet Befehlsbyte + Komplement und wartet auf ACK.
static bool blCmd(uint8_t cmd, uint32_t timeoutMs) {
  Serial1.write(cmd);
  Serial1.write((uint8_t)(cmd ^ 0xFF));
  Serial1.flush();
  return blWaitAck(timeoutMs);
}

// Sendet eine 4-Byte-Adresse (MSB first) + XOR-Checksumme und wartet auf ACK.
static bool blAddr(uint32_t addr, uint32_t timeoutMs) {
  uint8_t a[4] = { (uint8_t)(addr >> 24), (uint8_t)(addr >> 16),
                   (uint8_t)(addr >> 8),  (uint8_t)addr };
  Serial1.write(a, 4);
  Serial1.write((uint8_t)(a[0] ^ a[1] ^ a[2] ^ a[3]));
  Serial1.flush();
  return blWaitAck(timeoutMs);
}

// Setzt Status + Meldung (Single-Writer: Update-Task).
static void vmUpdSet(VmUpdateState st, int prog, const char* msg) {
  vmUpdateProgress = prog;
  strncpy(vmUpdateMessage, msg, sizeof(vmUpdateMessage) - 1);
  vmUpdateMessage[sizeof(vmUpdateMessage) - 1] = '\0';
  vmUpdateState = st;
}

// Liest die FW-Version aus der hochgeladenen .bin: scannt nach dem Magic-Tag "@@VMFW@@"
// und kopiert den FW-String dahinter (bis NUL/nicht-druckbar). false = nicht gefunden. (#33)
static bool readVmFwFileVersion(char* out, size_t outSize) {
  if (outSize == 0) return false;
  out[0] = '\0';
  File f = LittleFS.open(VM_FW_PATH, "r");
  if (!f) return false;
  static const char MAGIC[] = "@@VMFW@@";
  const size_t MAGLEN = 8;
  size_t match = 0;
  bool found = false;
  while (f.available()) {
    int c = f.read();
    if (c < 0) break;
    if ((char)c == MAGIC[match]) {
      if (++match == MAGLEN) { found = true; break; }
    } else {
      match = ((char)c == MAGIC[0]) ? 1 : 0;
    }
  }
  if (found) {
    size_t i = 0;
    while (f.available() && i < outSize - 1) {
      int c = f.read();
      if (c < 0x20 || c > 0x7E) break; // NUL/0xFF/nicht-druckbar -> Ende
      out[i++] = (char)c;
    }
    out[i] = '\0';
  }
  f.close();
  return found && out[0] != '\0';
}

// Plausibilisiert die .bin (Größe + Vektortabelle: MSP im RAM, Reset-Vektor im Flash).
static bool vmFwValidate(File& f, size_t& sizeOut) {
  size_t sz = f.size();
  if (sz < 0x100 || sz > VM_FW_MAX_SIZE) return false;
  uint8_t hdr[8];
  f.seek(0);
  if (f.read(hdr, 8) != 8) return false;
  uint32_t msp   = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
  uint32_t reset = hdr[4] | (hdr[5] << 8) | (hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
  // F103CB: 20 KB RAM ab 0x20000000, 128 KB Flash ab 0x08000000.
  if (msp < 0x20000000UL || msp > 0x20005000UL) return false;
  if (reset < VM_FLASH_BASE || reset >= (VM_FLASH_BASE + 0x20000UL)) return false;
  sizeOut = sz;
  return true;
}

// Führt das komplette Flashen aus. Setzt vmUpdateState/-Progress/-Message.
static void runVoltmeterFlash() {
  vmUpdSet(VMU_RUNNING, 0, "Pruefe Firmware-Datei...");

  // 1) Sicherheit: Ausgang aus, Regelung stoppen.
  is_regulation_active = false;
  if (A_onoff) A_onoff->off();

  // 2) Datei öffnen + plausibilisieren.
  File f = LittleFS.open(VM_FW_PATH, "r");
  if (!f) { vmUpdSet(VMU_ERROR, 0, "Firmware-Datei nicht gefunden."); return; }
  size_t fwSize = 0;
  if (!vmFwValidate(f, fwSize)) {
    f.close();
    vmUpdSet(VMU_ERROR, 0, "Ungueltige .bin (Groesse/Vektortabelle).");
    return;
  }

  // 3) Voltmeter in den Bootloader schicken (normaler Frame, communicationTask parst ACK).
  //    Diagnose-Modus (skipEnter): VM wurde bereits per BOOT0+Reset in den ROM-Loader gebracht.
  if (!vmUpdateSkipEnter) {
    vmUpdSet(VMU_RUNNING, 2, "Voltmeter -> Bootloader...");
    if (!voltmeterRequest(VM_CMD_ENTER_BOOTLOADER, nullptr, 0, 800)) {
      f.close();
      vmUpdSet(VMU_ERROR, 0, "Voltmeter antwortet nicht (ENTER_BOOTLOADER).");
      return;
    }
  } else {
    vmUpdSet(VMU_RUNNING, 2, "Diagnose: ENTER_BOOTLOADER uebersprungen (BOOT0).");
  }

  // 4) Ab hier gehört Serial1 uns: communicationTask suspendieren, auf 8E1 umstellen.
  vTaskSuspend(h_communicationTask);
  delay(150); // dem Voltmeter Zeit für den Sprung ins System-Memory lassen
  Serial1.end();
  Serial1.begin(115200, SERIAL_8E1, PIN_RX, PIN_TX);
  while (Serial1.available()) Serial1.read(); // RX-Reste verwerfen

  bool ok = false;
  do {
    // 5) Auto-Baud / Handshake: 0x7F -> ACK (einige Versuche).
    vmUpdSet(VMU_RUNNING, 5, "Bootloader-Handshake...");
    bool synced = false;
    for (int i = 0; i < 5 && !synced; i++) {
      Serial1.write((uint8_t)BL_INIT);
      Serial1.flush();
      int r = blReadByte(500);
      if (r == BL_ACK || r == BL_NACK) synced = true; // NACK = schon initialisiert
    }
    if (!synced) { vmUpdSet(VMU_ERROR, 5, "Kein Bootloader-ACK (0x7F)."); break; }

    // 6) Get-Befehl: unterstützte Kommandos abfragen (Standard- vs. Extended-Erase).
    bool extErase = false;
    Serial1.write((uint8_t)0x00); Serial1.write((uint8_t)0xFF); Serial1.flush();
    if (!blWaitAck(1000)) { vmUpdSet(VMU_ERROR, 6, "Get-Befehl fehlgeschlagen."); break; }
    int n = blReadByte(1000);                 // Anzahl folgender Kommando-Bytes
    int ver = blReadByte(1000); (void)ver;    // Bootloader-Version
    if (n < 0 || ver < 0) { vmUpdSet(VMU_ERROR, 6, "Get-Antwort unvollstaendig."); break; }
    for (int i = 0; i < n; i++) {
      int c = blReadByte(1000);
      if (c == 0x44) extErase = true;         // Extended Erase unterstützt
    }
    if (!blWaitAck(1000)) { vmUpdSet(VMU_ERROR, 6, "Get-Abschluss-ACK fehlt."); break; }

    // 7) NUR die Programmpages löschen (Page 0 .. nötige Pages). KEIN Mass-Erase, damit die
    //    letzte Page (emuliertes EEPROM = Kalibrierung) erhalten bleibt.
    vmUpdSet(VMU_RUNNING, 10, "Loesche Programm-Flash...");
    uint16_t pages = (uint16_t)((fwSize + VM_FLASH_PAGE - 1) / VM_FLASH_PAGE);
    if (pages == 0) pages = 1;
    if (pages > VM_EEPROM_PAGE) { // würde die EEPROM-Page überschreiben
      vmUpdSet(VMU_ERROR, 10, "Firmware zu gross (Kollision mit EEPROM-Page).");
      break;
    }
    if (extErase) {
      if (!blCmd(0x44, 2000)) { vmUpdSet(VMU_ERROR, 10, "Extended-Erase abgelehnt."); break; }
      // N (Pages-1) als 2 Byte, dann je Page 2 Byte (MSB first), dann XOR-Checksumme.
      uint16_t nm1 = pages - 1;
      uint8_t chk = 0;
      uint8_t b;
      b = (uint8_t)(nm1 >> 8);  Serial1.write(b); chk ^= b;
      b = (uint8_t)(nm1 & 0xFF); Serial1.write(b); chk ^= b;
      for (uint16_t p = 0; p < pages; p++) {
        b = (uint8_t)(p >> 8);  Serial1.write(b); chk ^= b;
        b = (uint8_t)(p & 0xFF); Serial1.write(b); chk ^= b;
      }
      Serial1.write(chk); Serial1.flush();
      if (!blWaitAck(30000)) { vmUpdSet(VMU_ERROR, 10, "Extended-Page-Erase Timeout."); break; }
    } else {
      if (!blCmd(0x43, 2000)) { vmUpdSet(VMU_ERROR, 10, "Erase abgelehnt."); break; }
      // N (Pages-1) als 1 Byte, dann je Page 1 Byte, dann XOR-Checksumme.
      uint8_t nm1 = (uint8_t)(pages - 1);
      uint8_t chk = nm1;
      Serial1.write(nm1);
      for (uint16_t p = 0; p < pages; p++) { Serial1.write((uint8_t)p); chk ^= (uint8_t)p; }
      Serial1.write(chk); Serial1.flush();
      if (!blWaitAck(30000)) { vmUpdSet(VMU_ERROR, 10, "Page-Erase Timeout."); break; }
    }

    // 8) Schreiben in 256-Byte-Blöcken ab 0x08000000.
    uint8_t buf[BL_BLOCK];
    uint32_t addr = VM_FLASH_BASE;
    size_t written = 0;
    bool writeOk = true;
    f.seek(0);
    while (written < fwSize) {
      int rd = f.read(buf, BL_BLOCK);
      if (rd <= 0) { writeOk = false; vmUpdSet(VMU_ERROR, 0, "Datei-Lesefehler."); break; }
      // auf 4-Byte-Grenze auffüllen (Bootloader schreibt wortweise)
      while (rd & 0x03) buf[rd++] = 0xFF;

      if (!blCmd(0x31, 1000)) { writeOk = false; vmUpdSet(VMU_ERROR, 0, "Write-Befehl abgelehnt."); break; }
      if (!blAddr(addr, 1000)) { writeOk = false; vmUpdSet(VMU_ERROR, 0, "Write-Adresse abgelehnt."); break; }
      Serial1.write((uint8_t)(rd - 1));
      uint8_t chk = (uint8_t)(rd - 1);
      for (int i = 0; i < rd; i++) { Serial1.write(buf[i]); chk ^= buf[i]; }
      Serial1.write(chk);
      Serial1.flush();
      if (!blWaitAck(2000)) { writeOk = false; vmUpdSet(VMU_ERROR, 0, "Write-Block ohne ACK."); break; }

      addr    += rd;
      written += rd;
      int prog = 10 + (int)((written * 85ULL) / fwSize); // 10..95 %
      vmUpdSet(VMU_RUNNING, prog, "Schreibe Firmware...");
    }
    if (!writeOk) break;

    // 9) Go: neue Anwendung ab 0x08000000 starten.
    vmUpdSet(VMU_RUNNING, 97, "Starte neue Firmware...");
    if (!blCmd(0x21, 1000) || !blAddr(VM_FLASH_BASE, 1000)) {
      // Go fehlgeschlagen -> der nächste Voltmeter-Reset startet die FW trotzdem.
      vmUpdSet(VMU_ERROR, 97, "Flash ok, aber 'Go' fehlte (Voltmeter neu starten).");
      break;
    }
    ok = true;
  } while (0);

  // 10) Aufräumen: zurück auf 8N1, communicationTask wieder aktivieren.
  f.close();
  Serial1.end();
  Serial1.begin(115200, SERIAL_8N1, PIN_RX, PIN_TX);
  while (Serial1.available()) Serial1.read();
  vTaskResume(h_communicationTask);

  if (ok) vmUpdSet(VMU_SUCCESS, 100, "Update erfolgreich.");
}

// Persistenter Task: wartet auf den Trigger aus dem Web und flasht dann.
void voltmeterUpdateTask(void *parameter) {
  for (;;) {
    if (vmUpdateRequested) {
      vmUpdateRequested = false;
      runVoltmeterFlash();
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

/**
 * @brief FreeRTOS Task zur Steuerung der Status-LED.
 * Zeigt den aktuellen Systemzustand durch verschiedene Blinkmuster an.
 * Status-LED ist LOW-Aktiv.
 * @param parameter Standard FreeRTOS Task-Parameter (hier ungenutzt).
 */
void statusLedTask(void *parameter) {
  pinMode(PIN_ESP_STATUS, OUTPUT);

  for (;;) { // Endlosschleife für den Task
    switch (currentSystemState) {
      case STATE_WIFI_CONNECTING: // Langsames Blinken
        digitalWrite(PIN_ESP_STATUS, LOW);
        vTaskDelay(pdMS_TO_TICKS(500));
        digitalWrite(PIN_ESP_STATUS, HIGH);
        vTaskDelay(pdMS_TO_TICKS(500));
        break;

      case STATE_WIFIMANAGER_AP: // Schneller Doppel-Blink
        digitalWrite(PIN_ESP_STATUS, LOW); vTaskDelay(pdMS_TO_TICKS(100));
        digitalWrite(PIN_ESP_STATUS, HIGH);  vTaskDelay(pdMS_TO_TICKS(100));
        digitalWrite(PIN_ESP_STATUS, LOW); vTaskDelay(pdMS_TO_TICKS(100));
        digitalWrite(PIN_ESP_STATUS, HIGH);  vTaskDelay(pdMS_TO_TICKS(1000));
        break;

      case STATE_OTA_UPDATE: // Schnelles Blinken
        digitalWrite(PIN_ESP_STATUS, LOW);
        vTaskDelay(pdMS_TO_TICKS(150));
        digitalWrite(PIN_ESP_STATUS, HIGH);
        vTaskDelay(pdMS_TO_TICKS(150));
        break;

      case STATE_ERROR: // SOS
        // S
        for(int i=0; i<3; i++) { digitalWrite(PIN_ESP_STATUS, LOW); vTaskDelay(pdMS_TO_TICKS(150)); digitalWrite(PIN_ESP_STATUS, HIGH); vTaskDelay(pdMS_TO_TICKS(150)); }
        vTaskDelay(pdMS_TO_TICKS(300));
        // O
        for(int i=0; i<3; i++) { digitalWrite(PIN_ESP_STATUS, LOW); vTaskDelay(pdMS_TO_TICKS(400)); digitalWrite(PIN_ESP_STATUS, HIGH); vTaskDelay(pdMS_TO_TICKS(150)); }
        vTaskDelay(pdMS_TO_TICKS(300));
        // S
        for(int i=0; i<3; i++) { digitalWrite(PIN_ESP_STATUS, LOW); vTaskDelay(pdMS_TO_TICKS(150)); digitalWrite(PIN_ESP_STATUS, HIGH); vTaskDelay(pdMS_TO_TICKS(150)); }
        vTaskDelay(pdMS_TO_TICKS(1000));
        break;
        
      case STATE_NORMAL_OPERATION: // Langsames "Atmen" / Herzschlag
      default:
        digitalWrite(PIN_ESP_STATUS, LOW);
        vTaskDelay(pdMS_TO_TICKS(100));
        digitalWrite(PIN_ESP_STATUS, HIGH);
        vTaskDelay(pdMS_TO_TICKS(2900));
        break;
    }
  }
}

/**
 * @brief Hochpriorer Task, der sich ausschließlich um die Ausführung der Stepper-Bewegungen kümmert.
 * Ruft kontinuierlich stepper.run() auf, um eine flüssige Motorbewegung zu gewährleisten,
 * gibt aber dem Betriebssystem mit vTaskDelay(1) kurz die Kontrolle zurück,
 * um andere Prozesse (wie den Encoder) nicht zu blockieren.
 */
void stepperTask(void *parameter) {
  for (;;) {
    // Der Task schläft hier, bis die ISR das Semaphore "gibt".
    // Der "portMAX_DELAY" sorgt dafür, dass er ewig wartet, wenn kein Signal kommt.
    if (xSemaphoreTake(stepperSemaphore, portMAX_DELAY) == pdTRUE) {
      
      // Sobald er aufgeweckt wurde, erledigt er seine Arbeit:
      if (hardwareInitialized) {
        // Unter stepperMux: AccelStepper ist nicht thread-safe — run() darf nicht
        // mitten in ein moveTo() aus einem anderen Task fallen (#5).
        portENTER_CRITICAL(&stepperMux);
        stepper.run();
        portEXIT_CRITICAL(&stepperMux);
      }
    }
  }
}

// ********************************************************************************
// Setup functions
// ********************************************************************************
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
    A_onoff->setCallBack(cb_SettingsHomingAction);
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
    A_p1->setValuePreset(minWhiperPos);
    logMessage(LOG_INFO, "Loaded minWhiperPos %d into A_p1", minWhiperPos);

    A_p2->setValuePreset(maxWhiperPos);
    logMessage(LOG_INFO, "Loaded maxWhiperPos %d into A_p2", maxWhiperPos);

    mcp.digitalWrite(PIN_RELAIS_ONOFF, HIGH);
    mcp.digitalWrite(PIN_RELAIS_LIMIT, HIGH);
}

/**
 * @brief Initialisiert das PWM-Signal für den Lüfter.
 * @param tempSensorAvailable Gibt an, ob ein Temperatursensor gefunden wurde.
 */
void initFAN(boolean tempSensorAvailable) {
    ledcAttach(PIN_FANPWM, FAN_PWM_FREQ, FAN_PWM_RESOLUTION);

    uint8_t dc = tempSensorAvailable ? MINFANPWM : NOSENSORFANSPEED;
    setFanSpeed(dc);	
}

/**
 * @brief Initialisiert den OneWire-Bus und den Temperatursensor.
 * @return boolean true, wenn ein Sensor gefunden und konfiguriert wurde, sonst false.
 */
boolean initTempSensor() {
    boolean res;
    sensors.begin();
    res = sensors.getAddress(tempSensorAdr, 0);
    res &= sensors.setResolution(tempSensorAdr, 9);
    sensors.setWaitForConversion(false);
    sensors.requestTemperatures();
    return res;
}

/**
 * @brief Setzt die interne Struktur für die Display-Werte zurück.
 * Erzwingt ein Neuzeichnen aller Elemente beim nächsten Update.
 */
void initDisplayStruct() {
    actDispValues.temp = -1;
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

/**
 * @brief Standard Arduino setup()-Funktion. Wird einmal beim Start ausgeführt.
 * Initialisiert alle Hardware-Komponenten, startet das Netzwerk und erstellt die RTOS-Tasks.
 */
void setup() {
  currentSystemState = STATE_WIFI_CONNECTING;

  // #4: Logging-Infrastruktur ZUERST, damit ab dem ersten logMessage() alles über die
  // Queue läuft (Serial/Historie/WS/Flash nur noch im Logger-Task).
  logQueue = xQueueCreate(24, sizeof(LogEntry));
  logHistoryMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(
      loggerTask,         // Task-Funktion
      "Logger",           // Name
      4096,               // Stack (String-/Datei-Operationen)
      NULL,               // Parameter
      1,                  // Niedrige Priorität
      &h_loggerTask,      // Handle (Stack-Überwachung)
      0);                 // Auf Core 0 pinnen

  xTaskCreatePinnedToCore(
      statusLedTask,      // Task-Funktion
      "StatusLED",        // Name
      1024,               // Kleiner Stack ist ausreichend
      NULL,               // Parameter
      1,                  // Niedrige Priorität
      NULL,               // Kein Handle nötig
      0);                 // Auf Core 0 pinnen

    uint32_t ser_start = millis();
    Serial.begin(115200);
    while (!Serial && ser_start + 5000 < millis());
    logMessage(LOG_INFO, "SYSTEM: Starting system...");

  // Initialisiere die zweite serielle Schnittstelle (USART1 auf PA9/PA10)
  Serial1.begin(115200, SERIAL_8N1, PIN_RX, PIN_TX);
  logMessage(LOG_INFO, "SYSTEM: Serial1 activated - Connection to voltmeter");

#ifdef SIM
  logMessage(LOG_WARN, "SYSTEM: *** SIMULATION MODE - voltmeter input is simulated from stepper position ***");
#endif

  // Setze den DHCP-Hostnamen
  // Dies ist der Name, der im Router angezeigt wird.
  WiFi.setHostname(hostname);

  // --- WiFiManager Initialisierung ---
  // Erstellt eine WiFiManager Instanz.
  WiFiManager wm;

  // Setze den Timeout auf 600 Sekunden (10 Minuten)
  wm.setConfigPortalTimeout(600);

  // Callback für den AP-Modus hinzufügen
   wm.setAPCallback([](WiFiManager *myWiFiManager) {
    currentSystemState = STATE_WIFIMANAGER_AP;
    logMessage(LOG_INFO, "WLAN: Entered WiFiManager config mode");
  });

  // Startet den Konfigurations-AP.
  // Wenn die Verbindung nicht innerhalb des Timeouts hergestellt wird, startet der ESP neu.
  if (!wm.autoConnect("TWM_IsolationVariac")) {
    logMessage(LOG_ERROR, "WLAN: No WiFi config entered or credentials wrong! Rebooting...");
    delay(3000);
    // Neustart, um es erneut zu versuchen
    ESP.restart();
    delay(5000);
  }

  // Wenn die Verbindung erfolgreich war:
  logMessage(LOG_INFO, "WLAN: Successfully connected to WiFi!");

  // WiFi-Modem-Sleep deaktivieren: hält das Funkmodul wach -> schnelleres OTA
  // und reaktiveres Web-Interface (geringfügig höhere Stromaufnahme).
  WiFi.setSleep(false);

  // --- OTA (Over-the-Air) Konfiguration ---
  
  // Hostname für den Netzwerk-Port in der Arduino IDE und den Zugang via mDNS
  ArduinoOTA.setHostname(hostname);

  // Optional aber empfohlen: Passwort für den Upload setzen
  // ArduinoOTA.setPassword("dein_sicheres_passwort");

  ArduinoOTA
    .onStart([]() {
      String type;
      currentSystemState = STATE_OTA_UPDATE; // Zustand auf OTA-Update setzen
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";
      logMessage(LOG_INFO, "OTA: Start updating %s", type.c_str());
    })
    .onEnd([]() {
      currentSystemState = STATE_NORMAL_OPERATION; // Zurück zum Normalbetrieb
      logMessage(LOG_INFO, "OTA: Update finished - Rebooting");
      ws.closeAll();
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      logMessage(LOG_INFO, "OTA: Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      currentSystemState = STATE_ERROR; // Fehlerzustand bei OTA-Problem
      logMessage(LOG_ERROR, "OTA: Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) logMessage(LOG_ERROR, "OTA: Auth failed");
      else if (error == OTA_BEGIN_ERROR) logMessage(LOG_ERROR, "OTA: Begin failed");
      else if (error == OTA_CONNECT_ERROR) logMessage(LOG_ERROR, "OTA: Connect failed");
      else if (error == OTA_RECEIVE_ERROR) logMessage(LOG_ERROR, "OTA: Receive failed");
      else if (error == OTA_END_ERROR) logMessage(LOG_ERROR, "OTA: End failed");
    });

  ArduinoOTA.begin();

  logMessage(LOG_INFO, "SYSTEM: OTA & mDNS ready. Access at http://%s.local/", hostname);
  logMessage(LOG_INFO, "SYSTEM: IP address: %s", WiFi.localIP().toString().c_str());

  // Initialisiere das LittleFS-Dateisystem
  if(!LittleFS.begin(true, "/littlefs", 10, "spiffs")){
    logMessage(LOG_ERROR, "SYSTEM: Error mounting LittleFS");
    currentSystemState = STATE_ERROR; // Signalisiere einen Fehler
  } else {
    logMessage(LOG_INFO, "SYSTEM: LittleFS mounted successfully.");
  }

  initWebServer();

  initDisplayStruct();
	
  pinMode(PIN_DISP_BL, OUTPUT);
  pinMode(PIN_SW1, INPUT_PULLUP);
  pinMode(PIN_ENCSW, INPUT_PULLUP);

  pinMode(PIN_ENCCLK, INPUT);
  pinMode(PIN_ENCDT, INPUT);


  uint8_t result = 0x00;
  uint8_t retry = 0;
  logMessage(LOG_INFO, "SYSTEM: Initializing I2C...");
	Wire.begin(PIN_SDA, PIN_SCL);
  logMessage(LOG_INFO, "SYSTEM: Initializing MCP23017...");

  logMessage(LOG_INFO, "SYSTEM: Scanning for MCP23017 at 0x20...");
  if (checkI2CDevice(MCP23017_ADDR)) {
    
    // ERFOLGSFALL: Gerät wurde gefunden
    logMessage(LOG_INFO, "SYSTEM: MCP23017 found! Initializing...");
    mcp.init(); // Jetzt können wir den Chip sicher initialisieren
    mcp.portMode(MCP23017Port::A, 0);
    mcp.portMode(MCP23017Port::B, 0);

    // JETZT die Action-Objekte sicher im Speicher erstellen
    A_onoff = new Action(mcp, PIN_T_ONOFF, PIN_LED_ONOFF);
    A_limit = new Action(mcp, PIN_T_LIMIT, PIN_LED_LIMIT);
    A_reg   = new Action(mcp, PIN_T_REG, PIN_LED_REG);
    A_p1    = new Action(mcp, PIN_T_P1, PIN_LED_P1);
    A_p2    = new Action(mcp, PIN_T_P2, PIN_LED_P2);
    A_p3    = new Action(mcp, PIN_T_P3, PIN_LED_P3);
    A_x10   = new Action(mcp, PIN_ENCSW, PIN_LED_x10);

    // Gruppe befüllen
    g[0] = A_p1;
    g[1] = A_p2;
    g[2] = A_p3;

    hardwareInitialized = true;
    currentSystemState = STATE_NORMAL_OPERATION;

  } else {
    
    // FEHLERFALL: Gerät antwortet nicht
    logMessage(LOG_ERROR, "SYSTEM: FATAL ERROR - MCP23017 not found on I2C bus!");
    hardwareInitialized = false;
    currentSystemState = STATE_ERROR;
  }

  // NEU: Erstelle den Mutex für den TFT-Zugriff
  tftMutex = xSemaphoreCreateMutex();
  if (tftMutex == NULL) {
    logMessage(LOG_ERROR, "SYSTEM: FATAL ERROR - Could not create TFT Mutex");
    currentSystemState = STATE_ERROR;
  }
  
  logMessage(LOG_INFO, "SYSTEM: Initializing LCD...");
  tft.init();
  tft.setRotation(2);  // Portrait, Connector 12 o'clock
  clearScreen();
	setScreenBacklight(true);

  if (!hardwareInitialized) {
      currentSystemState = STATE_ERROR;
      logMessage(LOG_ERROR, "SYSTEM: FATAL ERROR - System cannot start!");
      drawErrorScreen();
      while (1);
  }
  logMessage(LOG_INFO, "SYSTEM: Loading config file...");
  loadConfiguration();	
  logMessage(LOG_INFO, "SYSTEM: Initializing temperature sensor...");
  tempSensorAvailable = initTempSensor();
  logMessage(LOG_INFO, "SYSTEM: Initializing fan...");
	initFAN(tempSensorAvailable);

  logMessage(LOG_INFO, "SYSTEM: Initializing stepper...");
    stepperSemaphore = xSemaphoreCreateBinary();
    stepper.setMaxSpeed(STEPPERMAXSPEED);
    stepper.setMinPulseWidth(40);
    stepper.setAcceleration(STEPPERACCELERATION);
    stepper.setEnablePin(PIN_EN);
    stepper.setPinsInverted(true, true, false, false, true);
    stepper.enableOutputs();
    initStepperCallback();

  logMessage(LOG_INFO, "SYSTEM: Initializing encoder...");
    ESP32Encoder::useInternalWeakPullResistors=puType::up;
    encoder.attachFullQuad(PIN_ENCDT, PIN_ENCCLK);
    encoder.setCount(ENCMIDPOINT); // Setzt den Startwert
    lastEncPos = getEncoderCount();

    mcp.writeRegister(MCP23017Register::GPIO_A, 0x00);  //all LED OFF

    
    // Wähle den Start-Modus basierend auf dem gedrückten Taster
    if (!digitalRead(PIN_T_REG)) {
        logMessage(LOG_INFO, "SYSTEM: Setup button pressed, entering SETTINGS MODE");
        is_regulation_active = false;
        drawSettingsScreen();
        initSettingsActions();
        A_p1->on();
        currentMode = MODE_SETTINGS;
    } else {
        logMessage(LOG_INFO, "SYSTEM: Starting in NORMAL MODE");
        logMessage(LOG_INFO, "SYSTEM: Initializing actions and homing...");
        initActions();
        drawHomingScreen();
        homing();
        initDisplayStruct();
        drawBackground();
        drawLegend();

        logMessage(LOG_INFO, "SYSTEM: Homing completed");
        currentMode = MODE_NORMAL;
    }

    logMessage(LOG_INFO, "SYSTEM: Initializing RTOS tasks...");
    // Erstelle und starte die Tasks
    xTaskCreatePinnedToCore(
        userInputTask,      // Task-Funktion
        "UserInput",        // Name des Tasks
        4096,               // Stack-Größe in Wörtern
        NULL,               // Task-Parameter
        3,                  // Priorität (höher ist wichtiger)
        &h_userInputTask,   // Task-Handle
        1);                 // Auf Core 1 pinnen

    xTaskCreatePinnedToCore(
        motorControlTask,
        "MotorControl",
        4096,               // Mehr Stack für die komplexe Logik
        NULL,
        2,                  // Normale Priorität
        &h_motorControlTask,
        1);

    xTaskCreatePinnedToCore(
        displayUpdateTask,
        "DisplayUpdate",
        4096,               // Display-Libs brauchen oft mehr Stack
        NULL,
        1,                  // Niedrige Priorität
        &h_displayUpdateTask,
        0);                 // Auf Core 0 pinnen

    xTaskCreatePinnedToCore(
        sensorAndFanTask,
        "SensorFan",
        2048,
        NULL,
        1,
        &h_sensorAndFanTask,
        0);

    xTaskCreatePinnedToCore(
        communicationTask,
        "Communication",
        4096,
        NULL,
        2,
        &h_communicationTask,
        0);

    xTaskCreatePinnedToCore(
        voltmeterUpdateTask,
        "VmUpdate",
        4096,
        NULL,
        1,                    // niedrige Priorität, idlet meistens
        &h_voltmeterUpdateTask,
        1);                   // Core 1, damit der communicationTask auf Core 0 frei bleibt

    xTaskCreatePinnedToCore(
        stepperTask,
        "Stepper",
        2048,
        NULL,
        4,                    // HÖCHSTE Priorität (höher als userInputTask)
        &h_stepperTask,
        1);                   // Auf Core 1, wo auch der User-Input läuft

    logMessage(LOG_INFO, "SYSTEM: START - RTOS tasks running");
}

// ********************************************************************************
// Main program loop
// ********************************************************************************
/**
 * @brief Standard Arduino loop()-Funktion.
 * Läuft in einem eigenen Task und kümmert sich um Hintergrund-Dienste wie OTA-Updates.
 */
void loop() {
  // Diese Funktion muss im Loop aufgerufen werden, damit OTA Anfragen
  // empfangen und verarbeitet werden können.
  ArduinoOTA.handle();
  
  // Alle 5 Sekunden die Stacks aller Tasks auf kritische Werte prüfen
  if (millis() - lastStackCheck > 5000) {
    lastStackCheck = millis();
    
    // Eine Liste aller Task-Handles, die wir überwachen wollen
    TaskHandle_t tasksToCheck[] = {
        h_userInputTask,
        h_motorControlTask,
        h_displayUpdateTask,
        h_sensorAndFanTask,
        h_communicationTask,
        h_stepperTask,
        h_loggerTask
    };

    for (TaskHandle_t taskHandle : tasksToCheck) {
      if (taskHandle != NULL) {
        // Ermittle die High Water Mark in Bytes
        uint32_t hwm_bytes = uxTaskGetStackHighWaterMark(taskHandle) * sizeof(StackType_t);
        
        // Gib NUR DANN eine Warnung aus, wenn der Wert kritisch ist
        if (hwm_bytes < CRITICAL_STACK_THRESHOLD) {
          // pcTaskGetName() holt den Namen, den wir in xTaskCreate vergeben haben
          logMessage(LOG_WARN, "SYSTEM: !!! STACK WARNING - Task '%s' has only %u Bytes free !!!", pcTaskGetName(taskHandle), hwm_bytes);
        } else {
          //logMessage(LOG_INFO, "STACK - Task '%s' has %u Bytes free !!!", pcTaskGetName(taskHandle), hwm_bytes);
        }
      }
    }
  }

  // Die loop() darf nicht blockieren, daher nur eine minimale Pause
  vTaskDelay(pdMS_TO_TICKS(10));
}