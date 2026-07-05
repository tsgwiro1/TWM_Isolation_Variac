// TWM Isolation Variac – Pin-Belegung (ESP32-S3 + MCP23017)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#ifndef PINS_H
#define PINS_H

// Hardware definitions ESP32-S3
#define PIN_STEP 16
#define PIN_DIR 15
#define PIN_EN 7
#define PIN_ENCCLK 4
#define PIN_ENCDT 5
#define PIN_ENCSW 6
#define PIN_SCL 9
#define PIN_SDA 8
#define PIN_TX 18
#define PIN_RX 17
#define PIN_FANPWM 36
#define PIN_T_ONOFF 42
#define PIN_T_LIMIT 41
#define PIN_T_REG 40
#define PIN_T_P1 39
#define PIN_T_P2 38
#define PIN_T_P3 37
#define PIN_DISP_RESET 47
//#define PIN_MOSI 11
//#define PIN_MISO 13
//#define PIN_SCLK 12
#define PIN_CS1 10
#define PIN_DC 14
#define PIN_SW1 48
#define PIN_ONEWIRE 21
#define PIN_DISP_BL 35
#define PIN_ESP_STATUS 46
#define PIN_RX_STEPPER 2
#define PIN_TX_STEPPER 1

// Hardware definition MCP23017
#define PIN_RELAIS_ONOFF 8
#define PIN_RELAIS_LIMIT 9
#define PIN_LED_ONOFF 2
#define PIN_LED_LIMIT 1
#define PIN_LED_REG 0
#define PIN_LED_P1 3
#define PIN_LED_P2 4
#define PIN_LED_P3 5
#define PIN_LED_x10 6

#endif // PINS_H
