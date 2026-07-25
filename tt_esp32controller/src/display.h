// TWM Isolation Variac – TFT-Display: Screens und Update-Task (#10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#ifndef DISPLAY_H
#define DISPLAY_H

#include <TFT_eSPI.h>

extern TFT_eSPI tft;
extern SemaphoreHandle_t tftMutex;  // Mutex zum Schutz des TFT-Displays

void tftStartWrite();
void tftEndWrite();
void setScreenBacklight(boolean on);
void clearScreen();
void drawHomingScreen();
void drawBackground();
void drawLegend();
void drawErrorScreen();
void drawOtaScreen();      // OTA-Update läuft, Variac gesperrt (GitHub-#26)
void updateOtaScreen();    // Fortschritt im OTA-Screen (GitHub-#26)
void drawSettingsScreen();
void initDisplayStruct();  // Anzeige-Cache invalidieren -> alles neu zeichnen
void setSettingsWarning(const char* msg);  // Warnzeile im Setup-Screen, "" löscht (GitHub-#3)
// RTOS-Task
void displayUpdateTask(void *parameter);

#endif // DISPLAY_H
