// TWM Isolation Variac – Thread-sicheres Logging über Queue + Logger-Task (#4, #10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#ifndef LOGGING_H
#define LOGGING_H

#include <Arduino.h>

enum LogLevel { LOG_INFO, LOG_WARN, LOG_ERROR };

extern volatile bool debugEnabled; // Steuert die Serial-Log-Ausgabe (Teil der Konfiguration)

// Log-Dateien auf LittleFS. Seit #23 landen ALLE Level im File (nicht mehr nur WARN+):
// WARN+ werden sofort geschrieben, INFO gesammelt und gebündelt geflasht. Rotation nach
// Zeilenzahl über zwei Dateien; /api/log liefert beide verkettet aus.
#define LOG_FILE     "/system.log"
#define LOG_FILE_OLD "/system.log.old"
#define MAX_LOG_LINES 5000   // pro Datei; danach -> .old, neu anfangen (Download hängt beide zusammen)

// Erzeugt Queue, Mutex und Logger-Task. GANZ FRÜH in setup() aufrufen,
// damit ab dem ersten logMessage() alles über die Queue läuft.
void loggingInit();

// Formatiert und reiht die Meldung ein (nicht blockierend; volle Queue -> verwerfen+zählen).
void logMessage(LogLevel level, const char* format, ...);

// Kopie der RAM-Log-Historie (unter Mutex) — z. B. für den WebSocket-Connect.
String logHistorySnapshot();

// Schreibt gepufferte INFO-Zeilen sofort in die Datei (vor dem Download aufrufen, #23).
void logFlushToFile();

// Anzahl seit dem Start wegen voller Queue verworfener Meldungen (still, für /api/status, #23).
uint32_t logDroppedTotal();

#endif // LOGGING_H
