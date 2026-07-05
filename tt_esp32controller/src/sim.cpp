// TWM Isolation Variac – Simulationsmodus (#20): Voltmeter-Streckenmodell (#10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#include "sim.h"
#include <Arduino.h>
#include "state.h"
#include "motor.h"   // Stepper-Position als Plant-Eingang
#include "comm.h"    // schreibt received_rms_value

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
  if (maxWiperPos != minWiperPos) {
    idealV = minVoltageAtMinPos + (float)(pos - minWiperPos) *
             (maxVoltageAtMaxPos - minVoltageAtMinPos) / (float)(maxWiperPos - minWiperPos);
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
