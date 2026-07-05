// TWM Isolation Variac – Thread-sicheres Logging über Queue + Logger-Task (#4, #10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#ifndef LOGGING_H
#define LOGGING_H

#include <Arduino.h>

enum LogLevel { LOG_INFO, LOG_WARN, LOG_ERROR };

extern volatile bool debugEnabled; // Steuert die Serial-Log-Ausgabe (Teil der Konfiguration)

// Log-Datei auf LittleFS (nur WARN+, mit Rotation; /api/log liefert sie aus)
#define LOG_FILE "/system.log"

// Erzeugt Queue, Mutex und Logger-Task. GANZ FRÜH in setup() aufrufen,
// damit ab dem ersten logMessage() alles über die Queue läuft.
void loggingInit();

// Formatiert und reiht die Meldung ein (nicht blockierend; volle Queue -> verwerfen+zählen).
void logMessage(LogLevel level, const char* format, ...);

// Kopie der RAM-Log-Historie (unter Mutex) — z. B. für den WebSocket-Connect.
String logHistorySnapshot();

#endif // LOGGING_H
