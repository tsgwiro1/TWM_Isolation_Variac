// TWM Isolation Variac – Tasten/LEDs/Relais (Action), Encoder und Benutzereingaben (#10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#ifndef ACTIONS_H
#define ACTIONS_H

#include <Wire.h>
#include <MCP23017.h>
#include <ESP32Encoder.h>
#include "Action.h"

#define MCP23017_ADDR 0x20
extern MCP23017 mcp;

// Encoder
#define ENCMIDPOINT 32767
#define ENCLOWSPEED 2
#define ENCHIGHSPEED 10
extern int lastEncPos;
extern int encSpeed;
extern ESP32Encoder encoder;

// Actions (Tasten mit LED/Relais am MCP23017)
extern Action* A_onoff;
extern Action* A_limit;
extern Action* A_reg;
extern Action* A_p1;
extern Action* A_p2;
extern Action* A_p3;
extern Action* A_x10;
extern Action* g[3];   // Preset-Gruppe (gegenseitiges Ausschalten)

void resetPresetActions();
void forceSafeState();   // sicherer Grundzustand bei gesperrter Bedienung (GitHub-#26)
void handleAllActions();
int getEncoderCount();
// Callbacks (auch von Web-Routen aufgerufen, z. B. recall_p1)
void cb_RelaisAction(Action* act, ButtonEvent event);
void cb_ValueAction(Action* act, ButtonEvent event);
void cb_SettingsValueAction(Action* act, ButtonEvent event);
void cb_x10Action(Action* act, ButtonEvent event);
void cb_RegAction(Action* act, ButtonEvent event);
void cb_SettingsOnOffAction(Action* act, ButtonEvent event);
// Initialisierung (setup)
void initActions();
void initSettingsActions();
bool checkI2CDevice(byte addr);
// RTOS-Task
void userInputTask(void *parameter);

#endif // ACTIONS_H
