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

// =============================================================================
// Modularisiert (#10): Die Fachlogik liegt in Modulen (je .h/.cpp):
//   pins.h      Pin-Belegung ESP32-S3 + MCP23017
//   state.*     geteilte Zustände, Enums, Regelparameter, Task-Handles
//   logging.*   thread-sicheres Logging über Queue + Logger-Task (#4)
//   config.*    NVS-Konfiguration + Validierung (#35)
//   motor.*     Stepper, Homing, Spannungsregelung (#17)
//   comm.*      Voltmeter-Link + FW-Update AN3155 (#30)
//   display.*   TFT-Screens + Display-Task
//   actions.*   Tasten/LEDs/Relais, Encoder, Benutzereingaben
//   web.*       REST-API (#22) + WebSocket-Live-Log
//   system.*    Lüfter, Temperatursensor, Status-LED
//   sim.*       Simulationsmodus (#20)
// Hier verbleiben nur setup() und loop().
// =============================================================================

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
using namespace fs;
#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>

#include "pins.h"
#include "state.h"
#include "logging.h"
#include "config.h"
#include "motor.h"
#include "comm.h"
#include "display.h"
#include "actions.h"
#include "web.h"
#include "system.h"
#include "sim.h"

// System
uint32_t lastStackCheck = 0;
const char* hostname = "twm_variac";

/**
 * @brief Standard Arduino setup()-Funktion. Wird einmal beim Start ausgeführt.
 * Initialisiert alle Hardware-Komponenten, startet das Netzwerk und erstellt die RTOS-Tasks.
 */
void setup() {
  currentSystemState = STATE_WIFI_CONNECTING;

  // #4: Logging-Infrastruktur ZUERST, damit ab dem ersten logMessage() alles über die
  // Queue läuft (Serial/Historie/WS/Flash nur noch im Logger-Task).
  loggingInit();

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
    initStepper();          // Semaphore + AccelStepper-Parameter + Ticker/ISR (motor.cpp)

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
        // GitHub-#3: Auch im Setup-Einstieg zuerst die Referenz herstellen — ohne
        // Homing würde die zufällige physische Schleifer-Position zur logischen 0
        // und die Min-/Max-Anfahrten könnten in den mechanischen Anschlag fahren.
        drawHomingScreen();
        homing();
        logMessage(LOG_INFO, "SYSTEM: Homing completed");
        clearScreen();
        drawSettingsScreen();
        initSettingsActions();
        cb_SettingsValueAction(A_p1, ButtonEvent::RELEASED); // Min-Punkt anfahren
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
