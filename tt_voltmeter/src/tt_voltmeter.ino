/*************************************************************
Copyright(c) 2025 Roger Widmer & Michael Tanner

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
************************************************************ */

/*
    Name:     tt_voltmeter.ino
    Created:	12.07.2025 11:03:55
    Author:   LONDON-WS\roger
*/

// version
#define FW  "Firmware V1.2.3"

#include <Arduino.h>
#include <stm32f1xx_hal.h>
#include <math.h> // Für sqrt()
#include <EEPROM.h>

// Magic-getaggter Versions-String, damit der Controller die FW-Version direkt aus der .bin
// lesen kann (#33). __attribute__((used)) hält ihn auch bei --gc-sections im Image.
// Der Controller scannt die .bin nach "@@VMFW@@" und liest den FW-String dahinter.
const char fw_version_tag[] __attribute__((used)) = "@@VMFW@@" FW;

// --- Konfiguration basierend auf den Zielen ---
#define AC_FREQUENCY_HZ             50.0f
#define SAMPLES_PER_FULL_CYCLE      100
#define NUM_CYCLES_TO_SAMPLE        2 // Für 40ms bei 50Hz (2 * 20ms)
#define DMA_BUFFER_SIZE             (SAMPLES_PER_FULL_CYCLE * NUM_CYCLES_TO_SAMPLE) // 200

#define SAMPLING_FREQUENCY_HZ       (SAMPLES_PER_FULL_CYCLE * AC_FREQUENCY_HZ) // 5000 Hz

#define ADC_PIN                     PA0 // ADC-Eingangspin (z.B. PA0 für ADC1_IN0)
#define ADC_CHANNEL_NUM             ADC_CHANNEL_0 // Zugehöriger ADC-Kanal

#define ADC_REFERENCE_VOLTAGE       3.3f
#define ADC_RESOLUTION_BITS         12
#define ADC_MAX_VALUE               ((1 << ADC_RESOLUTION_BITS) - 1) // 4095

#define RELAY_PIN                   PB0 // Pin für die Relaissteuerung zur Auto-Zero-Kalibrierung definieren
#define SYSLED_PIN                  PB1 // Pin für System-LED auf Baseboard (low-active)

// EEPROM-Adressen systematisch definieren
#define EEPROM_ADDR_START           0
#define EEPROM_MAGIC_NUMBER         0x43 // Geändert, um eine Neukonfiguration zu erzwingen
#define EEPROM_ADDR_SCALING_FACTOR  (EEPROM_ADDR_START + 1)
#define EEPROM_ADDR_VOLTAGE_OFFSET  (EEPROM_ADDR_SCALING_FACTOR + sizeof(float))
#define EEPROM_ADDR_LOW_LIMIT       (EEPROM_ADDR_VOLTAGE_OFFSET + sizeof(float))
#define EEPROM_ADDR_HIGH_LIMIT      (EEPROM_ADDR_LOW_LIMIT + sizeof(float))

// Faktor, um den ADC-Wert auf die ursprüngliche Eingangsspannung vor
// einem eventuellen Spannungsteiler umzurechnen.
// Beispiel: Wenn ein 10:1 Spannungsteiler verwendet wird, ist der Faktor 10.0.
// Hier Annahme 1.0 (kein Teiler oder direkter Anschluss der aufbereiteten Spannung).
#define DEFAULT_SCALING_FACTOR 478.278f // Standardwert, falls EEPROM leer ist
#define DEFAULT_VOLTAGE_OFFSET 0.0f
float g_scaling_factor = DEFAULT_SCALING_FACTOR;
float g_voltage_offset = DEFAULT_VOLTAGE_OFFSET; // Korrektur-Offset

// Glättungsfaktor ALPHA der EMA. Wird seit V1.1.0 NUR noch für die lokale
// Live-Anzeige verwendet – an den Controller geht der ungeglättete 2-Zyklen-RMS
// (geringere Latenz für die Regelung). Kleiner = ruhigere Anzeige, träger.
#define ALPHA 0.1f

// Pin für Kommunikation mit Variac Controller definieren (USART1 TX)
#define UART_COM_TX_PIN PA9

// Pin für on-board LED
#define LED PC13

// Globale Variablen
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;
TIM_HandleTypeDef htim_trigger; // Timer für ADC-Trigger
// Kommunikation mit dem Variac-Controller läuft über die Arduino-HardwareSerial "Serial1"
// (USART1, PA9/PA10). Der STM32-Core verwaltet IRQ und RX-Ringpuffer.

bool is_live_display_active = false;

volatile uint16_t adc_buffer[DMA_BUFFER_SIZE];
volatile bool new_rms_data_ready = false;
volatile float last_rms_value = 0.0f;
volatile float smoothed_rms_value = 0.0f;
bool is_first_measurement = true; // Hilfsvariable für den ersten Wert
volatile float g_adc_zero_offset = 3071.0f; // Globaler Offset, der dem Nullpunkt der AC-Schwingung entspricht, mit einem Standardwert initialisiert

// Puffer zum Sammeln eines Befehls vom Serial Monitor
char command_buffer[50];
uint8_t command_pos = 0;

// --- Befehls-Link zum Controller über Serial1 (USART1, PA9/PA10), bidirektional ---
// Befehls-Frame  (Controller -> Voltmeter): 0xA5 CMD LEN [payload] CHK 0xBB
// Antwort-Frame  (Voltmeter -> Controller): 0xB5 CMD LEN [payload] CHK 0xBB
// CHK = (CMD + LEN + sum(payload)) & 0xFF  (Ringpuffer/IRQ übernimmt der Arduino-Core via Serial1)
#define LINK_CMD_SOF         0xA5
#define LINK_RSP_SOF         0xB5
#define LINK_EOF             0xBB
#define LINK_CMD_GET_VERSION  0x01
#define LINK_CMD_GET_STATUS   0x02
#define LINK_CMD_SET_FACTOR   0x10
#define LINK_CMD_SET_OFFSET   0x11
#define LINK_CMD_RECAL        0x20
#define LINK_CMD_CAL3_MEASURE 0x21
#define LINK_CMD_CAL3_FINISH  0x22
#define LINK_CMD_REBOOT       0x30
#define LINK_CMD_RESET_DEFAULTS 0x31
#define LINK_CMD_ENTER_BOOTLOADER 0x40

// Geführte 3-Punkt-Kalibrierung (über den Link): pro Punkt die anliegende Referenzspannung
#define CAL3_POINTS 3
float  cal3_actual[CAL3_POINTS]   = {0};
double cal3_measured[CAL3_POINTS] = {0};
bool   cal3_have[CAL3_POINTS]     = {false, false, false};

// --- Prototypen der Initialisierungsfunktionen ---
void MX_GPIO_Init(void);
void MX_DMA_Init(void);
void MX_ADC1_Init(void);
void MX_TIM_Trigger_Init(void); // Timer für ADC-Triggerung
void Error_Handler_Intern(void);
void performAutoZeroCalibration(void);
// ISR mit C-Linkage vordeklarieren, damit der .ino-Präprozessor keinen
// kollidierenden C++-Prototyp erzeugt (PlatformIO-Build).
extern "C" void DMA1_Channel1_IRQHandler(void);

void loadConfigFromEEPROM() {
  if (EEPROM.read(EEPROM_ADDR_START) == EEPROM_MAGIC_NUMBER) {
    // EEPROM ist initialisiert, lade alle Werte
    EEPROM.get(EEPROM_ADDR_SCALING_FACTOR, g_scaling_factor);
    EEPROM.get(EEPROM_ADDR_VOLTAGE_OFFSET, g_voltage_offset); // NEU
    Serial.println("Konfiguration aus dem EEPROM geladen.");
  } else {
    // Erster Start, schreibe alle Standardwerte
    Serial.println("EEPROM nicht initialisiert. Schreibe Standard-Konfiguration.");
    EEPROM.write(EEPROM_ADDR_START, EEPROM_MAGIC_NUMBER);
    EEPROM.put(EEPROM_ADDR_SCALING_FACTOR, g_scaling_factor);
    EEPROM.put(EEPROM_ADDR_VOLTAGE_OFFSET, g_voltage_offset); // NEU
  }
  Serial.print("Skalierungsfaktor: "); Serial.println(g_scaling_factor);
  Serial.print("Spannungs-Offset: "); Serial.println(g_voltage_offset); // NEU
}

// Blockiert, bis der erwartete Befehl vom seriellen Monitor kommt
void waitForSerialCommand(const char* expected_cmd) {
  while (true) {
    if (Serial.available() > 0) {
      String cmd = Serial.readStringUntil('\n');
      cmd.trim(); // Entfernt Leerzeichen und Zeilenumbrüche
      if (cmd.equalsIgnoreCase(expected_cmd)) {
        break;
      }
    }
    delay(100);
  }
}

void performGuidedCalibration() {
  const int num_points = 3;
  const float actual_voltages[num_points] = {50.0, 125.0, 230.0};
  double measured_adc_volts[num_points] = {0.0};
  const int samples_to_average = 50; // Mittelwert über 2 Sekunden (50 * 40ms)

  Serial.println("\n--- Gefuehrte 3-Punkt-Kalibrierung gestartet ---");
  Serial.println("WARNUNG: Live-Anzeige wird gestoppt. Bitte 'ok' senden, um fortzufahren.");
  is_live_display_active = false;
  waitForSerialCommand("ok");

  for (int i = 0; i < num_points; i++) {
    // 1. Benutzer auffordern, die Spannung anzulegen
    Serial.print("\nSchritt "); Serial.print(i + 1); Serial.print("/"); Serial.print(num_points);
    Serial.print(": Bitte exakt "); Serial.print(actual_voltages[i]);
    Serial.println("V anlegen und dann 'ok' senden.");
    waitForSerialCommand("ok");
    Serial.print("Messe...");

    // 2. Spannung über eine gewisse Zeit messen und mitteln
    double accumulated_adc_volts = 0.0;
    for (int s = 0; s < samples_to_average; s++) {
      while (!new_rms_data_ready) { delay(1); } // Auf neue Messung warten
      new_rms_data_ready = false;

      // Berechne den unskalierten "rms_volts_at_adc" Wert
      double sum_of_squares = 0.0;
      for (int k=0; k < DMA_BUFFER_SIZE; k++) {
        double ac_sample_counts = (double)adc_buffer[k] - g_adc_zero_offset;
        sum_of_squares += ac_sample_counts * ac_sample_counts;
      }
      double rms_counts = sqrt(sum_of_squares / DMA_BUFFER_SIZE);
      accumulated_adc_volts += (rms_counts / ADC_MAX_VALUE) * ADC_REFERENCE_VOLTAGE;
      
      Serial.print(".");
    }
    measured_adc_volts[i] = accumulated_adc_volts / samples_to_average;
    Serial.print(" Fertig. Gemessen: "); Serial.println(measured_adc_volts[i], 4);
  }

  // 3. Lineare Regression berechnen, um m (g_scaling_factor) und b (g_voltage_offset) zu finden
  double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
  for (int i = 0; i < num_points; i++) {
    sum_x += measured_adc_volts[i];
    sum_y += actual_voltages[i];
    sum_xy += measured_adc_volts[i] * actual_voltages[i];
    sum_x2 += measured_adc_volts[i] * measured_adc_volts[i];
  }

  // Formeln für m (slope) und b (intercept)
  double m = (num_points * sum_xy - sum_x * sum_y) / (num_points * sum_x2 - sum_x * sum_x);
  double b = (sum_y - m * sum_x) / num_points;

  // 4. Neue Kalibrierwerte speichern
  g_scaling_factor = (float)m;
  g_voltage_offset = (float)b;

  EEPROM.put(EEPROM_ADDR_SCALING_FACTOR, g_scaling_factor);
  EEPROM.put(EEPROM_ADDR_VOLTAGE_OFFSET, g_voltage_offset);

  Serial.println("\n--- Kalibrierung abgeschlossen ---");
  Serial.print("Neuer Skalierungsfaktor (m): "); Serial.println(g_scaling_factor);
  Serial.print("Neuer Spannungs-Offset (b): "); Serial.println(g_voltage_offset);
  Serial.println("Werte wurden im EEPROM gespeichert.");
}

void processCommand() {
  // Entferne eventuelle Zeilenumbrüche am Ende des Befehls
  command_buffer[command_pos] = '\0';
  strtok(command_buffer, "\r\n");

  // Befehle auswerten
  if (strcmp(command_buffer, "?") == 0) {
    Serial.println("\n--- Befehlsmenue ---");
    Serial.println("?           - Diese Hilfe anzeigen");
    Serial.println("?version    - Firmware-Version anzeigen");
    Serial.println("?status     - Status und kalibrierten Offset anzeigen");
    Serial.println("?live       - Live-Anzeige der Messwerte starten");
    Serial.println("!stop       - Live-Anzeige stoppen");
    Serial.println("!recal      - Auto-Zero-Kalibrierung erneut starten");
    Serial.println("?factor     - Aktuellen Skalierungsfaktor anzeigen");
    Serial.println("!setfactor=<wert> - Neuen Skalierungsfaktor setzen & speichern");
    Serial.println("!calibrate  - Gefuehrte 3-Punkt-Kalibrierung");
    Serial.println("--------------------");
    is_live_display_active = false;
  }
  else if (strcmp(command_buffer, "?live") == 0) {
    Serial.println("Live-Anzeige gestartet. Mit '!stop' beenden.");
    is_live_display_active = true;
  }
  else if (strcmp(command_buffer, "!stop") == 0) {
    is_live_display_active = false;
    Serial.println("\nLive-Anzeige gestoppt."); // Mit Newline, um die Zeile zu wechseln
  }
  else if (strcmp(command_buffer, "?status") == 0) {
    char response[100];
    snprintf(response, sizeof(response), "Status OK. Calibrated Offset: %.2f", g_adc_zero_offset);
    Serial.println(response);
  } 
  else if (strcmp(command_buffer, "?version") == 0) {
    Serial.println(FW);
  }
  else if (strcmp(command_buffer, "!recal") == 0) {
    is_live_display_active = false; // Live-Anzeige während Kalibrierung stoppen
    Serial.println("OK. Starte Neukalibrierung...");
    performAutoZeroCalibration();
  }
  else if (strncmp(command_buffer, "!setfactor=", 11) == 0) {
    // Extrahiere den Wert nach dem "="
    float new_factor = atof(command_buffer + 11);
    
    // Einfache Plausibilitätsprüfung
    if (new_factor > 100.0f && new_factor < 1000.0f) {
      g_scaling_factor = new_factor;
      
      // Speichere den neuen Wert permanent im EEPROM
      EEPROM.put(EEPROM_ADDR_START + 1, g_scaling_factor);
      
      Serial.print("OK. Neuer Skalierungsfaktor gesetzt & gespeichert: ");
      Serial.println(g_scaling_factor);
    } else {
      Serial.println("ERR: Ungueltiger oder ausserhalb des Bereichs liegender Wert.");
    }
  }
  else if (strcmp(command_buffer, "?factor") == 0) {
    Serial.print("Aktueller Skalierungsfaktor: ");
    Serial.println(g_scaling_factor);
  }
  else if (strcmp(command_buffer, "!calibrate") == 0) {
    performGuidedCalibration();
  }
  else {
    Serial.print("ERR: Unbekannter Befehl '");
    Serial.print(command_buffer);
    Serial.println("'. Sende '?' für Hilfe.");
  }
  
  // Puffer für den nächsten Befehl zurücksetzen
  command_pos = 0;
  command_buffer[0] = '\0';
}

void setup() {
  pinMode(LED, OUTPUT);
 
  Serial.begin(115200);
  unsigned long startTime = millis();
  unsigned long lastBlinkTime = 0;
  bool ledState = HIGH; // LED auf BluePill ist 'low-active' (leuchtet bei LOW)

  // Warte maximal 5 Sekunden ODER bis Serial verbunden ist
  while (millis() - startTime < 5000 && !Serial) {
    
    // Prüfe, ob 250ms für das Blinken vergangen sind
    if (millis() - lastBlinkTime > 250) {
      digitalWrite(LED, ledState); // LED-Zustand setzen
      ledState = !ledState;         // Zustand umkehren (an/aus)
      lastBlinkTime = millis();     // Zeit des letzten Blinkens speichern
    }
  }

  // Sorge dafür, dass die LED am Ende aus ist (HIGH bei PC13)
  digitalWrite(LED, HIGH); 

  pinMode(SYSLED_PIN, OUTPUT);
  digitalWrite(SYSLED_PIN, HIGH);

  Serial.println("STM32F103 AC RMS Voltmeter - Update alle 40ms");
  // Referenziert fw_version_tag (sonst entfernt --gc-sections ihn) und zeigt die Version (#33).
  Serial.print("Version: "); Serial.println(fw_version_tag + 8); // +8: "@@VMFW@@" überspringen
  Serial.print("Sampling Frequenz: "); Serial.print(SAMPLING_FREQUENCY_HZ); Serial.println(" Hz");
  Serial.print("DMA Buffer Groesse: "); Serial.println(DMA_BUFFER_SIZE);

  loadConfigFromEEPROM();

  MX_GPIO_Init();
  MX_DMA_Init();          // DMA vor ADC initialisieren
  MX_ADC1_Init();
  MX_TIM_Trigger_Init();  // Timer initialisieren
  Serial1.begin(115200);  // Link zum Controller (USART1, PA9/PA10)

  // ADC Kalibrierung (wichtig für genaue Ergebnisse)
  if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK) {
    Serial.println("ADC Kalibrierung fehlgeschlagen!");
    Error_Handler_Intern();
  }

  // Timer starten, der den ADC triggert
  if (HAL_TIM_Base_Start(&htim_trigger) != HAL_OK) {
    Serial.println("Timer Start fehlgeschlagen!");
    Error_Handler_Intern();
  }

  // ADC mit DMA starten
  // Der ADC wartet auf Trigger-Events vom Timer
  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_buffer, DMA_BUFFER_SIZE) != HAL_OK) {
    Serial.println("ADC mit DMA Start fehlgeschlagen!");
    Error_Handler_Intern();
  }

  // Zero Offset kalibrieren
  performAutoZeroCalibration();

  Serial.println("Messung gestartet...");
}

void loop() {

  // Befehle vom Controller (USART1) verarbeiten (Ringpuffer aus dem RX-Interrupt)
  processControllerLink();

  if (new_rms_data_ready) {
    new_rms_data_ready = false; // Flag zurücksetzen

    digitalWrite(SYSLED_PIN, LOW);

    // Temporären Puffer für Berechnungen erstellen, um Race Conditions zu vermeiden,
    // falls der Interrupt den adc_buffer während der Berechnung aktualisiert.
    uint16_t calculation_buffer[DMA_BUFFER_SIZE];
    // Interrupts kurz sperren während des Kopierens, um Datenkonsistenz zu sichern
    __disable_irq();
    for(int i=0; i < DMA_BUFFER_SIZE; i++) {
        calculation_buffer[i] = adc_buffer[i];
    }
    __enable_irq();


    double sum_of_squares = 0.0;
    for (int i = 0; i < DMA_BUFFER_SIZE; i++) {
      // 1. ADC-Rohwert in tatsächliche AC-Komponente umrechnen (bezogen auf den Nullpunkt)
      //    Das Ergebnis ist hier noch in "ADC counts" relativ zum AC-Nullpunkt.
      double ac_sample_counts = (double)calculation_buffer[i] - g_adc_zero_offset;

      // 2. Diesen Wert quadrieren
      sum_of_squares += ac_sample_counts * ac_sample_counts;
    }

    // 3. Mittelwert der Quadrate
    double mean_of_squares_counts = sum_of_squares / DMA_BUFFER_SIZE;

    // 4. Wurzel aus dem Mittelwert (RMS in "ADC counts")
    double rms_counts = sqrt(mean_of_squares_counts);

    // 5. RMS von "ADC counts" in Volt umrechnen (bezogen auf Vref)
    double rms_volts_at_adc = (rms_counts / ADC_MAX_VALUE) * ADC_REFERENCE_VOLTAGE;

    // 6. Auf tatsächliche Eingangsspannung skalieren (externer Spannungsteiler berücksichtigen)
    last_rms_value = (float)(rms_volts_at_adc * g_scaling_factor) + g_voltage_offset;

    // Beim allerersten Start, initialisiere den geglätteten Wert direkt,
    // um nicht bei 0 anfangen zu müssen.
    if (is_first_measurement) {
      smoothed_rms_value = last_rms_value;
      is_first_measurement = false;
    } else {
      // EMA-Formel: Mische den neuen Wert mit dem alten geglätteten Wert.
      smoothed_rms_value = (ALPHA * last_rms_value) + ((1.0f - ALPHA) * smoothed_rms_value);
    }

    // An den Controller den ungeglätteten 2-Zyklen-RMS senden (~40 ms Latenz).
    // Die EMA (smoothed_rms_value) dient nur noch der lokalen Live-Anzeige.
    sendRMSValue(last_rms_value);

    if (is_live_display_active) {
      char live_buffer[80];
      snprintf(live_buffer, sizeof(live_buffer), 
              "Ist: %6.1f V | Geglättet: %6.1f V", 
              last_rms_value, smoothed_rms_value);
      Serial.println(live_buffer);
    }

    digitalWrite(SYSLED_PIN, HIGH);
  }

  // --- NEU: Block zur Verarbeitung von Befehlen vom PC-Terminal ---
  while (Serial.available() > 0) {
    char received_char = Serial.read();

    // Prüfen, ob das Endzeichen (Enter/Newline) empfangen wurde
    if (received_char == '\n') {   
      if (command_pos > 0) { // Nur verarbeiten, wenn der Befehl nicht leer ist
        processCommand(); // Den gesammelten Befehl verarbeiten
      }
      command_pos = 0; // Puffer für den nächsten Befehl zurücksetzen
      command_buffer[0] = '\0';
    } 
    // Füge das Zeichen zum Puffer hinzu, aber nur, wenn noch Platz ist
    else if (command_pos < sizeof(command_buffer) - 1) {
      command_buffer[command_pos++] = received_char;
    }
  }

}

// --- Initialisierungsfunktionen ---

void MX_GPIO_Init(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  // Takte für GPIOs aktivieren (hier nur für PA0)
  __HAL_RCC_GPIOA_CLK_ENABLE();

  // ADC-Pin als Analog-Eingang konfigurieren
  GPIO_InitStruct.Pin = GPIO_PIN_0; // Entsprechend ADC_PIN
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

void MX_DMA_Init(void) {
  __HAL_RCC_DMA1_CLK_ENABLE();

  hdma_adc1.Instance = DMA1_Channel1; // DMA1 Channel 1 für ADC1 auf STM32F1
  hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
  hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
  hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD; // ADC-Daten sind 16-Bit (uint16_t)
  hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
  hdma_adc1.Init.Mode = DMA_CIRCULAR; // Kontinuierlich Daten sammeln
  hdma_adc1.Init.Priority = DMA_PRIORITY_HIGH;
  if (HAL_DMA_Init(&hdma_adc1) != HAL_OK) {
    Error_Handler_Intern();
  }

  // DMA-Handle mit ADC-Handle verknüpfen
  __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);

  // DMA-Interrupt aktivieren
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0); // Hohe Priorität
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}

void MX_ADC1_Init(void) {
  // Diese Funktion wurde speziell für die ältere HAL-Version des STM32F1 Cores angepasst.

  ADC_ChannelConfTypeDef sConfig = {0};

  // 1. Takt für den ADC konfigurieren (wie im vorherigen Schritt korrigiert)
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  __HAL_RCC_ADC1_CLK_ENABLE();
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6; // 72MHz / 6 = 12MHz (max 14MHz)
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler_Intern();
  }

  // 2. ADC Hauptkonfiguration
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE; // Wichtig: Für nur einen Kanal auf DISABLE setzen.
  hadc1.Init.ContinuousConvMode = DISABLE; // Korrekt: Wir wollen durch einen externen Timer triggern.
  hadc1.Init.DiscontinuousConvMode = DISABLE; // Nicht relevant bei einem Kanal.
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T3_TRGO; // Korrekt: Unser Trigger ist TIM3_TRGO.
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT; // Korrekt: Daten rechtsbündig.
  hadc1.Init.NbrOfConversion = 1; // Korrekt: Wir haben eine Konvertierung pro Trigger.
  
  // Die folgenden Felder aus dem ursprünglichen Code existieren in dieser HAL-Version NICHT und wurden entfernt:
  // - ClockPrescaler (wird jetzt über RCC oben gemacht)
  // - Resolution (ist fest 12-Bit)
  // - ExternalTrigConvEdge
  // - DMAContinuousRequests
  // - EOCSelection

  if (HAL_ADC_Init(&hadc1) != HAL_OK) {
    Error_Handler_Intern();
  }

  // 3. ADC Kanalkonfiguration
  sConfig.Channel = ADC_CHANNEL_NUM; // Unser definierter Kanal (z.B. ADC_CHANNEL_0)
  sConfig.Rank = ADC_REGULAR_RANK_1; // Rank 1 für den ersten (und einzigen) Kanal
  
  // Die Namen für die Sampling Time sind bei dieser HAL-Version anders.
  // ADC_SAMPLETIME_15CYCLES existiert nicht. Wir nehmen einen der verfügbaren Werte.
  // Verfügbar sind z.B.: ADC_SAMPLETIME_1CYCLE_5, ADC_SAMPLETIME_7CYCLES_5, ADC_SAMPLETIME_13CYCLES_5, ...
  sConfig.SamplingTime = ADC_SAMPLETIME_13CYCLES_5; // Ein guter Mittelweg.
  
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler_Intern();
  }
}

void MX_TIM_Trigger_Init(void) {
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  // Annahme: SystemCoreClock ist 72MHz (Standard für STM32F103 mit externem Quarz)
  // Timer-Takt ist PCLK1 (für TIM2,3,4), der auch 72MHz sein kann (wenn APB1 Prescaler = 1)
  // oder 36MHz (wenn APB1 Prescaler = 2, Default in CubeMX oft).
  // Für dieses Beispiel nehmen wir an, der Timer-Takt ist 72MHz.
  // Timer-Frequenz = Timer-Clock / (Prescaler + 1) / (Period + 1)
  // Wir wollen SAMPLING_FREQUENCY_HZ = 5000 Hz
  // 5000 = 72,000,000 / (PSC + 1) / (ARR + 1)
  // (PSC + 1) * (ARR + 1) = 72,000,000 / 5000 = 14400

  // Wähle Prescaler (PSC) und Periode (ARR)
  // z.B. PSC = 71 => PSC+1 = 72
  // ARR+1 = 14400 / 72 = 200 => ARR = 199
  uint32_t prescaler_val = 72 - 1; // Ergibt 1MHz Timer Counter Clock (72MHz / 72)
  uint32_t period_val = (SystemCoreClock / (prescaler_val + 1) / SAMPLING_FREQUENCY_HZ) - 1; // (1MHz / 5000Hz) - 1 = 200 - 1 = 199

  htim_trigger.Instance = TIM3; // TIM3 verwenden. Kann auch TIM2 oder TIM4 sein.
                               // Für TIM1 (Advanced Timer) ist die Konfiguration etwas anders.
  __HAL_RCC_TIM3_CLK_ENABLE(); // Takt für TIM3 aktivieren

  htim_trigger.Init.Prescaler = prescaler_val;
  htim_trigger.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim_trigger.Init.Period = period_val;
  htim_trigger.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim_trigger.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE; // Wichtig für gleichmäßige Trigger
  if (HAL_TIM_Base_Init(&htim_trigger) != HAL_OK) {
    Error_Handler_Intern();
  }

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim_trigger, &sClockSourceConfig) != HAL_OK) {
    Error_Handler_Intern();
  }

  // Konfiguriere den Timer, um ein Trigger-Ereignis (TRGO) bei jedem Update-Event (Überlauf) zu senden
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE; // Trigger bei Update Event (Overflow)
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim_trigger, &sMasterConfig) != HAL_OK) {
    Error_Handler_Intern();
  }
}

// --- Interrupt Service Routinen (ISRs) ---
#ifdef __cplusplus
extern "C" {
#endif

// DMA1 Channel 1 Interrupt Handler (für ADC1 auf STM32F1)
void DMA1_Channel1_IRQHandler(void) {
  HAL_DMA_IRQHandler(&hdma_adc1);
}

#ifdef __cplusplus
}
#endif

// Callback, der aufgerufen wird, wenn der DMA-Puffer voll ist
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
  if (hadc->Instance == ADC1) {
    // Der gesamte Block von DMA_BUFFER_SIZE (200) Werten ist jetzt im `adc_buffer`.
    // Setze ein Flag, damit der Hauptloop die RMS-Berechnung durchführt.
    new_rms_data_ready = true;
  }
}

// Optional: Callback für halb vollen DMA-Puffer (nützlich für Ping-Pong-Pufferung)
// void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef* hadc) {
//   if (hadc->Instance == ADC1) {
//     // Erste Hälfte des Puffers ist voll
//   }
// }

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc) {
  Serial.print("ADC Error: 0x");
  Serial.println(hadc->ErrorCode, HEX);
  Error_Handler_Intern();
}

void Error_Handler_Intern(void) {
  Serial.println("Error_Handler wurde aufgerufen!");
  __disable_irq();
  while (1) {
    // Hier könnte eine LED blinken, um einen Fehler anzuzeigen
    // digitalWrite(LED_BUILTIN, HIGH); delay(100);
    // digitalWrite(LED_BUILTIN, LOW); delay(100);
  }
}

void performAutoZeroCalibration() {
  digitalWrite(LED, LOW);
  Serial.println("Starte automatische Nullpunkt-Kalibrierung...");
  
  pinMode(RELAY_PIN, OUTPUT);
  
  // 1. Relais aktivieren (Eingang auf GND legen)
  digitalWrite(RELAY_PIN, HIGH); // Annahme: HIGH schaltet den Transistor und damit das Relais an
  delay(200); // Warten, bis das Relais mechanisch geschaltet hat und alles stabil ist

  // 2. ADC/DMA für eine Messung starten, um einen Block von Daten zu sammeln
  //    Wir nutzen die bestehende Infrastruktur!
  new_rms_data_ready = false; // Flag zurücksetzen
  // Der ADC/DMA ist bereits von setup() gestartet und läuft im Circular Mode.
  // Wir warten hier einfach auf den nächsten "Conversion Complete" Interrupt.
  while(!new_rms_data_ready) {
      // Warte, bis der DMA einen vollen Puffer geliefert hat
      // Ein Yield oder Delay hier kann bei manchen Cores nützlich sein
      delay(1); 
  }
  new_rms_data_ready = false; // Flag sofort wieder für die Hauptschleife zurücksetzen

  // 3. Den Durchschnitt der Rohwerte berechnen
  long long sum_raw_adc = 0;
  // Wir kopieren die Werte in einen sicheren Puffer, auch wenn es hier nicht 
  // unbedingt nötig wäre, ist es eine saubere Methode.
  uint16_t cal_buffer[DMA_BUFFER_SIZE];
  __disable_irq();
  for(int i=0; i < DMA_BUFFER_SIZE; i++) {
      cal_buffer[i] = adc_buffer[i];
  }
  __enable_irq();

  for (int i = 0; i < DMA_BUFFER_SIZE; i++) {
    sum_raw_adc += cal_buffer[i];
  }
  
  // 4. Den genauen Offset in der globalen Variable speichern
  g_adc_zero_offset = (double)sum_raw_adc / DMA_BUFFER_SIZE;

  Serial.print("Kalibrierung abgeschlossen. Neuer Offset: ");
  Serial.println(g_adc_zero_offset, 4);

  // 5. Relais deaktivieren (Eingang wieder auf AC-Signal legen)
  digitalWrite(RELAY_PIN, LOW);
  delay(200); // Erneut warten, bis das Relais stabil ist
  
  Serial.println("Kalibrierung beendet. Starte normale Messung.");
  delay(5000);
  digitalWrite(LED, HIGH);
}

void sendRMSValue(float rms_value) {
  uint8_t tx_buffer[5];

  // 1. Float in einen uint16_t umwandeln (z.B. 230.5V -> 2305)
  uint16_t payload = (uint16_t)(rms_value * 10.0f);

  // 2. Paket zusammenbauen
  tx_buffer[0] = 0xAA; // Start of Frame
  tx_buffer[1] = (payload >> 8) & 0xFF; // High-Byte
  tx_buffer[2] = payload & 0xFF;        // Low-Byte
  tx_buffer[3] = tx_buffer[1] + tx_buffer[2]; // Checksum (simple Addition)
  tx_buffer[4] = 0xBB; // End of Frame

  // 3. Paket über Serial1 (USART1) an den Controller senden.
  Serial1.write(tx_buffer, sizeof(tx_buffer));
}

// Sendet einen Antwort-Frame an den Controller: 0xB5 CMD LEN [data] CHK 0xBB
void sendControllerResponse(uint8_t cmd, const uint8_t* data, uint8_t len) {
  uint8_t buf[5 + 255];
  uint8_t chk = cmd + len;
  uint8_t i = 0;
  buf[i++] = LINK_RSP_SOF;
  buf[i++] = cmd;
  buf[i++] = len;
  for (uint8_t k = 0; k < len; k++) { buf[i++] = data[k]; chk += data[k]; }
  buf[i++] = chk;
  buf[i++] = LINK_EOF;
  Serial1.write(buf, i);
}

// Mittelt den unskalierten RMS-Wert (ADC-Volt) über N Messzyklen (~N*40 ms).
// Blockierend – nur für die Kalibrierung gedacht.
double measureAdcVolts(int samples) {
  double accumulated = 0.0;
  for (int s = 0; s < samples; s++) {
    while (!new_rms_data_ready) { delay(1); }
    new_rms_data_ready = false;
    double sum_of_squares = 0.0;
    for (int k = 0; k < DMA_BUFFER_SIZE; k++) {
      double ac = (double)adc_buffer[k] - g_adc_zero_offset;
      sum_of_squares += ac * ac;
    }
    double rms_counts = sqrt(sum_of_squares / DMA_BUFFER_SIZE);
    accumulated += (rms_counts / ADC_MAX_VALUE) * ADC_REFERENCE_VOLTAGE;
  }
  return accumulated / samples;
}

// Springt in den eingebauten STM32-System-Bootloader (ROM, AN3155) bei 0x1FFFF000.
// Danach kann der Controller die Voltmeter-FW über USART1 (8E1) neu flashen (#30).
// Kehrt nicht zurück. Aufrufer hat den ACK bereits gesendet.
void jumpToSystemBootloader(void) {
  const uint32_t SYSMEM_BASE = 0x1FFFF000UL;

  // 1) Restliche TX-Bytes (ACK) sicher rausschieben, bevor wir alles abschalten.
  Serial1.flush();
  delay(50);

  // 2) Peripherie + Takt auf Reset-Zustand bringen (wie nach einem echten Reset).
  //    Entscheidend: unser ADC/DMA/Timer läuft sonst weiter und stört den ROM-Loader.
  //    (Beim BOOT0-Reset ist genau diese Peripherie im Reset – darum klappt der Weg dort.)
  HAL_DeInit();
  HAL_RCC_DeInit();

  // 3) SysTick stilllegen.
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL  = 0;

  // 4) Vektortabelle auf System-Memory. Der STM32F1 kann den Speicher nicht per
  //    Software nach 0x00000000 spiegeln (kein SYSCFG), daher Cortex-M3-VTOR.
  SCB->VTOR = SYSMEM_BASE;

  // 5) Stackpointer und Einsprung aus der System-Memory-Vektortabelle holen, springen.
  //    Interrupts bleiben global aktiv – der F1-ROM-Loader nutzt die USART-IRQ.
  uint32_t bootStackPtr = *(volatile uint32_t*)(SYSMEM_BASE);
  uint32_t bootEntry    = *(volatile uint32_t*)(SYSMEM_BASE + 4);
  __set_MSP(bootStackPtr);

  void (*bootJump)(void) = (void (*)(void))bootEntry;
  bootJump();

  // Falls der Sprung wider Erwarten zurückkehrt: hängen lassen (Watchdog/Reset könnte greifen).
  while (1) { }
}

// Reagiert auf einen vollständig empfangenen Befehl vom Controller.
void handleControllerCommand(uint8_t cmd, const uint8_t* payload, uint8_t len) {
  switch (cmd) {
    case LINK_CMD_GET_VERSION: {
      const char* v = FW;
      sendControllerResponse(LINK_CMD_GET_VERSION, (const uint8_t*)v, (uint8_t)strlen(v));
      break;
    }
    case LINK_CMD_GET_STATUS: {
      // Antwort: 3 floats -> Skalierungsfaktor, Spannungs-Offset, ADC-Nullpunkt
      uint8_t buf[12];
      float f1 = g_scaling_factor;
      float f2 = g_voltage_offset;
      float f3 = (float)g_adc_zero_offset;
      memcpy(buf + 0, &f1, 4);
      memcpy(buf + 4, &f2, 4);
      memcpy(buf + 8, &f3, 4);
      sendControllerResponse(LINK_CMD_GET_STATUS, buf, sizeof(buf));
      break;
    }
    case LINK_CMD_SET_FACTOR: {
      // Payload: 1 float (Skalierungsfaktor). Antwort: 1 Byte (1=ok, 0=abgelehnt).
      uint8_t ok = 0;
      if (len == 4) {
        float nf;
        memcpy(&nf, payload, 4);
        if (nf > 100.0f && nf < 1000.0f) {     // gleiche Plausibilität wie Konsole
          g_scaling_factor = nf;
          EEPROM.put(EEPROM_ADDR_SCALING_FACTOR, g_scaling_factor);
          ok = 1;
        }
      }
      sendControllerResponse(LINK_CMD_SET_FACTOR, &ok, 1);
      break;
    }
    case LINK_CMD_SET_OFFSET: {
      // Payload: 1 float (Spannungs-Offset in V). Antwort: 1 Byte (1=ok, 0=abgelehnt).
      uint8_t ok = 0;
      if (len == 4) {
        float no;
        memcpy(&no, payload, 4);
        if (no >= -50.0f && no <= 50.0f) {     // plausibler Korrektur-Offset
          g_voltage_offset = no;
          EEPROM.put(EEPROM_ADDR_VOLTAGE_OFFSET, g_voltage_offset);
          ok = 1;
        }
      }
      sendControllerResponse(LINK_CMD_SET_OFFSET, &ok, 1);
      break;
    }
    case LINK_CMD_RECAL: {
      // ACK zuerst senden, dann die (blockierende) Auto-Zero-Kalibrierung ausführen.
      uint8_t ok = 1;
      sendControllerResponse(LINK_CMD_RECAL, &ok, 1);
      is_live_display_active = false;
      performAutoZeroCalibration();
      break;
    }
    case LINK_CMD_CAL3_MEASURE: {
      // Payload: [index(1)][actual_voltage(float,4)] -> Punkt messen & speichern.
      uint8_t ok = 0;
      if (len == 5) {
        uint8_t idx = payload[0];
        float actual;
        memcpy(&actual, payload + 1, 4);
        if (idx < CAL3_POINTS) {
          is_live_display_active = false;
          cal3_actual[idx]   = actual;
          cal3_measured[idx] = measureAdcVolts(50);  // ~2 s Mittelung
          cal3_have[idx]     = true;
          ok = 1;
        }
      }
      sendControllerResponse(LINK_CMD_CAL3_MEASURE, &ok, 1);
      break;
    }
    case LINK_CMD_CAL3_FINISH: {
      // Lineare Regression über alle gemessenen Punkte -> m (Faktor), b (Offset), speichern.
      uint8_t resp[9] = {0};   // [ok(1)][m(float)][b(float)]
      int n = 0;
      double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
      for (int i = 0; i < CAL3_POINTS; i++) {
        if (!cal3_have[i]) continue;
        n++;
        sum_x  += cal3_measured[i];
        sum_y  += cal3_actual[i];
        sum_xy += cal3_measured[i] * cal3_actual[i];
        sum_x2 += cal3_measured[i] * cal3_measured[i];
      }
      double denom = (double)n * sum_x2 - sum_x * sum_x;
      if (n >= 2 && denom != 0.0) {
        double m = ((double)n * sum_xy - sum_x * sum_y) / denom;
        double b = (sum_y - m * sum_x) / n;
        g_scaling_factor = (float)m;
        g_voltage_offset = (float)b;
        EEPROM.put(EEPROM_ADDR_SCALING_FACTOR, g_scaling_factor);
        EEPROM.put(EEPROM_ADDR_VOLTAGE_OFFSET, g_voltage_offset);
        for (int i = 0; i < CAL3_POINTS; i++) cal3_have[i] = false; // zurücksetzen
        resp[0] = 1;
        memcpy(resp + 1, &g_scaling_factor, 4);
        memcpy(resp + 5, &g_voltage_offset, 4);
      }
      sendControllerResponse(LINK_CMD_CAL3_FINISH, resp, sizeof(resp));
      break;
    }
    case LINK_CMD_REBOOT: {
      uint8_t ok = 1;
      sendControllerResponse(LINK_CMD_REBOOT, &ok, 1);
      Serial1.flush();   // sicherstellen, dass der ACK rausgeht
      delay(50);
      NVIC_SystemReset();
      break;
    }
    case LINK_CMD_RESET_DEFAULTS: {
      g_scaling_factor = DEFAULT_SCALING_FACTOR;
      g_voltage_offset = DEFAULT_VOLTAGE_OFFSET;
      EEPROM.put(EEPROM_ADDR_SCALING_FACTOR, g_scaling_factor);
      EEPROM.put(EEPROM_ADDR_VOLTAGE_OFFSET, g_voltage_offset);
      uint8_t ok = 1;
      sendControllerResponse(LINK_CMD_RESET_DEFAULTS, &ok, 1);
      break;
    }
    case LINK_CMD_ENTER_BOOTLOADER: {
      uint8_t ok = 1;
      sendControllerResponse(LINK_CMD_ENTER_BOOTLOADER, &ok, 1);
      jumpToSystemBootloader();  // kehrt nicht zurück (flush + delay sind drin)
      break;
    }
    default:
      break; // unbekannter Befehl -> vorerst ignorieren
  }
}

// Liest den RX-Ringpuffer und parst Befehls-Frames (0xA5 CMD LEN [payload] CHK 0xBB).
void processControllerLink() {
  static uint8_t st = 0;            // 0=SOF,1=CMD,2=LEN,3=DATA,4=CHK,5=EOF
  static uint8_t cmd, len, idx, chk;
  static uint8_t payload[64];

  while (Serial1.available() > 0) {
    uint8_t b = (uint8_t)Serial1.read();

    switch (st) {
      case 0: if (b == LINK_CMD_SOF) st = 1; break;
      case 1: cmd = b; chk = b; st = 2; break;
      case 2: len = b; chk += b; idx = 0;
              if (len > sizeof(payload)) st = 0;          // ungültige Länge -> verwerfen
              else st = (len > 0) ? 3 : 4;
              break;
      case 3: payload[idx++] = b; chk += b; if (idx >= len) st = 4; break;
      case 4: st = (b == chk) ? 5 : 0; break;             // Checksumme prüfen
      case 5: if (b == LINK_EOF) handleControllerCommand(cmd, payload, len); st = 0; break;
    }
  }
}
