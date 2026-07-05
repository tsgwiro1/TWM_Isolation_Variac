// TWM Isolation Variac – Lüfter, Temperatursensor und Status-LED (#10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#ifndef SYSTEM_H
#define SYSTEM_H

#include <Arduino.h>

extern volatile float wiperTemp;
extern volatile boolean tempSensorAvailable;

void setFanSpeed(uint8_t v);
float getTemperature();
void updateFan(float temp);
void initFAN(boolean tempSensorAvailable);
boolean initTempSensor();
// RTOS-Tasks
void sensorAndFanTask(void *parameter);
void statusLedTask(void *parameter);

#endif // SYSTEM_H
