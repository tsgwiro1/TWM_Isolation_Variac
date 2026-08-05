// TWM Isolation Variac – Webserver/API (REST, #22) und WebSocket-Live-Log (#10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#include "web.h"
#include <memory>   // shared_ptr für die verkettete Log-Auslieferung (#23)
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <Ticker.h>
#include "pins.h"
#include "state.h"
#include "logging.h"
#include "config.h"
#include "motor.h"
#include "comm.h"
#include "actions.h"
#include "system.h"   // wiperTemp in /api/status

AsyncWebServer server(80); // Server auf Port 80 erstellen
AsyncWebSocket ws("/ws");
AsyncWebSocket wsStatus("/ws_status"); // Live-Statuswerte fürs Dashboard (#13)
static Ticker rebootTicker;

/**
 * @brief Baut das Status-JSON (eine Quelle für GET /api/status und den
 * WebSocket-Push /ws_status, #13).
 */
static String buildStatusJson() {
  JsonDocument doc;

  doc["voltage_actual"] = received_rms_value;
  doc["voltage_fresh"] = isVoltageDataFresh();
  doc["voltage_setpoint"] = setpoint_voltage;
  doc["temperature"] = wiperTemp;
  doc["stepper_position"] = wiperPos;
  doc["is_hardware_ok"] = hardwareInitialized;
  doc["fw_version"] = FW;
  doc["log_dropped"] = logDroppedTotal();   // #23: still gezählte, wegen voller Queue verworfene Meldungen

  JsonObject states = doc["states"].to<JsonObject>();
  if (hardwareInitialized) {
    states["output_on"] = (bool)A_onoff->getState();
    states["limit_on"] = (bool)A_limit->getState();
    states["regulation_on"] = (bool)A_reg->getState();
    // Preset-Tasten-Zustände (ehemals in /data; /data ist in /api/status aufgegangen, #22)
    states["p1_on"] = (bool)A_p1->getState();
    states["p2_on"] = (bool)A_p2->getState();
    states["p3_on"] = (bool)A_p3->getState();
    // Preset-Werte (GitHub-#15): im 500-ms-Push mitschicken, damit Dashboard und
    // Einstellungsseite live folgen, wenn ein Preset am Gerät neu gespeichert wird.
    JsonObject presets = doc["presets"].to<JsonObject>();
    presets["p1"] = A_p1->getValuePreset();
    presets["p2"] = A_p2->getValuePreset();
    presets["p3"] = A_p3->getValuePreset();
  }

  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}

/**
 * @brief Pusht den aktuellen Status an alle /ws_status-Clients (#13).
 * Wird periodisch aus loop() aufgerufen (Haupttask — textAll ist dort sicher).
 * Räumt nebenbei tote WebSocket-Clients beider Endpunkte ab.
 */
void webPushStatus() {
  ws.cleanupClients();
  wsStatus.cleanupClients();
  if (wsStatus.count() > 0) {
    wsStatus.textAll(buildStatusJson());
  }
}

/**
 * @brief Weist eine Anfrage mit 503 ab, solange die Bedienung gesperrt ist (GitHub-#26).
 * Gesperrt wird während eines OTA- oder Voltmeter-FW-Updates — dann darf weder die
 * Webseite noch die API den Zustand des Geräts ändern. Lesende Routen bleiben offen.
 * @return true, wenn abgewiesen wurde (der Handler muss dann sofort zurückkehren).
 */
static bool rejectIfLocked(AsyncWebServerRequest *request) {
  if (!controlsLocked()) return false;
  request->send(503, "application/json",
                "{\"status\":\"error\",\"message\":\"Locked: update in progress\"}");
  return true;
}

/**
 * @brief Initialisiert den Webserver und definiert alle Routen (URLs).
 */
void initWebServer() {
  // Liefere alle statischen Dateien (.html, .css, .js) automatisch aus dem Root-Verzeichnis von LittleFS
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // API-Route für die Datenabfrage ("/data"): Liefere IST/SOLL und Tasten-Zustände als JSON
  // API-Route zum Setzen des Sollwerts (POST /api/setpoint?voltage=...) (#22)
  server.on("/api/setpoint", HTTP_POST, [] (AsyncWebServerRequest *request) {
    if (rejectIfLocked(request)) return;   // GitHub-#26
    if (!hardwareInitialized) {
      request->send(503, "application/json", "{\"status\":\"error\",\"message\":\"Hardware not ready\"}");
      return;
    }
    if (request->hasParam("voltage")) {
      float new_voltage = request->getParam("voltage")->value().toFloat();
      logMessage(LOG_INFO, "API: New setpoint received -> %.1f V", new_voltage);
      resetPresetActions();
      setpoint_voltage = constrain(new_voltage, (float)MIN_VOLTAGE_TARGET, (float)maxVoltageTarget());
      isRecallPreset = true;
      request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Setpoint updated\"}");
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing 'voltage' parameter\"}");
    }
  });

  // API-Route zum Auslösen von Aktionen (POST /api/command?action=...) (#22)
  server.on("/api/command", HTTP_POST, [] (AsyncWebServerRequest *request) {
    if (rejectIfLocked(request)) return;   // GitHub-#26
    if (!hardwareInitialized) {
      request->send(503, "application/json", "{\"status\":\"error\",\"message\":\"Hardware not ready\"}");
      return;
    }
    if (request->hasParam("action")) {
      String action = request->getParam("action")->value();
      logMessage(LOG_INFO, "API: Command received -> %s", action.c_str());
      
      if (action == "toggle_output") { A_onoff->toggle(); }
      else if (action == "toggle_limit") { A_limit->toggle(); }
      else if (action == "toggle_regulation") { toggleRegulation(); }   // GitHub-#27: Feedforward mit anstossen
      else if (action == "recall_p1") { cb_ValueAction(A_p1, ButtonEvent::RELEASED); }
      else if (action == "recall_p2") { cb_ValueAction(A_p2, ButtonEvent::RELEASED); }
      else if (action == "recall_p3") { cb_ValueAction(A_p3, ButtonEvent::RELEASED); }
      else if (action == "enter_settings") { requestEnterSettingsMode = true; is_regulation_active = false; } 
      else {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Unknown action\"}");
        return;
      }
      request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Command executed\"}");
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing 'action' parameter\"}");
    }
  });

  // API-Route für den kompletten Gerätestatus (gleiche Quelle wie /ws_status, #13)
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "application/json", buildStatusJson());
  });

  // API-Route: Voltmeter-Version über den seriellen Link abfragen (Paket J)
  server.on("/api/voltmeter/version", HTTP_GET, [](AsyncWebServerRequest *request){
    if (voltmeterRequest(VM_CMD_GET_VERSION, nullptr, 0, 400)) {
      char ver[65];
      uint8_t n = voltmeterResponseLen < 64 ? voltmeterResponseLen : 64;
      memcpy(ver, voltmeterResponsePayload, n);
      ver[n] = '\0';
      JsonDocument doc;
      doc["status"] = "success";
      doc["version"] = ver;
      String out; serializeJson(doc, out);
      request->send(200, "application/json", out);
    } else {
      request->send(504, "application/json", "{\"status\":\"error\",\"message\":\"No response from voltmeter\"}");
    }
  });

  // API-Route: Voltmeter-Status (Skalierungsfaktor, Spannungs-Offset, ADC-Nullpunkt)
  server.on("/api/voltmeter/status", HTTP_GET, [](AsyncWebServerRequest *request){
    if (voltmeterRequest(VM_CMD_GET_STATUS, nullptr, 0, 400) && voltmeterResponseLen >= 12) {
      float factor, voff, adcz;
      memcpy(&factor, voltmeterResponsePayload + 0, 4);
      memcpy(&voff,   voltmeterResponsePayload + 4, 4);
      memcpy(&adcz,   voltmeterResponsePayload + 8, 4);
      JsonDocument doc;
      doc["status"] = "success";
      doc["scaling_factor"] = factor;
      doc["voltage_offset"] = voff;
      doc["adc_zero_offset"] = adcz;
      String out; serializeJson(doc, out);
      request->send(200, "application/json", out);
    } else {
      request->send(504, "application/json", "{\"status\":\"error\",\"message\":\"No response from voltmeter\"}");
    }
  });

  // API-Route: Skalierungsfaktor des Voltmeters setzen (+ EEPROM speichern)
  server.on("/api/voltmeter/factor", HTTP_POST, [](AsyncWebServerRequest *request){
    if (rejectIfLocked(request)) return;   // GitHub-#26
    if (!request->hasParam("value")) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing 'value' parameter\"}");
      return;
    }
    float v = request->getParam("value")->value().toFloat();
    uint8_t b[4];
    memcpy(b, &v, 4);
    if (voltmeterRequest(VM_CMD_SET_FACTOR, b, 4, 400)) {
      bool ok = (voltmeterResponseLen >= 1 && voltmeterResponsePayload[0] == 1);
      if (ok) {
        request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Factor set\"}");
      } else {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Factor rejected (range 100..1000)\"}");
      }
    } else {
      request->send(504, "application/json", "{\"status\":\"error\",\"message\":\"No response from voltmeter\"}");
    }
  });

  // API-Route: Spannungs-Offset des Voltmeters setzen (+ EEPROM speichern)
  server.on("/api/voltmeter/offset", HTTP_POST, [](AsyncWebServerRequest *request){
    if (rejectIfLocked(request)) return;   // GitHub-#26
    if (!request->hasParam("value")) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing 'value' parameter\"}");
      return;
    }
    float v = request->getParam("value")->value().toFloat();
    uint8_t b[4];
    memcpy(b, &v, 4);
    if (voltmeterRequest(VM_CMD_SET_OFFSET, b, 4, 400)) {
      bool ok = (voltmeterResponseLen >= 1 && voltmeterResponsePayload[0] == 1);
      if (ok) {
        request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Offset set\"}");
      } else {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Offset rejected (range -50..50)\"}");
      }
    } else {
      request->send(504, "application/json", "{\"status\":\"error\",\"message\":\"No response from voltmeter\"}");
    }
  });

  // API-Route: Auto-Zero-Kalibrierung des Voltmeters starten (läuft danach mehrere Sekunden)
  server.on("/api/voltmeter/autozero", HTTP_POST, [](AsyncWebServerRequest *request){
    if (rejectIfLocked(request)) return;   // GitHub-#26
    if (voltmeterRequest(VM_CMD_RECAL, nullptr, 0, 400)) {
      request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Auto-zero started (takes a few seconds)\"}");
    } else {
      request->send(504, "application/json", "{\"status\":\"error\",\"message\":\"No response from voltmeter\"}");
    }
  });

  // API-Route: 3-Punkt-Kalibrierung – einen Punkt messen (index + anliegende Referenzspannung)
  server.on("/api/voltmeter/cal3/measure", HTTP_POST, [](AsyncWebServerRequest *request){
    if (rejectIfLocked(request)) return;   // GitHub-#26
    if (!request->hasParam("index") || !request->hasParam("voltage")) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing 'index' or 'voltage'\"}");
      return;
    }
    uint8_t payload[5];
    payload[0] = (uint8_t)request->getParam("index")->value().toInt();
    float v = request->getParam("voltage")->value().toFloat();
    memcpy(payload + 1, &v, 4);
    // Messung mittelt ~2 s -> längerer Timeout.
    if (voltmeterRequest(VM_CMD_CAL3_MEASURE, payload, 5, 3000)) {
      bool ok = (voltmeterResponseLen >= 1 && voltmeterResponsePayload[0] == 1);
      request->send(ok ? 200 : 400, "application/json",
                    ok ? "{\"status\":\"success\",\"message\":\"Point measured\"}"
                       : "{\"status\":\"error\",\"message\":\"Invalid point\"}");
    } else {
      request->send(504, "application/json", "{\"status\":\"error\",\"message\":\"No response from voltmeter\"}");
    }
  });

  // API-Route: Voltmeter neu starten (Soft-Reset)
  server.on("/api/voltmeter/reboot", HTTP_POST, [](AsyncWebServerRequest *request){
    if (rejectIfLocked(request)) return;   // GitHub-#26
    if (voltmeterRequest(VM_CMD_REBOOT, nullptr, 0, 400)) {
      request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Voltmeter rebooting\"}");
    } else {
      request->send(504, "application/json", "{\"status\":\"error\",\"message\":\"No response from voltmeter\"}");
    }
  });

  // API-Route: Voltmeter-Kalibrierung auf Standardwerte zurücksetzen
  server.on("/api/voltmeter/reset-defaults", HTTP_POST, [](AsyncWebServerRequest *request){
    if (rejectIfLocked(request)) return;   // GitHub-#26
    if (voltmeterRequest(VM_CMD_RESET_DEFAULTS, nullptr, 0, 400)) {
      request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Calibration reset to defaults\"}");
    } else {
      request->send(504, "application/json", "{\"status\":\"error\",\"message\":\"No response from voltmeter\"}");
    }
  });

  // API-Route: 3-Punkt-Kalibrierung abschließen – Regression rechnen + speichern
  server.on("/api/voltmeter/cal3/finish", HTTP_POST, [](AsyncWebServerRequest *request){
    if (rejectIfLocked(request)) return;   // GitHub-#26
    if (voltmeterRequest(VM_CMD_CAL3_FINISH, nullptr, 0, 600) && voltmeterResponseLen >= 9) {
      bool ok = (voltmeterResponsePayload[0] == 1);
      if (ok) {
        float factor, voff;
        memcpy(&factor, voltmeterResponsePayload + 1, 4);
        memcpy(&voff,   voltmeterResponsePayload + 5, 4);
        JsonDocument doc;
        doc["status"] = "success";
        doc["scaling_factor"] = factor;
        doc["voltage_offset"] = voff;
        String out; serializeJson(doc, out);
        request->send(200, "application/json", out);
      } else {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Need at least 2 measured points\"}");
      }
    } else {
      request->send(504, "application/json", "{\"status\":\"error\",\"message\":\"No response from voltmeter\"}");
    }
  });

  // --- Voltmeter-FW-Update (#30) ---
  // Upload der .bin nach LittleFS (POST multipart). Antwort kommt nach dem Upload.
  server.on("/api/voltmeter/update/upload", HTTP_POST,
    [](AsyncWebServerRequest *request){
    if (rejectIfLocked(request)) return;   // GitHub-#26
      request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Upload complete\"}");
    },
    [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final){
      static File up;
      // Während eines laufenden Voltmeter- oder OTA-Updates nichts annehmen (GitHub-#26)
      if (controlsLocked()) return;
      if (index == 0) up = LittleFS.open(VM_FW_PATH, "w");
      if (up) up.write(data, len);
      if (final && up) up.close();
    });

  // Update starten: prüft Datei, stößt den Update-Task an.
  server.on("/api/voltmeter/update/start", HTTP_POST, [](AsyncWebServerRequest *request){
    // 409 zuerst: für ein bereits laufendes Voltmeter-Update ist das die genauere Antwort
    // (die UI unterscheidet sie). rejectIfLocked greift danach für den OTA-Fall (GitHub-#26).
    if (vmUpdateState == VMU_RUNNING) {
      request->send(409, "application/json", "{\"status\":\"error\",\"message\":\"Update already running\"}");
      return;
    }
    if (rejectIfLocked(request)) return;   // GitHub-#26
    if (!LittleFS.exists(VM_FW_PATH)) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"No firmware uploaded\"}");
      return;
    }
    // Diagnose: ?skipenter=1 überspringt ENTER_BOOTLOADER (VM bereits via BOOT0 im ROM-Loader).
    vmUpdateSkipEnter = request->hasParam("skipenter") && request->getParam("skipenter")->value() == "1";
    vmUpdSet(VMU_RUNNING, 0, "Update gestartet...");
    vmUpdateRequested = true;
    request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Update started\"}");
  });

  // Update-Fortschritt/Status abfragen (UI pollt diese Route).
  server.on("/api/voltmeter/update/status", HTTP_GET, [](AsyncWebServerRequest *request){
    const char* st = vmUpdateState == VMU_RUNNING ? "running"
                   : vmUpdateState == VMU_SUCCESS ? "success"
                   : vmUpdateState == VMU_ERROR   ? "error" : "idle";
    JsonDocument doc;
    doc["state"]    = st;
    doc["progress"] = vmUpdateProgress;
    doc["message"]  = vmUpdateMessage;
    String out; serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  // Version der auf LittleFS liegenden .bin (aus dem Magic-Tag). (#33)
  server.on("/api/voltmeter/update/fileversion", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    char ver[48];
    if (LittleFS.exists(VM_FW_PATH) && readVmFwFileVersion(ver, sizeof(ver))) {
      doc["status"]  = "success";
      doc["version"] = ver;
    } else {
      doc["status"] = "none"; // keine Datei oder kein Tag
    }
    String out; serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  // #22: /api/presets (GET), /api/presets/save und /api/calibration (GET) entfernt —
  // die Werte stecken vollständig in /api/config; Presets speichern läuft über
  // POST /api/config oder die Gerätetasten.

  // API-Route zum Setzen eines Kalibrier-Endanschlags (POST /api/calibration?limit=min|max[&value=]) (#22)
  server.on("/api/calibration", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (rejectIfLocked(request)) return;   // GitHub-#26
    // 1. Zuerst prüfen, ob die Hardware überhaupt bereit ist
    if (!hardwareInitialized) {
        request->send(503, "application/json", "{\"status\":\"error\",\"message\":\"Hardware not ready\"}");
        return;
    }

    // 2. Prüfen, ob der 'limit'-Parameter vorhanden ist
    if (request->hasParam("limit")) {
        String limitType = request->getParam("limit")->value();
        int valueToSave;

        // Prüfe, ob ein Wert explizit mitgegeben wurde
        if (request->hasParam("value")) {
            valueToSave = request->getParam("value")->value().toInt();
        } else {
            valueToSave = stepper.currentPosition();
        }

        if (limitType == "min") {
            portENTER_CRITICAL(&calibMux);
            minWiperPos = valueToSave;
            portEXIT_CRITICAL(&calibMux);
            logMessage(LOG_WARN, "API: NEW MIN LIMIT calibrated -> %d steps", valueToSave);
            saveConfiguration();
        }
        else if (limitType == "max") {
            portENTER_CRITICAL(&calibMux);
            maxWiperPos = valueToSave;
            portEXIT_CRITICAL(&calibMux);
            logMessage(LOG_WARN, "API: NEW MAX LIMIT calibrated -> %d steps", valueToSave);
            saveConfiguration();
        }
        else {
            // Fehler: Ungültiger limit-Typ
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid limit type. Use 'min' or 'max'.\"}");
            return;
        }
		
        // Erfolgs-Antwort im JSON-Format
        request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Calibration point saved\"}");

    } else {
        // Fehler: Fehlender 'limit'-Parameter
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing 'limit' parameter\"}");
    }
  });

  // API-Route für einen Neustart des Geräts (POST, #22)
  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *request){
    if (rejectIfLocked(request)) return;   // GitHub-#26
    // Sende die Bestätigung an den Client.
    request->send(200, "text/plain", "Rebooting in 200ms...");
    
    logMessage(LOG_INFO, "API: Reboot requested!");

    // Verabschiede alle WebSocket-Clients sauber
    ws.closeAll();
    wsStatus.closeAll();

    // Starte einen einmaligen Timer, der den Neustart nach 500ms auslöst.
    // Die Funktion kehrt sofort zurück, damit die HTTP-Antwort in der Zwischenzeit gesendet
    // werden kann. GitHub-#26: Vor dem Reset die Logdatei sichern und das Dateisystem
    // schliessen — sonst können die letzten Zeilen als gelöschtes Flash (0xFF) in der Datei
    // landen, weil LittleFS seine Metadaten noch nicht durchgeschrieben hat.
    rebootTicker.once_ms(500, [](){
      logFlushToFile();
      LittleFS.end();
      ESP.restart();
    });
  });

  // API-Route zum Abrufen des Logs (#23): liefert das Backup (.old) und die aktuelle
  // Datei nacheinander als eine zusammenhängende Datei aus (bis ~2×MAX_LOG_LINES Zeilen).
  server.on("/api/log", HTTP_GET, [](AsyncWebServerRequest *request){
    // Erst die gepufferten INFO-Zeilen in die Datei schreiben, damit der Download vollständig ist.
    logFlushToFile();

    // Beide Dateien offen halten und über eine Chunked-Response nacheinander streamen.
    // shared_ptr hält die Handles am Leben, bis die Antwort fertig gesendet ist.
    auto oldF = std::make_shared<File>();
    auto curF = std::make_shared<File>();
    if (LittleFS.exists(LOG_FILE_OLD)) *oldF = LittleFS.open(LOG_FILE_OLD, FILE_READ);
    if (LittleFS.exists(LOG_FILE))     *curF = LittleFS.open(LOG_FILE, FILE_READ);

    bool haveOld = (*oldF && oldF->available());
    bool haveCur = (*curF && curF->available());
    if (!haveOld && !haveCur) {
      request->send(404, "application/json", "{\"status\":\"error\",\"message\":\"Log file not found\"}");
      return;
    }

    AsyncWebServerResponse *response = request->beginChunkedResponse("text/plain",
      [oldF, curF](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        // GitHub-#26: 0xFF-Bytes herausfiltern. Nach einem harten Reset (Stromausfall
        // mitten im Schreiben) kann die Datei Bereiche mit gelöschtem Flash enthalten;
        // die landeten sonst als Müll im Download. 0xFF kommt in gültigem Text nicht vor.
        // Wichtig: notfalls nachlesen, denn ein Rückgabewert 0 beendet die Übertragung —
        // ein reiner Füllbyte-Block darf den Rest der Datei nicht abschneiden.
        while (true) {
          size_t got = 0;
          if (*oldF && oldF->available()) got = oldF->read(buffer, maxLen);   // zuerst das Backup
          if (got == 0 && *curF && curF->available()) got = curF->read(buffer, maxLen); // dann aktuell
          if (got == 0) return 0;   // beide Dateien zu Ende -> vollständig
          size_t keep = 0;
          for (size_t i = 0; i < got; i++) {
            if (buffer[i] != 0xFF) buffer[keep++] = buffer[i];
          }
          if (keep > 0) return keep;
        }
      });

    if (request->hasParam("download")) {
      response->addHeader("Content-Disposition", "attachment; filename=system.log");
    }
    request->send(response);
  });

  // API-Route zum Löschen einer Datei (DELETE /api/files?filename=/x.y) (#22)
  server.on("/api/files", HTTP_DELETE, [](AsyncWebServerRequest *request) {
    if (rejectIfLocked(request)) return;   // GitHub-#26
    if (request->hasParam("filename")) {
      String filename = request->getParam("filename")->value();
      
      // Kleiner Security-Check: Erlaube nur das Löschen von Dateien im Root-Verzeichnis
      if (!filename.startsWith("/") || filename.indexOf('/', 1) != -1) {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid filename\"}");
        return;
      }
      
      if (LittleFS.exists(filename)) {
        if (LittleFS.remove(filename)) {
          logMessage(LOG_WARN, "API: File deleted -> %s", filename.c_str());
          request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"File deleted\"}");
        } else {
          request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to delete file\"}");
        }
      } else {
        request->send(404, "application/json", "{\"status\":\"error\",\"message\":\"File not found\"}");
      }
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing 'filename' parameter\"}");
    }
  });

  // API-Route, um alle Dateien im LittleFS aufzulisten
  server.on("/api/files", HTTP_GET, [](AsyncWebServerRequest *request){
    // Ein JSON-Dokument erstellen. 1024 Bytes sollte für ca. 20-25 Dateien reichen.
    JsonDocument doc;
    JsonArray files = doc.to<JsonArray>();

    File root = LittleFS.open("/");
    File file = root.openNextFile();

    while(file){
      if (!file.isDirectory()) {
        JsonObject fileObj = files.add<JsonObject>();
        fileObj["name"] = String(file.name());
        fileObj["size"] = file.size();
      }
      file = root.openNextFile();
    }

    String jsonString;
    serializeJson(doc, jsonString);
    request->send(200, "application/json", jsonString);
  });

  // API-Route zum Auslesen der Konfiguration (aus dem NVS, #35) inkl. Download-Option
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request){
    String cfg = configRawJson();

    if (!cfg.isEmpty()) {
      // Prüfe, ob der Download-Parameter gesetzt ist
      if (request->hasParam("download")) {
        // JA: Sende den Inhalt als Anhang (löst den Download im Browser aus)
        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", cfg);
        response->addHeader("Content-Disposition", "attachment; filename=\"config.json\"");
        request->send(response);
      } else {
        // NEIN: Sende den Inhalt normal zur Anzeige im Browser
        request->send(200, "application/json", cfg);
      }
    } else {
      request->send(404, "application/json", "{\"status\":\"error\",\"message\":\"Config not found\"}");
    }
  });

  // Handler für das Schreiben/Aktualisieren der Konfiguration
  AsyncCallbackJsonWebHandler* handler = new AsyncCallbackJsonWebHandler("/api/config", [](AsyncWebServerRequest *request, JsonVariant &json) {
    if (rejectIfLocked(request)) return;   // GitHub-#26
    JsonObject doc;
    // Prüfe, ob der Body valides JSON ist
    if (json.is<JsonObject>()) {
      doc = json.as<JsonObject>();
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON body\"}");
      return;
    }
    
    // Rufe die zentrale Validierungs-Funktion auf
    String validationErrorJson = applyAndValidateConfig(doc);

    if (validationErrorJson.isEmpty()) {
      // Erfolg: Speichere die neuen, validierten Werte und sende Erfolgsmeldung
      saveConfiguration();
      logMessage(LOG_INFO, "Configuration updated via API.");
      request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Configuration updated and saved\"}");
    } else {
      // Fehler: Sende eine 400 Bad Request Antwort mit dem detaillierten Fehler-JSON
      String response = "{\"status\":\"error\",\"validation_errors\":" + validationErrorJson + "}";
      request->send(400, "application/json", response);
    }
  });
  server.addHandler(handler);
 
  // Handler für nicht gefundene Seiten (404)
  server.onNotFound([](AsyncWebServerRequest *request){
    // Prüfe, ob die Anfrage an die API gerichtet war
    if (request->url().startsWith("/api/")) {
      // Wenn ja, antworte mit JSON
      request->send(404, "application/json", "{\"status\":\"error\",\"message\":\"Endpoint not found\"}");
    } else {
      // Wenn nein (z.B. eine fehlende .css Datei), antworte mit einfachem Text
      request->send(404, "text/plain", "Not found");
    }
  });

  // Event-Handler für den WebSocket
  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len){
      if (type == WS_EVT_CONNECT) {
          // Ein neuer Client (Browser) hat sich verbunden!
          // Schicke ihm sofort die gesamte gespeicherte Historie aus dem RAM.
          // Snapshot unter Mutex — der Logger-Task (#4) verändert logHistory parallel.
          client->text(logHistorySnapshot());
      }
  });

  // Status-WebSocket (#13): beim Verbinden sofort einen Status schicken,
  // damit das Dashboard ohne Wartezeit rendert; danach pusht loop() periodisch.
  wsStatus.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len){
      if (type == WS_EVT_CONNECT) {
          client->text(buildStatusJson());
      }
  });

  // WebSockets an den Server binden
  server.addHandler(&ws);
  server.addHandler(&wsStatus);

  // Starte den Server
  server.begin();
  logMessage(LOG_INFO,"Web server started.");
}


