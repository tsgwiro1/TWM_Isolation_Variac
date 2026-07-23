// TWM Isolation Variac – Definitionen der geteilten Zustände (#10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#include "state.h"

volatile bool hardwareInitialized = false;
volatile SystemMode currentMode = MODE_NORMAL; // Standardmässig im Normalbetrieb starten
volatile bool requestEnterSettingsMode = false;
volatile SystemState currentSystemState = STATE_STARTING;

volatile int wiperPos = 0;
volatile int minWiperPos = 0;
volatile int maxWiperPos = 2000;
volatile uint32_t last_encoder_change_time = 0;

portMUX_TYPE calibMux   = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE stepperMux = portMUX_INITIALIZER_UNLOCKED;

volatile bool isRecallPreset = false;
volatile RegPhase regPhase = RP_IDLE;

volatile bool is_regulation_active = false;
volatile float setpoint_voltage = 0.0f;
volatile float minVoltageAtMinPos = 3.0f;
volatile float maxVoltageAtMaxPos = 255.0f;

// Regelparameter (#31): Startwerte = bisherige #define-Werte; Config überschreibt beim Boot.
volatile float    reg_deadband_v   = 1.0f;
volatile float    reg_damping      = 0.8f;
volatile uint32_t reg_settle_ms    = 150;
volatile float    reg_undershoot_v = 5.0f;

TaskHandle_t h_userInputTask;
TaskHandle_t h_motorControlTask;
TaskHandle_t h_displayUpdateTask;
TaskHandle_t h_sensorAndFanTask;
TaskHandle_t h_communicationTask;
TaskHandle_t h_stepperTask;
TaskHandle_t h_voltmeterUpdateTask;
TaskHandle_t h_loggerTask;

volatile bool otaReady = false;
