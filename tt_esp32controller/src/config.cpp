// TWM Isolation Variac – Konfiguration: NVS-Store (#35) + Validierung (#10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#include "config.h"
#include <FS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include "state.h"
#include "logging.h"
#include "actions.h"

// File und Standardwerte für die Settings
#define CONFIG_FILE "/config.json"   // nur noch für die einmalige Migration ins NVS (#35)

// #35: Konfiguration liegt im NVS (eigene Flash-Partition) und überlebt damit jedes
// uploadfs (Webseiten-Update) und jede App-OTA. Gespeichert wird der komplette
// Config-JSON-String unter einem Key (Schema bleibt allein applyAndValidateConfig()).
#define NVS_NAMESPACE  "twm"
#define NVS_KEY_CONFIG "config"
#define DEFAULT_WIPER_MIN 0
#define DEFAULT_WIPER_MAX 2000
#define DEFAULT_VOLTAGE_PRESET 0
#define DEFAULT_VOLTAGE_AT_MIN_POS 3.0f
#define DEFAULT_VOLTAGE_AT_MAX_POS 255.0f
#define DEFAULT_REG_DEADBAND_V    1.0f
#define DEFAULT_REG_DAMPING       0.8f
#define DEFAULT_REG_SETTLE_MS     150
#define DEFAULT_REG_UNDERSHOOT_V  5.0f
#define DEFAULT_DEBUG true

// Liefert den rohen Config-JSON-String aus dem NVS (leer, wenn keiner existiert).
String configRawJson() {
  Preferences prefs;
  String cfg;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    cfg = prefs.getString(NVS_KEY_CONFIG, "");
    prefs.end();
  }
  return cfg;
}


/**
 * @brief Setzt eine Standard Konfiguration, damit das System lauffähig ist.
 * Es werden alle globalen Variabeln initialisiert, welche sonst aus dem config.json File gesetzt werden.
 */
void applyDefaultConfiguration() {
    // Standardwerte in die laufenden Variablen laden (Kalibrier-Satz atomar, #5)
    portENTER_CRITICAL(&calibMux);
    minWiperPos = DEFAULT_WIPER_MIN;
    maxWiperPos = DEFAULT_WIPER_MAX;
    minVoltageAtMinPos = DEFAULT_VOLTAGE_AT_MIN_POS;
    maxVoltageAtMaxPos = DEFAULT_VOLTAGE_AT_MAX_POS;
    portEXIT_CRITICAL(&calibMux);
    reg_deadband_v   = DEFAULT_REG_DEADBAND_V;
    reg_damping      = DEFAULT_REG_DAMPING;
    reg_settle_ms    = DEFAULT_REG_SETTLE_MS;
    reg_undershoot_v = DEFAULT_REG_UNDERSHOOT_V;
    debugEnabled = DEFAULT_DEBUG;
    if (hardwareInitialized) {
      A_p1->setValuePreset(DEFAULT_VOLTAGE_PRESET);
      A_p2->setValuePreset(DEFAULT_VOLTAGE_PRESET);
      A_p3->setValuePreset(DEFAULT_VOLTAGE_PRESET);
    }
}

/**
 * @brief Validiert und wendet eine neue Konfiguration an.
 * Prüft jeden Wert aus dem übergebenen JSON-Dokument auf Gültigkeit.
 * @param doc Das JsonObject, das die neue Konfiguration enthält.
 * @return String Ein JSON-String mit Fehlermeldungen. Ist leer bei Erfolg.
 */
String applyAndValidateConfig(JsonObject doc) {
  JsonDocument errorDoc;
  JsonObject errors = errorDoc.to<JsonObject>();

  // Temporäre Variablen, um Abhängigkeiten korrekt zu prüfen
  int tempMinPos = minWiperPos;
  int tempMaxPos = maxWiperPos;
  float tempMinVoltage = minVoltageAtMinPos;
  float tempMaxVoltage = maxVoltageAtMaxPos;
  bool tempDebugEnabled = debugEnabled;
  float    tempRegDeadband   = reg_deadband_v;
  float    tempRegDamping    = reg_damping;
  uint32_t tempRegSettle     = reg_settle_ms;
  float    tempRegUndershoot = reg_undershoot_v;

  bool calibrationHasErrors = false;

  // --- Kalibrierung validieren ---
  if (!doc["calibration"].isNull()) {
    JsonObject calibration = doc["calibration"];

    if (!calibration["min_pos"].is<int>()) {
        errors["calibration_min_pos"] = "must be an integer";
        calibrationHasErrors = true;
    } else {
      if (!calibration["min_pos"].isNull()) {
        int val = calibration["min_pos"];
        if (val > 0 || val < MINWIPERLIMIT) {
          errors["calibration_min_pos"] = "must be between " + String(MINWIPERLIMIT) + " and 0";
          calibrationHasErrors = true;
        } else {
          tempMinPos = val;
        }
      }
    }

    // max_pos validieren (nur wenn min_pos gültig ist)
    if (!calibration["max_pos"].isNull() && errors["calibration_min_pos"].isNull()) {
      if (!calibration["max_pos"].is<int>()) {
        errors["calibration_max_pos"] = "must be an integer";
        calibrationHasErrors = true;
      } else {      
        int val = calibration["max_pos"];
        if (val <= tempMinPos) {
          errors["calibration_max_pos"] = "must be greater than min_pos (" + String(tempMinPos) + ")";
          calibrationHasErrors = true;
        } else if (val > MAXWIPERLIMIT) {
          errors["calibration_max_pos"] = "cannot be greater than " + String(MAXWIPERLIMIT);
          calibrationHasErrors = true;
        } else {
          tempMaxPos = val;
        }
      }
    }

    // min_voltage validieren
    if (!calibration["min_voltage"].isNull()) {
      if (!calibration["min_voltage"].is<float>()) {
        errors["calibration_min_voltage"] = "must be a float";
        calibrationHasErrors = true;
      } else {
        float val = calibration["min_voltage"];
        if (val < 0) {
          errors["calibration_min_voltage"] = "cannot be negative";
          calibrationHasErrors = true;
        } else {
          tempMinVoltage = val;
        }
      }
    }

    // max_voltage validieren (nur wenn min_voltage gültig ist)
    if (!calibration["max_voltage"].isNull() && errors["calibration_min_voltage"].isNull()) {
      if (!calibration["max_voltage"].is<float>()) {
        errors["calibration_max_voltage"] = "must be a float";
        calibrationHasErrors = true;
      } else {
        float val = calibration["max_voltage"];
        if (val <= tempMinVoltage) {
          errors["calibration_max_voltage"] = "must be greater than min_voltage (" + String(tempMinVoltage, 1) + ")";
          calibrationHasErrors = true;
        } else {
          tempMaxVoltage = val;
        }
      }
    }
  }

  // --- Presets validieren ---
  if (!doc["presets"].isNull()) {
    if (calibrationHasErrors) {
      errors["presets"] = "Cannot validate presets due to errors in calibration section.";
    } else {
      JsonObject presets = doc["presets"];
      if (!presets["p1"].isNull() && presets["p1"].is<int>()) {
        if (presets["p1"] < 0 || presets["p1"] > (int)tempMaxVoltage) {
          errors["preset_p1"] = "must be between 0 and " + String((int)tempMaxVoltage);
        }
      } else {
        errors["preset_p1"] = "must be an integer";
      }
      if (!presets["p2"].isNull() && presets["p2"].is<int>()) {
        if (presets["p2"] < 0 || presets["p2"] > (int)tempMaxVoltage) {
          errors["preset_p2"] = "must be between 0 and " + String((int)tempMaxVoltage);
        }
      } else {
        errors["preset_p2"] = "must be an integer";
      }
      if (!presets["p3"].isNull() && presets["p3"].is<int>()) {
        if (presets["p3"] < 0 || presets["p3"] > (int)tempMaxVoltage) {
          errors["preset_p3"] = "must be between 0 and " + String((int)tempMaxVoltage);
        }
      } else {
        errors["preset_p3"] = "must be an integer";
      }
    }
  }

  // --- System validieren (und anwenden, da unkritisch) ---
  if (!doc["system"].isNull()) {
    JsonObject system = doc["system"];

    // debug_enabled validieren
    if (!system["debug_enabled"].isNull()) {
        // Prüfe, ob der Wert ein Boolean ist
        if (system["debug_enabled"].is<bool>()) {
            tempDebugEnabled = system["debug_enabled"];
        } else {
            // Wenn nicht, füge eine Fehlermeldung hinzu
            errors["debug_enabled"] = "must be a boolean (true or false)";
        }
    }
  }

  // --- Regelparameter validieren (#31; alle optional, mit Plausibilitätsgrenzen) ---
  if (!doc["regulation"].isNull()) {
    JsonObject regulation = doc["regulation"];

    if (!regulation["deadband_v"].isNull()) {
      float val = regulation["deadband_v"] | -1.0f;
      if (val < 0.1f || val > 10.0f) errors["reg_deadband_v"] = "must be 0.1 .. 10.0 V";
      else tempRegDeadband = val;
    }
    if (!regulation["damping"].isNull()) {
      float val = regulation["damping"] | -1.0f;
      if (val < 0.1f || val > 1.0f) errors["reg_damping"] = "must be 0.1 .. 1.0";
      else tempRegDamping = val;
    }
    if (!regulation["settle_ms"].isNull()) {
      int val = regulation["settle_ms"] | -1;
      if (val < 50 || val > 2000) errors["reg_settle_ms"] = "must be 50 .. 2000 ms";
      else tempRegSettle = (uint32_t)val;
    }
    if (!regulation["undershoot_v"].isNull()) {
      float val = regulation["undershoot_v"] | -1.0f;
      if (val < 0.0f || val > 20.0f) errors["reg_undershoot_v"] = "must be 0 .. 20 V";
      else tempRegUndershoot = val;
    }
  }

  // --- Finale Entscheidung ---
  if (errors.size() == 0) {
    // KEINE FEHLER: Wende die validierten Werte auf die globalen Variablen an.
    // Kalibrier-Satz atomar schreiben (#5) — Leser (Regelung) sehen nie einen Mischzustand.
    portENTER_CRITICAL(&calibMux);
    minWiperPos = tempMinPos;
    maxWiperPos = tempMaxPos;
    minVoltageAtMinPos = tempMinVoltage;
    maxVoltageAtMaxPos = tempMaxVoltage;
    portEXIT_CRITICAL(&calibMux);
    debugEnabled = tempDebugEnabled;
    reg_deadband_v   = tempRegDeadband;
    reg_damping      = tempRegDamping;
    reg_settle_ms    = tempRegSettle;
    reg_undershoot_v = tempRegUndershoot;
    
    // Wende die validierten Presets an
    if (!doc["presets"].isNull() && hardwareInitialized) {
      JsonObject presets = doc["presets"];
      if (!presets["p1"].isNull()) A_p1->setValuePreset(presets["p1"]);
      if (!presets["p2"].isNull()) A_p2->setValuePreset(presets["p2"]);
      if (!presets["p3"].isNull()) A_p3->setValuePreset(presets["p3"]);
    }
    logMessage(LOG_INFO, "Configuration successfully validated and applied.");
    return ""; // Leerer String signalisiert Erfolg
  } else {
    // FEHLER: Gib den JSON-String mit den Fehlermeldungen zurück
    String errorJson;
    serializeJson(errorDoc, errorJson);
    logMessage(LOG_ERROR, "Configuration validation failed: %s", errorJson.c_str());
    return errorJson;
  }
}

/**
 * @brief Validiert die übergebene Stepper-Konfiguration.
 * Prüft jeden Wert aus dem JSON-Dokument auf Gültigkeit.
 * @param doc Das JsonObject, das die neue Konfiguration enthält.
 * @return String Ein JSON-String mit Fehlermeldungen. Ist leer bei Erfolg.
 */
String applyAndValidateStepperConfig(JsonObject doc) {
  JsonDocument errorDoc;
  JsonObject errors = errorDoc.to<JsonObject>();

  // --- StallGuard Threshold (Param 174) ---
  // Gültiger Bereich: -64 bis 63
  if (!doc["stallguard_threshold"].isNull()) {
    if (!doc["stallguard_threshold"].is<int>()) {
      errors["stallguard_threshold"] = "must be an integer";
    } else {
      int val = doc["stallguard_threshold"];
      if (val < -64 || val > 63) {
        errors["stallguard_threshold"] = "must be between -64 and 63";
      }
    }
  } else {
    errors["stallguard_threshold"] = "is missing";
  }

  // --- CoolStep Speed Threshold (Param 182) ---
  // Gültiger Bereich: 0 bis 1048575
  if (!doc["coolstep_speed_threshold"].isNull()) {
    if (!doc["coolstep_speed_threshold"].is<int>()) {
      errors["coolstep_speed_threshold"] = "must be an integer";
    } else {
      long val = doc["coolstep_speed_threshold"];
      if (val < 0 || val > 1048575) {
        errors["coolstep_speed_threshold"] = "must be between 0 and 1048575";
      }
    }
  } else {
    errors["coolstep_speed_threshold"] = "is missing";
  }

  // --- CoolStep Hysteresis Start (Param 172) ---
  // Gültiger Bereich: 0 bis 15
  if (!doc["coolstep_hyst_start"].isNull()) {
    if (!doc["coolstep_hyst_start"].is<int>()) {
      errors["coolstep_hyst_start"] = "must be an integer";
    } else {
      int val = doc["coolstep_hyst_start"];
      if (val < 0 || val > 15) {
        errors["coolstep_hyst_start"] = "must be between 0 and 15";
      }
    }
  } else {
    errors["coolstep_hyst_start"] = "is missing";
  }

  // --- CoolStep Min Current (Param 168) ---
  // Gültiger Wert: 0 oder 1
  if (!doc["coolstep_min_current"].isNull()) {
    if (!doc["coolstep_min_current"].is<int>()) {
      errors["coolstep_min_current"] = "must be an integer (0 or 1)";
    } else {
      int val = doc["coolstep_min_current"];
      if (val != 0 && val != 1) {
        errors["coolstep_min_current"] = "must be 0 (1/2 current) or 1 (1/4 current)";
      }
    }
  } else {
    errors["coolstep_min_current"] = "is missing";
  }

  // --- Finale Entscheidung ---
  if (errors.size() == 0) {
    logMessage(LOG_INFO, "Stepper configuration successfully validated.");
    return ""; // Leerer String signalisiert Erfolg
  } else {
    // FEHLER: Gib den JSON-String mit den Fehlermeldungen zurück
    String errorJson;
    serializeJson(errorDoc, errorJson);
    logMessage(LOG_ERROR, "Stepper configuration validation failed: %s", errorJson.c_str());
    return errorJson;
  }
}

/**
 * @brief Speichert die aktuellen Konfigurationswerte als JSON ins NVS (#35).
 */
void saveConfiguration() {
  JsonDocument doc;

  // Fülle das Dokument mit den aktuellen Werten aus den globalen Variablen
  doc["system"]["debug_enabled"] = debugEnabled;

  doc["regulation"]["deadband_v"]   = serialized(String(reg_deadband_v, 1));
  doc["regulation"]["damping"]      = serialized(String(reg_damping, 2));
  doc["regulation"]["settle_ms"]    = reg_settle_ms;
  doc["regulation"]["undershoot_v"] = serialized(String(reg_undershoot_v, 1));

  doc["calibration"]["min_pos"] = minWiperPos;
  doc["calibration"]["max_pos"] = maxWiperPos;
  doc["calibration"]["min_voltage"] = serialized(String(minVoltageAtMinPos, 1));
  doc["calibration"]["max_voltage"] = serialized(String(maxVoltageAtMaxPos, 1));
  
  if (hardwareInitialized && currentMode == MODE_NORMAL) {
    doc["presets"]["p1"] = A_p1->getValuePreset();
    doc["presets"]["p2"] = A_p2->getValuePreset();
    doc["presets"]["p3"] = A_p3->getValuePreset();
  } else {
    // Im Settings-Modus tragen A_p1/A_p2 Kalibrier-POSITIONEN (Schritte), keine
    // Preset-Spannungen — deshalb die gespeicherten Presets unverändert aus dem
    // NVS übernehmen. (Bugfix: hier wurde früher 0/0/0 geschrieben, wodurch die
    // Presets bei jedem Speichern eines Kalibrierpunkts verloren gingen.)
    JsonDocument cur;
    String raw = configRawJson();
    if (raw.length() > 0 &&
        deserializeJson(cur, raw) == DeserializationError::Ok &&
        !cur["presets"].isNull()) {
      doc["presets"]["p1"] = (int)(cur["presets"]["p1"] | 0);
      doc["presets"]["p2"] = (int)(cur["presets"]["p2"] | 0);
      doc["presets"]["p3"] = (int)(cur["presets"]["p3"] | 0);
    } else {
      doc["presets"]["p1"] = 0;
      doc["presets"]["p2"] = 0;
      doc["presets"]["p3"] = 0;
    }
  }

  // #35: In den NVS schreiben (überlebt uploadfs/OTA). Lokale Preferences-Instanz,
  // damit gleichzeitige Aufrufer (Web-Task, Input-Task) sich keinen Handle teilen.
  String out;
  serializeJson(doc, out);
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    size_t written = prefs.putString(NVS_KEY_CONFIG, out);
    prefs.end();
    if (written > 0) {
      logMessage(LOG_INFO, "Configuration saved to NVS.");
    } else {
      logMessage(LOG_ERROR, "Failed to write configuration to NVS.");
    }
  } else {
    logMessage(LOG_ERROR, "Failed to open NVS for writing.");
  }
}

/**
 * @brief Lädt alle Konfigurationswerte aus dem NVS (#35); migriert einmalig von config.json.
 * Wenn kein Eintrag existiert oder er fehlerhaft ist, werden Standardwerte angewendet und gespeichert.
 * @return true falls die Konfiguration erfolgreich geladen und angewendet werden konnte.
 */
boolean loadConfiguration() {
  // #35: Quelle ist der NVS. Ist er leer, wird einmalig von der alten config.json
  // (LittleFS) migriert — deren ROHER Inhalt wandert nach erfolgreicher Validierung
  // unverändert ins NVS (bewahrt insbesondere die Preset-Werte exakt).
  Preferences prefs;
  String cfg;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    cfg = prefs.getString(NVS_KEY_CONFIG, "");
    prefs.end();
  }

  bool migratedFromFile = false;
  if (cfg.isEmpty() && LittleFS.exists(CONFIG_FILE)) {
    File configFile = LittleFS.open(CONFIG_FILE, "r");
    if (configFile) {
      cfg = configFile.readString();
      configFile.close();
      migratedFromFile = true;
      logMessage(LOG_WARN, "Config migration: found legacy config.json, importing into NVS.");
    }
  }

  if (cfg.isEmpty()) {
    logMessage(LOG_WARN, "No config in NVS. Applying defaults and creating new entry.");
    applyDefaultConfiguration(); // Setzt globale Variablen auf Standardwerte
    saveConfiguration();         // Speichert diese Standardwerte ins NVS
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, cfg);

  if (error) {
    logMessage(LOG_ERROR, "Failed to parse config, applying defaults. Error: %s", error.c_str());
    applyDefaultConfiguration();
    saveConfiguration(); // Speichere einen sauberen Eintrag, um den korrupten zu überschreiben
    return false;
  }

  // Rufe die zentrale Validierungs-Funktion auf
  String validationError = applyAndValidateConfig(doc.as<JsonObject>());

  if (!validationError.isEmpty()) {
    // Wenn die geladene Konfiguration ungültig ist, wende stattdessen die Standardwerte an
    logMessage(LOG_ERROR, "Loaded config is invalid, applying defaults.");
    applyDefaultConfiguration();
    return false;
  }

  if (migratedFromFile) {
    // Validierten Datei-Inhalt 1:1 ins NVS übernehmen und die alte Datei entfernen,
    // damit sie nicht fälschlich weiterhin als Quelle erscheint.
    Preferences wr;
    if (wr.begin(NVS_NAMESPACE, false)) {
      wr.putString(NVS_KEY_CONFIG, cfg);
      wr.end();
      LittleFS.remove(CONFIG_FILE);
      logMessage(LOG_WARN, "Config migration: config.json imported into NVS and removed from LittleFS.");
    } else {
      logMessage(LOG_ERROR, "Config migration: could not open NVS - keeping config.json.");
    }
  }
  return true;
}
