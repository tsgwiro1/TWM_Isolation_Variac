// TWM Isolation Variac – Thread-sicheres Logging (#4): Implementation (#10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#include "logging.h"
#include <FS.h>
#include <LittleFS.h>
#include "state.h"
#include "web.h"   // ws.textAll() – Live-Log an offene Browser

#define MAX_LOG_SIZE 20480 // Maximale Grösse der Log-Datei in Bytes (z.B. 20 KB)
static const char* logLevelStrings[] = { "INFO", "WARN", "ERROR" };
volatile bool debugEnabled = true;

// RAM-Puffer für die Log-Historie (ca. 4 KB)
static String logHistory = "";
static const int MAX_LOG_HISTORY = 4096;

// #4: Logging thread-safe — logMessage() formatiert nur noch und legt den Eintrag in eine
// Queue; ein einzelner Logger-Task übernimmt Serial, RAM-Historie, WebSocket und Flash-Write.
struct LogEntry {
  LogLevel level;
  char msg[300];                       // fertig formatierte Zeile inkl. Zeitstempel + '\n'
};
static QueueHandle_t     logQueue = NULL;
static SemaphoreHandle_t logHistoryMutex = NULL;   // schützt logHistory (Logger-Task vs. WS-Connect)
static volatile uint32_t logDroppedCount = 0;      // wegen voller Queue verworfene Meldungen

static void processLogEntry(const LogEntry& entry);
static void loggerTask(void *parameter);

void loggingInit() {
  logQueue = xQueueCreate(24, sizeof(LogEntry));
  logHistoryMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(
      loggerTask,         // Task-Funktion
      "Logger",           // Name
      4096,               // Stack (String-/Datei-Operationen)
      NULL,               // Parameter
      1,                  // Niedrige Priorität
      &h_loggerTask,      // Handle (Stack-Überwachung)
      0);                 // Auf Core 0 pinnen
}

String logHistorySnapshot() {
  String snapshot;
  if (logHistoryMutex != NULL && xSemaphoreTake(logHistoryMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    snapshot = logHistory;
    xSemaphoreGive(logHistoryMutex);
  }
  return snapshot;
}

/**
 * @brief Schreibt eine Log-Meldung auf Serial und in ein Log-File auf LittleFS.
 * Verwendet printf-ähnliche Formatierung.
 * @param level Das Log-Level (LOG_INFO, LOG_WARN, LOG_ERROR).
 * @param format Der Format-String.
 * @param ... Die variablen Argumente für den Format-String.
 */
void logMessage(LogLevel level, const char* format, ...) {
    LogEntry entry;
    entry.level = level;

    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    snprintf(entry.msg, sizeof(entry.msg), "[%lu][%s] %s\n", millis(), logLevelStrings[level], buf);

    // Nur formatieren + einreihen; die Verarbeitung (Serial/Historie/WS/Flash) macht
    // ausschliesslich der Logger-Task (#4). Kein Warten: volle Queue -> Meldung verwerfen.
    if (logQueue != NULL) {
      if (xQueueSend(logQueue, &entry, 0) != pdTRUE) {
        logDroppedCount = logDroppedCount + 1; // kein ++ auf volatile (C++20-Deprecation)
      }
    } else {
      // Fallback ganz früh im Boot (bevor der Logger-Task existiert): nur Serial.
      if (debugEnabled) Serial.print(entry.msg);
    }
}

/**
 * @brief Verarbeitet einen Log-Eintrag: Serial, RAM-Historie, WebSocket, Flash (nur WARN+).
 * Läuft NUR im Logger-Task (#4) — dadurch sind String/LittleFS/ws-Zugriffe serialisiert.
 */
static void processLogEntry(const LogEntry& entry) {
    // Gib die Meldung auf Serial aus, wenn Debugging aktiviert ist
    if (debugEnabled) {
      Serial.print(entry.msg);
    }

    // RAM-Historie unter Mutex (der WS-Connect-Handler liest sie aus einem anderen Task)
    if (xSemaphoreTake(logHistoryMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      logHistory += entry.msg;
      // Wenn der Puffer zu gross wird, die ältere Hälfte abschneiden
      if (logHistory.length() > MAX_LOG_HISTORY) {
          logHistory = logHistory.substring(logHistory.length() - 2048);
          // Die erste, unvollständige Zeile nach dem Abschneiden bereinigen
          int firstNewLine = logHistory.indexOf('\n');
          if (firstNewLine != -1) {
              logHistory = logHistory.substring(firstNewLine + 1);
          }
      }
      xSemaphoreGive(logHistoryMutex);
    }

    // Live-Log an alle offenen Browser-Fenster senden!
    ws.textAll(entry.msg);

    // Schreibe nur Warnungen und Fehler ins Log-File, um den Flash zu schonen
    if (entry.level >= LOG_WARN) {
      // Öffne die Datei im "Append"-Modus.
      // Der Modus "FILE_APPEND" erstellt die Datei automatisch, falls sie nicht existiert.
      File logFile = LittleFS.open(LOG_FILE, FILE_APPEND);

      if (logFile) {
        logFile.print(entry.msg);

        // Log-Rotation: Wenn die Datei zu gross wird, alte löschen und neue anfangen
        if (logFile.size() > MAX_LOG_SIZE) {
          logFile.close();
          // Lösche zuerst das alte Backup, falls es existiert
          if (LittleFS.exists("/system.log.old")) {
            LittleFS.remove("/system.log.old");
          }
          // Benenne die aktuelle Log-Datei in .old um
          LittleFS.rename(LOG_FILE, "/system.log.old");
        } else {
          logFile.close();
        }
      } else {
        // Dieser Fall sollte selten auftreten, ist aber eine gute Absicherung
        if (debugEnabled) {
          Serial.println("Failed to open log file for writing.");
        }
      }
    }
}

/**
 * @brief FreeRTOS Task: einziger Konsument der Log-Queue (#4).
 * Meldet zusätzlich, wenn Einträge wegen voller Queue verworfen wurden.
 */
static void loggerTask(void *parameter) {
  LogEntry entry;
  for (;;) {
    if (xQueueReceive(logQueue, &entry, portMAX_DELAY) == pdTRUE) {
      processLogEntry(entry);

      // Verworfene Meldungen nachmelden (Zähler ist nur ungefähr — bewusst einfach gehalten)
      uint32_t dropped = logDroppedCount;
      if (dropped > 0) {
        logDroppedCount = 0;
        LogEntry note;
        note.level = LOG_WARN;
        snprintf(note.msg, sizeof(note.msg), "[%lu][WARN] LOGGER: %lu Meldung(en) verworfen (Queue voll)\n",
                 millis(), (unsigned long)dropped);
        processLogEntry(note);
      }
    }
  }
}

// ********************************************************************************
// Display functions
// ********************************************************************************
