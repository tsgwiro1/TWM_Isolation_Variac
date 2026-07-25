// TWM Isolation Variac – Stepper, Homing und Spannungsregelung (#17, #10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#ifndef MOTOR_H
#define MOTOR_H

#include <AccelStepper.h>

extern AccelStepper stepper;

// Initialisiert Semaphore, AccelStepper-Parameter und Ticker/ISR (in setup() aufrufen).
void initStepper();
// Ticker-basierter "Wecker" für den stepperTask (ISR gibt Semaphore).
void initStepperCallback();
// Referenzfahrt auf den unteren Endanschlag (blockierend; aus setup() und beim
// Kalibrier-Einstieg aus dem userInputTask — pausiert dann selbst den stepperTask, GitHub-#3).
void homing();
bool isHomingActive();
// Kalibrier-Anfahrt: innerhalb 0..2000 normale Geschwindigkeit, außerhalb gedrosselt (GitHub-#3).
void setCalibrationApproachSpeed(int targetPos);
// Schleifer bewegen (Grenzen: kalibrierte min/max-Position; thread-safe, #5).
void setWiperRelativ(int delta);
void setWiperAbsolut(int value);
void setWiperMove(int value, bool relative);
void stopWiperMove();   // laufende Bewegung ausbremsen (GitHub-#26, vor dem OTA-Start)
// Kalibrier-Viertupel als konsistenten Satz lesen (#5).
void getCalibration(int& minPos, int& maxPos, float& minV, float& maxV);
// Regelungs-Mathematik
int estimatePositionForVoltage(float target_voltage);
int maxVoltageTarget();
float voltsPerStep();
// RTOS-Tasks
void motorControlTask(void *parameter);
void stepperTask(void *parameter);

#endif // MOTOR_H
