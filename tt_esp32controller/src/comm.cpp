// TWM Isolation Variac – Voltmeter-Link (UART) + FW-Update AN3155 (#30, #10)
// Copyright (c) 2025 Roger Widmer & Michael Tanner – MIT-Lizenz (siehe tt_esp32controller.ino)
#include "comm.h"
#include <FS.h>
#include <LittleFS.h>
#include "pins.h"
#include "state.h"
#include "logging.h"
#include "motor.h"    // stepper.isRunning() im communicationTask
#include "actions.h"  // A_onoff (Ausgang aus vor dem Flashen), Encoder-Zeit
#include "sim.h"

// Frame-Formate (siehe tt_voltmeter/documentation/Link-Protokoll.md):
// RMS-Frame     (Voltmeter -> Controller): 0xAA HI LO CHK 0xBB        (Spannung*10)
// Antwort-Frame (Voltmeter -> Controller): 0xB5 CMD LEN [payload] CHK 0xBB
// Befehl-Frame  (Controller -> Voltmeter): 0xA5 CMD LEN [payload] CHK 0xBB
static const byte RMS_SOF      = 0xAA;
static const byte RSP_SOF      = 0xB5;
static const byte LINK_CMD_SOF = 0xA5;
static const byte FRAME_EOF    = 0xBB;

enum RxPhase {
  RXP_SOF,
  RXP_RMS_PAYLOAD, RXP_RMS_CHK, RXP_RMS_EOF,        // RMS-Frame
  RXP_RSP_CMD, RXP_RSP_LEN, RXP_RSP_DATA, RXP_RSP_CHK, RXP_RSP_EOF  // Antwort-Frame
};
static RxPhase rxPhase = RXP_SOF;
static byte rmsBuf[2];
static byte rmsIdx = 0;
static uint8_t rspCmd = 0, rspLen = 0, rspIdx = 0, rspChk = 0;
static uint8_t rspBuf[64];
// Vom Voltmeter empfangene Antwort (für den Befehls-Link)
volatile bool    voltmeterResponseReady = false;
volatile uint8_t voltmeterResponseCmd   = 0;
volatile uint8_t voltmeterResponseLen   = 0;
uint8_t          voltmeterResponsePayload[64];

// --- Voltmeter-FW-Update (#30, AN3155-Host) ---
#define VM_FLASH_BASE   0x08000000UL
#define VM_FW_MAX_SIZE  (124u * 1024u)   // F103CB hat 128 KB Flash, etwas Reserve
#define VM_FLASH_PAGE   1024u            // F103CB: 1 KB Flash-Page
#define VM_EEPROM_PAGE  127u             // letzte Page = emuliertes EEPROM (Kalibrierung) -> NICHT löschen
#define BL_BLOCK        256              // AN3155 Write-Memory: max. 256 Byte/Block
#define BL_ACK          0x79
#define BL_NACK         0x1F
#define BL_INIT         0x7F
volatile VmUpdateState vmUpdateState   = VMU_IDLE;
volatile int           vmUpdateProgress = 0;     // 0..100 %
volatile bool          vmUpdateRequested = false; // vom Web gesetzt, vom Update-Task abgearbeitet
volatile bool          vmUpdateSkipEnter = false; // Diagnose: ENTER_BOOTLOADER überspringen (BOOT0)
char                   vmUpdateMessage[96] = "";  // letzte Status-/Fehlermeldung (Single-Writer: Update-Task)

// Messwert vom Voltmeter (RMS-Stream)
volatile bool new_value_available = false;
volatile float received_rms_value = 0.0;
// Datenfrische: Zeitpunkt des letzten gültigen Messwerts vom Voltmeter (#18).
volatile uint32_t last_rms_received_time = 0;
#define RMS_TIMEOUT_MS 250   // Voltmeter sendet alle ~40 ms; nach 250 ms gilt der Wert als veraltet

static void parseByte(byte b);

/**
 * @brief Parst ein einzelnes Byte aus dem seriellen Datenstrom.
 * @param b Das zu verarbeitende Byte.
 */
static void parseByte(byte b) {
  switch (rxPhase) {
    case RXP_SOF:
      if (b == RMS_SOF)      { rmsIdx = 0; rxPhase = RXP_RMS_PAYLOAD; }
      else if (b == RSP_SOF) { rspIdx = 0; rspChk = 0; rxPhase = RXP_RSP_CMD; }
      break;

    // --- RMS-Frame (Spannungswert) ---
    case RXP_RMS_PAYLOAD:
      rmsBuf[rmsIdx++] = b;
      if (rmsIdx >= 2) rxPhase = RXP_RMS_CHK;
      break;
    case RXP_RMS_CHK:
      rxPhase = (b == (byte)(rmsBuf[0] + rmsBuf[1])) ? RXP_RMS_EOF : RXP_SOF;
      break;
    case RXP_RMS_EOF:
      if (b == FRAME_EOF) {
        uint16_t int_value = ((uint16_t)rmsBuf[0] << 8) | rmsBuf[1];
        received_rms_value = (float)int_value / 10.0f;
        new_value_available = true;
      }
      rxPhase = RXP_SOF;
      break;

    // --- Antwort-Frame (Voltmeter -> Controller) ---
    case RXP_RSP_CMD:
      rspCmd = b; rspChk = b; rxPhase = RXP_RSP_LEN;
      break;
    case RXP_RSP_LEN:
      rspLen = b; rspChk += b; rspIdx = 0;
      if (rspLen > sizeof(rspBuf)) rxPhase = RXP_SOF;              // ungültige Länge
      else rxPhase = (rspLen > 0) ? RXP_RSP_DATA : RXP_RSP_CHK;
      break;
    case RXP_RSP_DATA:
      rspBuf[rspIdx++] = b; rspChk += b;
      if (rspIdx >= rspLen) rxPhase = RXP_RSP_CHK;
      break;
    case RXP_RSP_CHK:
      rxPhase = (b == rspChk) ? RXP_RSP_EOF : RXP_SOF;
      break;
    case RXP_RSP_EOF:
      if (b == FRAME_EOF) {
        voltmeterResponseCmd = rspCmd;
        voltmeterResponseLen = rspLen;
        memcpy(voltmeterResponsePayload, rspBuf, rspLen);
        voltmeterResponseReady = true;
      }
      rxPhase = RXP_SOF;
      break;
  }
}

/**
 * @brief Sendet einen Befehls-Frame an das Voltmeter: 0xA5 CMD LEN [payload] CHK 0xBB.
 */
void sendVoltmeterCommand(uint8_t cmd, const uint8_t* payload, uint8_t len) {
  uint8_t chk = cmd + len;
  Serial1.write(LINK_CMD_SOF);
  Serial1.write(cmd);
  Serial1.write(len);
  for (uint8_t i = 0; i < len; i++) { Serial1.write(payload[i]); chk += payload[i]; }
  Serial1.write((uint8_t)(chk & 0xFF));
  Serial1.write(FRAME_EOF);
}

/**
 * @brief Sendet einen Befehl ans Voltmeter und wartet auf die passende Antwort.
 * Die Antwort wird vom communicationTask geparst (setzt voltmeterResponse*).
 * @return true, wenn rechtzeitig eine Antwort mit demselben CMD kam.
 */
bool voltmeterRequest(uint8_t cmd, const uint8_t* payload, uint8_t len, uint32_t timeoutMs) {
  voltmeterResponseReady = false;
  sendVoltmeterCommand(cmd, payload, len);
  uint32_t t0 = millis();
  while (!voltmeterResponseReady && (millis() - t0) < timeoutMs) {
    delay(5); // yieldet -> communicationTask verarbeitet den Antwort-Frame
  }
  return voltmeterResponseReady && voltmeterResponseCmd == cmd;
}

/**
 * @brief Prüft, ob ein aktueller (frischer) Messwert vom Voltmeter vorliegt.
 * @return true, wenn innerhalb von RMS_TIMEOUT_MS ein gültiger RMS-Wert empfangen wurde.
 */
bool isVoltageDataFresh() {
  return last_rms_received_time != 0 && (millis() - last_rms_received_time) < RMS_TIMEOUT_MS;
}

/**
 * @brief FreeRTOS Task zur Verarbeitung der seriellen Kommunikation.
 * Parst eingehende Daten und aktualisiert den Systemzustand entsprechend.
 * @param parameter Standard FreeRTOS Task-Parameter (hier ungenutzt).
 */
void communicationTask(void *parameter) {
  for (;;) {
#ifdef SIM
    // Simulationsmodus: alle 40 ms (wie das echte Voltmeter) einen neuen Wert erzeugen
    static uint32_t lastSimUpdate = 0;
    if (millis() - lastSimUpdate >= 40) {
      lastSimUpdate = millis();
      simUpdateMeasuredVoltage();
      new_value_available = true;
    }
#else
    while (Serial1.available() > 0) {
      byte newByte = Serial1.read();
      parseByte(newByte);
    }
#endif

    if (new_value_available) {
      new_value_available = false; // Flag zurücksetzen
      last_rms_received_time = millis(); // Datenfrische markieren (gilt für Real- und SIM-Pfad)
      static bool was_manual = false; // wird aktuell manuell am Encoder gedreht

      // Prüfen, ob der Benutzer gerade den Encoder bedient oder kurz zuvor bedient hat
      bool user_is_adjusting = (millis() - last_encoder_change_time < 1000);

      if (user_is_adjusting) {
        // PHASE 1 & 2: Manuelle Steuerung & Beruhigung

        // Regelung ist und bleibt aus
        is_regulation_active = false;

        // Solange der Stepper noch in Bewegung ist, bleibt manuell aktiv
        if (stepper.isRunning()) {
          last_encoder_change_time  = millis();
        }
        
        // SYNCHRONISIERUNG: Der Sollwert folgt dem realen Istwert.
        setpoint_voltage = received_rms_value;

        if (!was_manual) {
            logMessage(LOG_INFO, "MOTOR: Mode MANUAL (Encoder active)");
            was_manual = true;
        }      
      } else {
        // Der Benutzer hat den Encoder seit mehr als 1 Sekunde (1000 ms) nicht mehr berührt und der Stepper hat sein Ziel erreicht.
        
        // Wenn die Regelung noch nicht aktiv war, ist dies der Moment, sie zu starten.
        if (!is_regulation_active && last_encoder_change_time != 0) {
          
          // PHASE 3: Automatik-Regelung wird aktiviert
          is_regulation_active = true;
          
          // Zeitstempel zurücksetzen, damit dieser Block nicht erneut ausgeführt wird.
          last_encoder_change_time = 0; 
          
          logMessage(LOG_INFO, "MOTOR: Mode AUTOMATIC (Setpoint fixed at %.1f V)", setpoint_voltage);
          was_manual = false;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10)); // Sehr oft prüfen
  }
}

// ********************************************************************************
// Voltmeter-FW-Update über den ROM-UART-Bootloader (AN3155), #30
// ********************************************************************************
// Während dieser Sequenz ist der communicationTask suspendiert und Serial1 läuft
// vorübergehend auf 8E1 (der STM32-Bootloader nutzt gerade Parität). Wir lesen die
// Bootloader-Bytes hier roh (ohne den Frame-Parser). Recovery bei Fehlflash: ST-Link.

// Liest ein einzelnes Byte direkt von Serial1 mit Timeout. -1 = Timeout.
static int blReadByte(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (Serial1.available() == 0) {
    if (millis() - t0 >= timeoutMs) return -1;
    delay(1); // yieldet
  }
  return Serial1.read();
}

// Wartet auf ein ACK (0x79) vom Bootloader.
static bool blWaitAck(uint32_t timeoutMs) {
  return blReadByte(timeoutMs) == BL_ACK;
}

// Sendet Befehlsbyte + Komplement und wartet auf ACK.
static bool blCmd(uint8_t cmd, uint32_t timeoutMs) {
  Serial1.write(cmd);
  Serial1.write((uint8_t)(cmd ^ 0xFF));
  Serial1.flush();
  return blWaitAck(timeoutMs);
}

// Sendet eine 4-Byte-Adresse (MSB first) + XOR-Checksumme und wartet auf ACK.
static bool blAddr(uint32_t addr, uint32_t timeoutMs) {
  uint8_t a[4] = { (uint8_t)(addr >> 24), (uint8_t)(addr >> 16),
                   (uint8_t)(addr >> 8),  (uint8_t)addr };
  Serial1.write(a, 4);
  Serial1.write((uint8_t)(a[0] ^ a[1] ^ a[2] ^ a[3]));
  Serial1.flush();
  return blWaitAck(timeoutMs);
}

// Setzt Status + Meldung (Single-Writer: Update-Task).
void vmUpdSet(VmUpdateState st, int prog, const char* msg) {
  vmUpdateProgress = prog;
  strncpy(vmUpdateMessage, msg, sizeof(vmUpdateMessage) - 1);
  vmUpdateMessage[sizeof(vmUpdateMessage) - 1] = '\0';
  vmUpdateState = st;
}

// Liest die FW-Version aus der hochgeladenen .bin: scannt nach dem Magic-Tag "@@VMFW@@"
// und kopiert den FW-String dahinter (bis NUL/nicht-druckbar). false = nicht gefunden. (#33)
bool readVmFwFileVersion(char* out, size_t outSize) {
  if (outSize == 0) return false;
  out[0] = '\0';
  File f = LittleFS.open(VM_FW_PATH, "r");
  if (!f) return false;
  static const char MAGIC[] = "@@VMFW@@";
  const size_t MAGLEN = 8;
  size_t match = 0;
  bool found = false;
  while (f.available()) {
    int c = f.read();
    if (c < 0) break;
    if ((char)c == MAGIC[match]) {
      if (++match == MAGLEN) { found = true; break; }
    } else {
      match = ((char)c == MAGIC[0]) ? 1 : 0;
    }
  }
  if (found) {
    size_t i = 0;
    while (f.available() && i < outSize - 1) {
      int c = f.read();
      if (c < 0x20 || c > 0x7E) break; // NUL/0xFF/nicht-druckbar -> Ende
      out[i++] = (char)c;
    }
    out[i] = '\0';
  }
  f.close();
  return found && out[0] != '\0';
}

// Plausibilisiert die .bin (Größe + Vektortabelle: MSP im RAM, Reset-Vektor im Flash).
static bool vmFwValidate(File& f, size_t& sizeOut) {
  size_t sz = f.size();
  if (sz < 0x100 || sz > VM_FW_MAX_SIZE) return false;
  uint8_t hdr[8];
  f.seek(0);
  if (f.read(hdr, 8) != 8) return false;
  uint32_t msp   = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
  uint32_t reset = hdr[4] | (hdr[5] << 8) | (hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
  // F103CB: 20 KB RAM ab 0x20000000, 128 KB Flash ab 0x08000000.
  if (msp < 0x20000000UL || msp > 0x20005000UL) return false;
  if (reset < VM_FLASH_BASE || reset >= (VM_FLASH_BASE + 0x20000UL)) return false;
  sizeOut = sz;
  return true;
}

// Führt das komplette Flashen aus. Setzt vmUpdateState/-Progress/-Message.
static void runVoltmeterFlash() {
  vmUpdSet(VMU_RUNNING, 0, "Pruefe Firmware-Datei...");

  // 1) Sicherheit: Ausgang aus, Regelung stoppen.
  is_regulation_active = false;
  if (A_onoff) A_onoff->off();

  // 2) Datei öffnen + plausibilisieren.
  File f = LittleFS.open(VM_FW_PATH, "r");
  if (!f) { vmUpdSet(VMU_ERROR, 0, "Firmware-Datei nicht gefunden."); return; }
  size_t fwSize = 0;
  if (!vmFwValidate(f, fwSize)) {
    f.close();
    vmUpdSet(VMU_ERROR, 0, "Ungueltige .bin (Groesse/Vektortabelle).");
    return;
  }

  // 3) Voltmeter in den Bootloader schicken (normaler Frame, communicationTask parst ACK).
  //    Diagnose-Modus (skipEnter): VM wurde bereits per BOOT0+Reset in den ROM-Loader gebracht.
  if (!vmUpdateSkipEnter) {
    vmUpdSet(VMU_RUNNING, 2, "Voltmeter -> Bootloader...");
    if (!voltmeterRequest(VM_CMD_ENTER_BOOTLOADER, nullptr, 0, 800)) {
      f.close();
      vmUpdSet(VMU_ERROR, 0, "Voltmeter antwortet nicht (ENTER_BOOTLOADER).");
      return;
    }
  } else {
    vmUpdSet(VMU_RUNNING, 2, "Diagnose: ENTER_BOOTLOADER uebersprungen (BOOT0).");
  }

  // 4) Ab hier gehört Serial1 uns: communicationTask suspendieren, auf 8E1 umstellen.
  vTaskSuspend(h_communicationTask);
  delay(150); // dem Voltmeter Zeit für den Sprung ins System-Memory lassen
  Serial1.end();
  Serial1.begin(115200, SERIAL_8E1, PIN_RX, PIN_TX);
  while (Serial1.available()) Serial1.read(); // RX-Reste verwerfen

  bool ok = false;
  do {
    // 5) Auto-Baud / Handshake: 0x7F -> ACK (einige Versuche).
    vmUpdSet(VMU_RUNNING, 5, "Bootloader-Handshake...");
    bool synced = false;
    for (int i = 0; i < 5 && !synced; i++) {
      Serial1.write((uint8_t)BL_INIT);
      Serial1.flush();
      int r = blReadByte(500);
      if (r == BL_ACK || r == BL_NACK) synced = true; // NACK = schon initialisiert
    }
    if (!synced) { vmUpdSet(VMU_ERROR, 5, "Kein Bootloader-ACK (0x7F)."); break; }

    // 6) Get-Befehl: unterstützte Kommandos abfragen (Standard- vs. Extended-Erase).
    bool extErase = false;
    Serial1.write((uint8_t)0x00); Serial1.write((uint8_t)0xFF); Serial1.flush();
    if (!blWaitAck(1000)) { vmUpdSet(VMU_ERROR, 6, "Get-Befehl fehlgeschlagen."); break; }
    int n = blReadByte(1000);                 // Anzahl folgender Kommando-Bytes
    int ver = blReadByte(1000); (void)ver;    // Bootloader-Version
    if (n < 0 || ver < 0) { vmUpdSet(VMU_ERROR, 6, "Get-Antwort unvollstaendig."); break; }
    for (int i = 0; i < n; i++) {
      int c = blReadByte(1000);
      if (c == 0x44) extErase = true;         // Extended Erase unterstützt
    }
    if (!blWaitAck(1000)) { vmUpdSet(VMU_ERROR, 6, "Get-Abschluss-ACK fehlt."); break; }

    // 7) NUR die Programmpages löschen (Page 0 .. nötige Pages). KEIN Mass-Erase, damit die
    //    letzte Page (emuliertes EEPROM = Kalibrierung) erhalten bleibt.
    vmUpdSet(VMU_RUNNING, 10, "Loesche Programm-Flash...");
    uint16_t pages = (uint16_t)((fwSize + VM_FLASH_PAGE - 1) / VM_FLASH_PAGE);
    if (pages == 0) pages = 1;
    if (pages > VM_EEPROM_PAGE) { // würde die EEPROM-Page überschreiben
      vmUpdSet(VMU_ERROR, 10, "Firmware zu gross (Kollision mit EEPROM-Page).");
      break;
    }
    if (extErase) {
      if (!blCmd(0x44, 2000)) { vmUpdSet(VMU_ERROR, 10, "Extended-Erase abgelehnt."); break; }
      // N (Pages-1) als 2 Byte, dann je Page 2 Byte (MSB first), dann XOR-Checksumme.
      uint16_t nm1 = pages - 1;
      uint8_t chk = 0;
      uint8_t b;
      b = (uint8_t)(nm1 >> 8);  Serial1.write(b); chk ^= b;
      b = (uint8_t)(nm1 & 0xFF); Serial1.write(b); chk ^= b;
      for (uint16_t p = 0; p < pages; p++) {
        b = (uint8_t)(p >> 8);  Serial1.write(b); chk ^= b;
        b = (uint8_t)(p & 0xFF); Serial1.write(b); chk ^= b;
      }
      Serial1.write(chk); Serial1.flush();
      if (!blWaitAck(30000)) { vmUpdSet(VMU_ERROR, 10, "Extended-Page-Erase Timeout."); break; }
    } else {
      if (!blCmd(0x43, 2000)) { vmUpdSet(VMU_ERROR, 10, "Erase abgelehnt."); break; }
      // N (Pages-1) als 1 Byte, dann je Page 1 Byte, dann XOR-Checksumme.
      uint8_t nm1 = (uint8_t)(pages - 1);
      uint8_t chk = nm1;
      Serial1.write(nm1);
      for (uint16_t p = 0; p < pages; p++) { Serial1.write((uint8_t)p); chk ^= (uint8_t)p; }
      Serial1.write(chk); Serial1.flush();
      if (!blWaitAck(30000)) { vmUpdSet(VMU_ERROR, 10, "Page-Erase Timeout."); break; }
    }

    // 8) Schreiben in 256-Byte-Blöcken ab 0x08000000.
    uint8_t buf[BL_BLOCK];
    uint32_t addr = VM_FLASH_BASE;
    size_t written = 0;
    bool writeOk = true;
    f.seek(0);
    while (written < fwSize) {
      int rd = f.read(buf, BL_BLOCK);
      if (rd <= 0) { writeOk = false; vmUpdSet(VMU_ERROR, 0, "Datei-Lesefehler."); break; }
      // auf 4-Byte-Grenze auffüllen (Bootloader schreibt wortweise)
      while (rd & 0x03) buf[rd++] = 0xFF;

      if (!blCmd(0x31, 1000)) { writeOk = false; vmUpdSet(VMU_ERROR, 0, "Write-Befehl abgelehnt."); break; }
      if (!blAddr(addr, 1000)) { writeOk = false; vmUpdSet(VMU_ERROR, 0, "Write-Adresse abgelehnt."); break; }
      Serial1.write((uint8_t)(rd - 1));
      uint8_t chk = (uint8_t)(rd - 1);
      for (int i = 0; i < rd; i++) { Serial1.write(buf[i]); chk ^= buf[i]; }
      Serial1.write(chk);
      Serial1.flush();
      if (!blWaitAck(2000)) { writeOk = false; vmUpdSet(VMU_ERROR, 0, "Write-Block ohne ACK."); break; }

      addr    += rd;
      written += rd;
      int prog = 10 + (int)((written * 85ULL) / fwSize); // 10..95 %
      vmUpdSet(VMU_RUNNING, prog, "Schreibe Firmware...");
    }
    if (!writeOk) break;

    // 9) Go: neue Anwendung ab 0x08000000 starten.
    vmUpdSet(VMU_RUNNING, 97, "Starte neue Firmware...");
    if (!blCmd(0x21, 1000) || !blAddr(VM_FLASH_BASE, 1000)) {
      // Go fehlgeschlagen -> der nächste Voltmeter-Reset startet die FW trotzdem.
      vmUpdSet(VMU_ERROR, 97, "Flash ok, aber 'Go' fehlte (Voltmeter neu starten).");
      break;
    }
    ok = true;
  } while (0);

  // 10) Aufräumen: zurück auf 8N1, communicationTask wieder aktivieren.
  f.close();
  Serial1.end();
  Serial1.begin(115200, SERIAL_8N1, PIN_RX, PIN_TX);
  while (Serial1.available()) Serial1.read();
  vTaskResume(h_communicationTask);

  if (ok) vmUpdSet(VMU_SUCCESS, 100, "Update erfolgreich.");
}

// Persistenter Task: wartet auf den Trigger aus dem Web und flasht dann.
void voltmeterUpdateTask(void *parameter) {
  for (;;) {
    if (vmUpdateRequested) {
      vmUpdateRequested = false;
      runVoltmeterFlash();
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

