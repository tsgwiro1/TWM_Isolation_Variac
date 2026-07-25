// TWM Isolation Variac – Thread-sicheres Logging (#4): Implementation (#10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#include "logging.h"
#include <FS.h>
#include <LittleFS.h>
#include "state.h"
#include "web.h"   // ws.textAll() – Live-Log an offene Browser

static const char* logLevelStrings[] = { "INFO", "WARN", "ERROR" };
volatile bool debugEnabled = true;

// RAM-Puffer für die Log-Historie (ca. 4 KB) — nur für den WS-Connect-Snapshot.
static String logHistory = "";
static const int MAX_LOG_HISTORY = 4096;

// #4: Logging thread-safe — logMessage() formatiert nur noch und legt den Eintrag in eine
// Queue; ein einzelner Logger-Task übernimmt Serial, RAM-Historie, WebSocket und Flash.
struct LogEntry {
  LogLevel level;
  char msg[300];                       // fertig formatierte Zeile inkl. Zeitstempel + '\n'
};
static QueueHandle_t     logQueue = NULL;
static SemaphoreHandle_t logHistoryMutex = NULL;   // schützt logHistory (Logger-Task vs. WS-Connect)
static volatile uint32_t logDroppedCount = 0;      // seit Start verworfene Meldungen (Queue voll)

// #23: Sammelpuffer für die Datei. INFO wird hier akkumuliert und gebündelt geschrieben;
// WARN+ löst einen sofortigen Flush aus (schreibt vorher die gepufferten INFO -> Reihenfolge
// bleibt chronologisch). logFileMutex schützt Puffer + Datei (Logger-Task vs. Download-Handler).
#define FILE_BUFFER_SIZE      8192
#define FLUSH_LINE_THRESHOLD  100
static char   fileBuffer[FILE_BUFFER_SIZE];
static size_t fileBufferLen = 0;
static uint16_t fileBufferLines = 0;
static uint32_t currentFileLines = 0;              // Zeilen in LOG_FILE (für Rotation)
static bool   lineCountKnown = false;              // beim ersten Flush aus der Datei ermittelt
static SemaphoreHandle_t logFileMutex = NULL;

static void processLogEntry(const LogEntry& entry);
static void loggerTask(void *parameter);
static void flushFileBuffer();                     // Aufrufer hält logFileMutex

void loggingInit() {
  // #23: Queue von 24 auf 48 vergrössert. Durch das Batching (WARN+ sofort, INFO gebündelt)
  // leert der Logger-Task schnell, ein Überlauf wird damit sehr selten.
  logQueue = xQueueCreate(48, sizeof(LogEntry));
  logHistoryMutex = xSemaphoreCreateMutex();
  logFileMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(
      loggerTask,         // Task-Funktion
      "Logger",           // Name
      4096,               // Stack (String-/Datei-Operationen)
      NULL,               // Parameter
      1,                  // Niedrige Priorität
      &h_loggerTask,      // Handle (Stack-Überwachung)
      0);                 // Auf Core 0 pinnen
}

uint32_t logDroppedTotal() { return logDroppedCount; }

// Zählt die Zeilen ('\n') einer Datei — einmalig beim ersten Flush, um currentFileLines
// nach einem Neustart korrekt weiterzuführen (LittleFS ist in setup() erst spät gemountet).
static uint32_t countFileLines(const char* path) {
  File f = LittleFS.open(path, FILE_READ);
  if (!f) return 0;
  uint32_t n = 0;
  uint8_t buf[512];
  while (f.available()) {
    int r = f.read(buf, sizeof(buf));
    for (int i = 0; i < r; i++) if (buf[i] == '\n') n++;
  }
  f.close();
  return n;
}

// Hängt eine fertig formatierte Zeile an den Sammelpuffer; läuft der Puffer voll, vorher
// flushen. Aufrufer hält logFileMutex.
static void appendFileLine(const char* line) {
  size_t len = strlen(line);
  if (len >= FILE_BUFFER_SIZE) return;                        // Sicherheitsnetz (kommt nicht vor)
  if (fileBufferLen + len >= FILE_BUFFER_SIZE) flushFileBuffer();
  if (fileBufferLen + len >= FILE_BUFFER_SIZE) return;        // Flush brachte keinen Platz (FS nicht bereit) -> Zeile verwerfen
  memcpy(fileBuffer + fileBufferLen, line, len);
  fileBufferLen += len;
  fileBufferLines++;
}

// Schreibt den Sammelpuffer in einem Rutsch in die Datei und rotiert nach Zeilenzahl.
// Aufrufer hält logFileMutex. Der Puffer wird NUR bei erfolgreichem Schreiben geleert —
// so überleben Zeilen, die vor dem LittleFS-Mount anfallen, bis zum ersten echten Flush.
static void flushFileBuffer() {
  if (fileBufferLen == 0) return;

  File f = LittleFS.open(LOG_FILE, FILE_APPEND);
  if (!f) return;                          // FS noch nicht bereit -> Puffer behalten, später erneut

  // Zeilen der bestehenden Datei einmalig zählen (nach Neustart korrekt weiterführen).
  // Dazu den Append-Handle kurz schliessen — kein paralleler Lese-/Schreibzugriff.
  if (!lineCountKnown) {
    f.close();
    currentFileLines = countFileLines(LOG_FILE);
    lineCountKnown = true;
    f = LittleFS.open(LOG_FILE, FILE_APPEND);
    if (!f) return;                        // nach erfolgreichem Open unerwartet -> Puffer behalten
  }

  f.write((const uint8_t*)fileBuffer, fileBufferLen);
  f.close();
  currentFileLines += fileBufferLines;
  fileBufferLen = 0;
  fileBufferLines = 0;

  // Rotation: aktuelle Datei voll -> altes Backup weg, aktuelle wird zum Backup, neu anfangen.
  if (currentFileLines >= MAX_LOG_LINES) {
    if (LittleFS.exists(LOG_FILE_OLD)) LittleFS.remove(LOG_FILE_OLD);
    LittleFS.rename(LOG_FILE, LOG_FILE_OLD);
    currentFileLines = 0;
  }
}

void logFlushToFile() {
  if (logFileMutex != NULL && xSemaphoreTake(logFileMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    flushFileBuffer();
    xSemaphoreGive(logFileMutex);
  }
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
 * @brief Verarbeitet einen Log-Eintrag: Serial, RAM-Historie, WebSocket, Datei (#23).
 * Läuft NUR im Logger-Task (#4) — dadurch sind String/LittleFS/ws-Zugriffe serialisiert.
 * Datei-Strategie (#23): ALLE Level landen im File, aber gepuffert. WARN+ wird sofort
 * geschrieben (überlebt einen Absturz), INFO gesammelt und alle FLUSH_LINE_THRESHOLD
 * Zeilen in einem Rutsch geflasht — das entlastet den Flash und hält den Task schnell.
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

    // Datei-Handling (#23): puffern; WARN+ oder voller Puffer lösen den Flash-Write aus.
    // Ein WARN+ flusht die davor gesammelten INFO gleich mit -> Reihenfolge bleibt korrekt.
    if (logFileMutex != NULL && xSemaphoreTake(logFileMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
      appendFileLine(entry.msg);
      if (entry.level >= LOG_WARN || fileBufferLines >= FLUSH_LINE_THRESHOLD) {
        flushFileBuffer();
      }
      xSemaphoreGive(logFileMutex);
    }
}

/**
 * @brief FreeRTOS Task: einziger Konsument der Log-Queue (#4).
 * Verworfene Meldungen werden nur noch still gezählt (logDroppedTotal(), sichtbar in
 * /api/status) — die früheren „x verworfen"-Meldungen entfielen (#23), da sie das File
 * fluteten und den Overflow selbst verstärkten.
 */
static void loggerTask(void *parameter) {
  LogEntry entry;
  for (;;) {
    if (xQueueReceive(logQueue, &entry, portMAX_DELAY) == pdTRUE) {
      processLogEntry(entry);
    }
  }
}

// ********************************************************************************
// Display functions
// ********************************************************************************
