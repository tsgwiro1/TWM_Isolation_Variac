// TWM Isolation Variac – geteilte Zustände, Enums und System-Konstanten (#10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#ifndef STATE_H
#define STATE_H

#include <Arduino.h>

// Firmware-Version (entspricht dem CHANGELOG)
#ifdef SIM
#define FW  "Firmware V4.3.0 (SIM)"
#else
#define FW  "Firmware V4.3.0"
#endif

// System
#define CRITICAL_STACK_THRESHOLD 256 // Warnung bei weniger als 256 Bytes freiem Stack
extern volatile bool hardwareInitialized;  // "Wächter"-Variable für den Hardware-Init-Zustand
enum SystemMode { MODE_NORMAL, MODE_SETTINGS };
extern volatile SystemMode currentMode;
extern volatile bool requestEnterSettingsMode;

// Systemzustände für die Status-LED
enum SystemState {
  STATE_WIFI_CONNECTING,
  STATE_WIFIMANAGER_AP,
  STATE_NORMAL_OPERATION,
  STATE_OTA_UPDATE,
  STATE_ERROR
};
extern volatile SystemState currentSystemState;

// Transformer Wiper
#define MINWIPERLIMIT -50
#define MAXWIPERLIMIT 2500
extern volatile int wiperPos;
extern volatile int minWiperPos;
extern volatile int maxWiperPos;
extern volatile uint32_t last_encoder_change_time;

// #5: Schutz geteilter Zustände.
// - calibMux: die 4 Kalibrierwerte (minWiperPos/maxWiperPos/minVoltageAtMinPos/
//   maxVoltageAtMaxPos) werden immer als SATZ geschrieben/gelesen — ohne Schutz könnte die
//   Regelungs-Mathematik einen halb-aktualisierten Satz sehen (falsche Anfahrposition).
// - stepperMux: serialisiert das wiperPos-Read-Modify-Write und alle AccelStepper-Aufrufe
//   (moveTo vs. run() aus verschiedenen Tasks — AccelStepper ist nicht thread-safe).
// Einzelne 32-bit-Skalare (setpoint_voltage, received_rms_value, …) bleiben bewusst nur
// volatile: ausgerichtete 32-bit-Zugriffe sind auf dem ESP32 atomar, und es gibt auf ihnen
// keine zusammengesetzten Read-Modify-Write-Sequenzen.
extern portMUX_TYPE calibMux;
extern portMUX_TYPE stepperMux;

// Sollwert-/Preset-Grenzen. Untergrenze fix 0; effektive Obergrenze liefert
// maxVoltageTarget() = min(kalibriertes Max, MAX_VOLTAGE_TARGET).
// MAX_VOLTAGE_TARGET ist die absolute Sicherheits-Obergrenze (Schutz bei defekter Kalibrierung).
#define MIN_VOLTAGE_TARGET  0
#define MAX_VOLTAGE_TARGET  260
extern volatile bool isRecallPreset;   // löst eine Vorsteuer-Anfahrt auf setpoint_voltage aus

// Phasen der Spannungsregelung (Vorsteuerung + gain-Korrektur + Halten, #17)
enum RegPhase {
  RP_IDLE,         // keine automatische Regelung
  RP_FEEDFORWARD,  // beschleunigte Anfahrt auf die geschätzte Zielposition
  RP_CORRECT,      // gain-gerechte Einzelkorrektur(en) bis im Deadband
  RP_HOLD          // Ziel erreicht, Sollwert halten (Drift-Trim)
};
extern volatile RegPhase regPhase;

// Voltage regulation
extern volatile bool is_regulation_active;
extern volatile float setpoint_voltage;
extern volatile float minVoltageAtMinPos;
extern volatile float maxVoltageAtMaxPos;

// --- Parameter der Spannungsregelung (#17), seit #31 über die Konfiguration einstellbar ---
// (Tuning pro Gerät ohne Code-Änderung; Persistenz im NVS, API/UI über /api/config)
extern volatile float    reg_deadband_v;    // innerhalb +/- dieses Fehlers wird nicht korrigiert
extern volatile float    reg_damping;       // Anteil der berechneten Korrektur (Schutz vor Überschwingen)
extern volatile uint32_t reg_settle_ms;     // Wartezeit nach Stepper-Stopp, bis gemessen wird
extern volatile float    reg_undershoot_v;  // Vorsteuerung stoppt um diese Spannung kurz vor dem Ziel

// Feste Parameter der Spannungsregelung (bewusst nicht konfigurierbar)
#define REG_DRIFT_PERSIST_MS     800    // im Halten: so lange außerhalb Deadband, bevor nachkorrigiert wird
#define REG_MAX_CORRECTION_STEPS 150    // Klemme je Einzelkorrektur (Schutz gegen Ausreißer-Messung)
#define REG_MAX_CORRECTIONS      5      // max. Korrekturiterationen pro Anfahrt

// Periodizität für Regelung und Preset Handling
#define REGULATION_LOOP_PERIOD 100

// Task-Handles (Erzeugung in setup(), Stack-Überwachung in loop())
extern TaskHandle_t h_userInputTask;
extern TaskHandle_t h_motorControlTask;
extern TaskHandle_t h_displayUpdateTask;
extern TaskHandle_t h_sensorAndFanTask;
extern TaskHandle_t h_communicationTask;
extern TaskHandle_t h_stepperTask;
extern TaskHandle_t h_voltmeterUpdateTask;
extern TaskHandle_t h_loggerTask;

#endif // STATE_H
