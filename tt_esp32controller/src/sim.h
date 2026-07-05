// TWM Isolation Variac – Simulationsmodus (#20): Voltmeter-Streckenmodell (#10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#ifndef SIM_H
#define SIM_H

#ifdef SIM
// Berechnet die "gemessene" RMS-Spannung aus der Stepper-Position
// (lineares Modell + Lag + Abweichung + Rauschen).
void simUpdateMeasuredVoltage();
#endif

#endif // SIM_H
