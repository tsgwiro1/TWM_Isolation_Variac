// TWM Isolation Variac – Lüfter, Temperatursensor und Status-LED (#10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#include "system.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "pins.h"
#include "state.h"
#include "logging.h"

// Fan
#define MINFANPWM 10
#define MAXFANPWM 100
#define MINFANTEMP 30
// GitHub-#20: MAXFANTEMP ist die führende Temperatur-Schwelle des Systems (Lüfter auf
// 100 % + Alarm im Log). Das Dashboard spiegelt sie in data/script.js (cfg.tWarn/tMax) —
// bei einer Änderung hier dort mitziehen, sonst zeigt die Weboberfläche noch grün,
// während die Firmware bereits Alarm meldet.
#define MAXFANTEMP 60
#define NOSENSORFANSPEED 75
#define FAN_PWM_CHANNEL 0
#define FAN_PWM_FREQ 25000
#define FAN_PWM_RESOLUTION 8 // 8-bit = 0-255

// Temperature Sensor
static OneWire oneWire(PIN_ONEWIRE);
static DallasTemperature sensors(&oneWire);
static DeviceAddress tempSensorAdr;
volatile float wiperTemp = 0.0;
volatile boolean tempSensorAvailable = false;

/**
 * @brief Setzt die Lüftergeschwindigkeit über PWM.
 * @param v Die Geschwindigkeit in Prozent (0-100).
 */
void setFanSpeed(uint8_t v) {
    v = constrain(v, MINFANPWM, MAXFANPWM);
    // Konvertiere den 0-100% Wert auf die 8-bit Auflösung (0-255)
    uint32_t dutyCycle = map(v, 0, 100, 0, 255);
    ledcWrite(PIN_FANPWM, dutyCycle);
}

/**
 * @brief Liest die Temperatur vom OneWire-Sensor.
 * @return float Die aktuelle Temperatur in Grad Celsius.
 */
float getTemperature() {
    float t = wiperTemp;
    if (sensors.isConversionComplete()) {
        t = sensors.getTempC(tempSensorAdr);
    }
    sensors.requestTemperatures();
    return t;
}

/**
 * @brief Passt die Lüftergeschwindigkeit basierend auf der Temperatur an.
 * @param temp Die aktuelle Temperatur in Grad Celsius.
 */
void updateFan(float temp) {
    uint8_t p;
    if (temp < MINFANTEMP) {
        p = MINFANPWM;
    }
    else if (temp > MAXFANTEMP) {
        p = MAXFANPWM;
    }
    else {
        p = map(temp, MINFANTEMP, MAXFANTEMP, MINFANPWM, MAXFANPWM);
    }
    setFanSpeed(p);
}

/**
 * @brief FreeRTOS Task zur periodischen Messung der Temperatur und Steuerung des Lüfters.
 * @param parameter Standard FreeRTOS Task-Parameter (hier ungenutzt).
 */
void sensorAndFanTask(void *parameter) {
  static bool temp_warning_active = false;
  for (;;) {
    if (tempSensorAvailable) {
      wiperTemp = getTemperature();
      updateFan(wiperTemp);
      if (wiperTemp >= MAXFANTEMP && !temp_warning_active) {
          logMessage(LOG_ERROR, "SYSTEM: ALARM - Temperature critical (%.1f C)! Fan at 100%%.", wiperTemp);
          temp_warning_active = true;
      } 
      // Wieder abgekühlt? (Hysterese von 5 Grad)
      else if (wiperTemp < (MAXFANTEMP - 5.0) && temp_warning_active) {
          logMessage(LOG_INFO, "SYSTEM: CLEARED - Temperature back to normal (%.1f C).", wiperTemp);
          temp_warning_active = false;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); // Einmal pro Sekunde messen
  }
}

/**
 * @brief FreeRTOS Task zur Steuerung der Status-LED.
 * Zeigt den aktuellen Systemzustand durch verschiedene Blinkmuster an.
 * Status-LED ist LOW-Aktiv.
 * @param parameter Standard FreeRTOS Task-Parameter (hier ungenutzt).
 */
void statusLedTask(void *parameter) {
  pinMode(PIN_ESP_STATUS, OUTPUT);

  for (;;) { // Endlosschleife für den Task
    switch (currentSystemState) {
      case STATE_STARTING: // Langsames Blinken
        digitalWrite(PIN_ESP_STATUS, LOW);
        vTaskDelay(pdMS_TO_TICKS(500));
        digitalWrite(PIN_ESP_STATUS, HIGH);
        vTaskDelay(pdMS_TO_TICKS(500));
        break;

      case STATE_WIFIMANAGER_AP: // Schneller Doppel-Blink
        digitalWrite(PIN_ESP_STATUS, LOW); vTaskDelay(pdMS_TO_TICKS(100));
        digitalWrite(PIN_ESP_STATUS, HIGH);  vTaskDelay(pdMS_TO_TICKS(100));
        digitalWrite(PIN_ESP_STATUS, LOW); vTaskDelay(pdMS_TO_TICKS(100));
        digitalWrite(PIN_ESP_STATUS, HIGH);  vTaskDelay(pdMS_TO_TICKS(1000));
        break;

      case STATE_OTA_UPDATE: // Schnelles Blinken
        digitalWrite(PIN_ESP_STATUS, LOW);
        vTaskDelay(pdMS_TO_TICKS(150));
        digitalWrite(PIN_ESP_STATUS, HIGH);
        vTaskDelay(pdMS_TO_TICKS(150));
        break;

      case STATE_ERROR: // SOS
        // S
        for(int i=0; i<3; i++) { digitalWrite(PIN_ESP_STATUS, LOW); vTaskDelay(pdMS_TO_TICKS(150)); digitalWrite(PIN_ESP_STATUS, HIGH); vTaskDelay(pdMS_TO_TICKS(150)); }
        vTaskDelay(pdMS_TO_TICKS(300));
        // O
        for(int i=0; i<3; i++) { digitalWrite(PIN_ESP_STATUS, LOW); vTaskDelay(pdMS_TO_TICKS(400)); digitalWrite(PIN_ESP_STATUS, HIGH); vTaskDelay(pdMS_TO_TICKS(150)); }
        vTaskDelay(pdMS_TO_TICKS(300));
        // S
        for(int i=0; i<3; i++) { digitalWrite(PIN_ESP_STATUS, LOW); vTaskDelay(pdMS_TO_TICKS(150)); digitalWrite(PIN_ESP_STATUS, HIGH); vTaskDelay(pdMS_TO_TICKS(150)); }
        vTaskDelay(pdMS_TO_TICKS(1000));
        break;
        
      case STATE_NORMAL_OPERATION: // Langsames "Atmen" / Herzschlag
      default:
        digitalWrite(PIN_ESP_STATUS, LOW);
        vTaskDelay(pdMS_TO_TICKS(100));
        digitalWrite(PIN_ESP_STATUS, HIGH);
        vTaskDelay(pdMS_TO_TICKS(2900));
        break;
    }
  }
}

/**
 * @brief Initialisiert das PWM-Signal für den Lüfter.
 * @param tempSensorAvailable Gibt an, ob ein Temperatursensor gefunden wurde.
 */
void initFAN(boolean tempSensorAvailable) {
    ledcAttach(PIN_FANPWM, FAN_PWM_FREQ, FAN_PWM_RESOLUTION);

    uint8_t dc = tempSensorAvailable ? MINFANPWM : NOSENSORFANSPEED;
    setFanSpeed(dc);	
}

/**
 * @brief Initialisiert den OneWire-Bus und den Temperatursensor.
 * @return boolean true, wenn ein Sensor gefunden und konfiguriert wurde, sonst false.
 */
boolean initTempSensor() {
    boolean res;
    sensors.begin();
    res = sensors.getAddress(tempSensorAdr, 0);
    res &= sensors.setResolution(tempSensorAdr, 9);
    sensors.setWaitForConversion(false);
    sensors.requestTemperatures();
    return res;
}

