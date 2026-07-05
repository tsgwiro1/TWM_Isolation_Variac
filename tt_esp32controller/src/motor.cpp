// TWM Isolation Variac – Stepper, Homing und Spannungsregelung (#17, #10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#include "motor.h"
#include <Ticker.h>
#include "pins.h"
#include "state.h"
#include "logging.h"
#include "comm.h"     // received_rms_value, isVoltageDataFresh()
#include "actions.h"  // A_reg (REG-Modus in der Regelung)

// Stepper
AccelStepper stepper(AccelStepper::DRIVER, PIN_STEP, PIN_DIR);
static Ticker stepperTicker;
static SemaphoreHandle_t stepperSemaphore;

#define STEPPERMAXSPEED             1000
#define STEPPERACCELERATION         1500
#define STEPPERHOMINGSPEED          150
#define STEPPERHOMINGSPEED2         10
#define STEPPERHOMINGRETRACT        25

static void isr_stepper();

/**
 * @brief Initialisiert und startet den Ticker, der als "Wecker" für den stepperTask dient.
 */
// Initialisiert Semaphore, AccelStepper-Parameter und den Ticker/ISR-Wecker.
// Reihenfolge wichtig: Semaphore VOR dem Ticker-Start (die ISR gibt sie frei).
void initStepper() {
    stepperSemaphore = xSemaphoreCreateBinary();
    stepper.setMaxSpeed(STEPPERMAXSPEED);
    stepper.setMinPulseWidth(40);
    stepper.setAcceleration(STEPPERACCELERATION);
    stepper.setEnablePin(PIN_EN);
    stepper.setPinsInverted(true, true, false, false, true);
    stepper.enableOutputs();
    initStepperCallback();
}

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
    wiperPos = 0;
    initStepperCallback(); // Startet den Ticker/Wecker wieder für den normalen Betrieb
}

// ********************************************************************************
// Hardware and System control functions
// ********************************************************************************
/**
 * @brief Bewegt den Schleifer um eine relative Anzahl von Schritten.
 * @param delta Die Anzahl der Schritte, um die bewegt werden soll (positiv oder negativ).
 */
void setWiperRelativ(int delta) {
    // Delta-Anwendung läuft komplett unter stepperMux (in setWiperMove), damit sich
    // gleichzeitige relative Bewegungen aus mehreren Tasks nicht gegenseitig verlieren. (#5)
    setWiperMove(delta, true);
}

/**
 * @brief Bewegt den Schleifer zu einer absoluten Position.
 * Die Position wird durch minWiperPos und maxWiperPos begrenzt.
 * @param value Die absolute Zielposition.
 */
void setWiperAbsolut(int value) {
    setWiperMove(value, false);
}

/**
 * @brief Gemeinsamer Kern für relative/absolute Schleifer-Bewegung (#5).
 * wiperPos-Read-Modify-Write und stepper.moveTo() laufen atomar unter stepperMux
 * (serialisiert gegen stepper.run() im Stepper-Task und gegen andere Aufrufer).
 */
void setWiperMove(int value, bool relative) {
    int minPos, maxPos;
    float minV, maxV;
    getCalibration(minPos, maxPos, minV, maxV);

    portENTER_CRITICAL(&stepperMux);
    if (relative) value += wiperPos;
    wiperPos = constrain(value, minPos, maxPos);
    stepper.moveTo(wiperPos);
    portEXIT_CRITICAL(&stepperMux);
}

/**
 * @brief Liest die 4 Kalibrierwerte als konsistenten Satz (#5).
 * Schreiber (Web-Config, /api/calibration/save, Settings-Modus) schreiben unter demselben
 * calibMux — Leser sehen dadurch nie einen halb-aktualisierten Satz.
 */
void getCalibration(int& minPos, int& maxPos, float& minV, float& maxV) {
  portENTER_CRITICAL(&calibMux);
  minPos = minWiperPos;
  maxPos = maxWiperPos;
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
  // nicht 0 — minWiperPos ist kalibrierungsbedingt negativ).
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
 * @brief Extrem kurze ISR, die nur ein Semaphore an den stepperTask gibt.
 * Dies geschieht alle 100 Mikrosekunden.
 */
static void ICACHE_RAM_ATTR isr_stepper() {
  // Wecke den stepperTask auf. Wichtig: FromISR-Version verwenden!
  xSemaphoreGiveFromISR(stepperSemaphore, NULL);
}


// ********************************************************************************
// RTOS Task Functions
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
      int margin = (gain > 0.0f) ? (int)(reg_undershoot_v / gain) : 0;
      int curPos = stepper.currentPosition();
      if (targetPos > curPos)      targetPos = max(curPos, targetPos - margin); // hoch -> tiefer stoppen
      else if (targetPos < curPos) targetPos = min(curPos, targetPos + margin); // runter -> höher stoppen

      setWiperAbsolut(targetPos);
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
          if (millis() - settleStart < reg_settle_ms || !isVoltageDataFresh()) break;

          float error = setpoint_voltage - received_rms_value;
          if (fabs(error) <= reg_deadband_v || correctionCount >= REG_MAX_CORRECTIONS || gain <= 0.0f) {
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
            int steps = constrain((int)(error / gain * reg_damping),
                                  -REG_MAX_CORRECTION_STEPS, REG_MAX_CORRECTION_STEPS);
            setWiperRelativ(steps);
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
          if (fabs(error) > reg_deadband_v && gain > 0.0f) {
            if (driftStart == 0) {
              driftStart = millis();
            } else if (millis() - driftStart >= REG_DRIFT_PERSIST_MS) {
              int steps = constrain((int)(error / gain * reg_damping),
                                    -REG_MAX_CORRECTION_STEPS, REG_MAX_CORRECTION_STEPS);
              setWiperRelativ(steps);
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
