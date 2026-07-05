// TWM Isolation Variac – Voltmeter-Link (UART, Paket J) + FW-Update AN3155 (#30, #10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#ifndef COMM_H
#define COMM_H

#include <Arduino.h>

// Befehls-Codes des Voltmeter-Links (siehe tt_voltmeter/documentation/Link-Protokoll.md)
#define VM_CMD_GET_VERSION  0x01
#define VM_CMD_GET_STATUS   0x02
#define VM_CMD_SET_FACTOR   0x10
#define VM_CMD_SET_OFFSET   0x11
#define VM_CMD_RECAL        0x20
#define VM_CMD_CAL3_MEASURE 0x21
#define VM_CMD_CAL3_FINISH  0x22
#define VM_CMD_REBOOT       0x30
#define VM_CMD_RESET_DEFAULTS 0x31
#define VM_CMD_ENTER_BOOTLOADER 0x40

// Vom Voltmeter empfangene Antwort (für den Befehls-Link; Web-Routen lesen sie aus)
extern volatile bool    voltmeterResponseReady;
extern volatile uint8_t voltmeterResponseCmd;
extern volatile uint8_t voltmeterResponseLen;
extern uint8_t          voltmeterResponsePayload[64];

// Messwert vom Voltmeter (RMS-Stream) + Datenfrische (#18)
extern volatile bool new_value_available;
extern volatile float received_rms_value;
extern volatile uint32_t last_rms_received_time;
bool isVoltageDataFresh();

// Befehls-Link
void sendVoltmeterCommand(uint8_t cmd, const uint8_t* payload, uint8_t len);
bool voltmeterRequest(uint8_t cmd, const uint8_t* payload, uint8_t len, uint32_t timeoutMs);

// --- Voltmeter-FW-Update (#30, AN3155-Host) ---
#define VM_FW_PATH      "/voltmeter_fw.bin"
#define VM_UPDATE_RESULT_MS 5000   // Ergebnis (Erfolg/Fehler) so lange auf dem LCD zeigen (#32)
enum VmUpdateState { VMU_IDLE, VMU_RUNNING, VMU_SUCCESS, VMU_ERROR };
extern volatile VmUpdateState vmUpdateState;
extern volatile int           vmUpdateProgress;   // 0..100 %
extern volatile bool          vmUpdateRequested;  // vom Web gesetzt, vom Update-Task abgearbeitet
extern volatile bool          vmUpdateSkipEnter;  // Diagnose: ENTER_BOOTLOADER überspringen (BOOT0)
extern char                   vmUpdateMessage[96];
// Update-Status setzen (Web-Route /update/start nutzt das für "Update gestartet")
void vmUpdSet(VmUpdateState st, int prog, const char* msg);
// FW-Version aus der hochgeladenen .bin lesen (Magic-Tag @@VMFW@@, #33)
bool readVmFwFileVersion(char* out, size_t outSize);

// RTOS-Tasks
void communicationTask(void *parameter);
void voltmeterUpdateTask(void *parameter);

#endif // COMM_H
