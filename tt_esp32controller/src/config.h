// TWM Isolation Variac – Konfiguration: NVS-Store (#35) + Validierung (#10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#ifndef CONFIG_H
#define CONFIG_H

#include <ArduinoJson.h>

// Setzt alle Konfigurationswerte auf Standardwerte (lauffähiges System).
void applyDefaultConfiguration();
// Validiert ein Config-JSON und übernimmt es bei Erfolg. Leerer String = ok, sonst Fehler-JSON.
String applyAndValidateConfig(JsonObject doc);
// Validiert eine Stepper-Konfiguration (Trinamic-Parameter). Leerer String = ok.
String applyAndValidateStepperConfig(JsonObject doc);
// Speichert die aktuelle Konfiguration als JSON-String ins NVS (#35).
void saveConfiguration();
// Lädt die Konfiguration aus dem NVS; migriert einmalig von config.json (#35).
boolean loadConfiguration();
// Liefert den rohen Config-JSON-String aus dem NVS (leer, wenn keiner existiert).
String configRawJson();

#endif // CONFIG_H
