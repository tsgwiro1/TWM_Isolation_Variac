// TWM Isolation Variac – Webserver/API (REST, #22) und WebSocket-Live-Log (#10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#ifndef WEB_H
#define WEB_H

#include <ESPAsyncWebServer.h>

extern AsyncWebServer server;
extern AsyncWebSocket ws;
extern AsyncWebSocket wsStatus;  // Live-Statuswerte fürs Dashboard (#13)

// Registriert alle Routen (REST-API + statische Dateien) und startet den Server.
void initWebServer();
// Pusht den Status an alle /ws_status-Clients; periodisch aus loop() aufrufen (#13).
void webPushStatus();

#endif // WEB_H
