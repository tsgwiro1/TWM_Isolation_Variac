# Status-LED des TWM Isolation Variac Controllers

Die Status-LED auf dem Controller-Board dient als Diagnosewerkzeug. Sie zeigt jederzeit den
aktuellen Betriebs- oder Fehlerzustand des Systems an. Die Muster werden vom
`statusLedTask` in [`../src/system.cpp`](../src/system.cpp) erzeugt (LED ist LOW-aktiv);
die Zustände definiert `SystemState` in [`../src/state.h`](../src/state.h).

## 1. Normalbetrieb (Herzschlag)

- **Blinkmuster:** Kurzes Aufblitzen (0,1 s an), gefolgt von einer langen Pause (2,9 s aus).
- **Bedeutung:** Das System läuft fehlerfrei im regulären Betrieb
  (`STATE_NORMAL_OPERATION`). Alle Tasks arbeiten wie vorgesehen.

## 2. Startphase

- **Blinkmuster:** Gleichmäßiges, ruhiges Blinken (0,5 s an, 0,5 s aus).
- **Bedeutung:** Das Gerät initialisiert gerade seine Hardware (`STATE_STARTING`).
  Der Zustand geht in den „Herzschlag" über, sobald der Port-Expander gefunden und die
  Hardware bereit ist — üblicherweise nach wenigen Sekunden.
- **Hinweis:** Seit V4.4.0 wird das WLAN erst *nach* der Hardware aufgebaut (GitHub-#13).
  Dieses Muster steht deshalb für die Startphase des Geräts, nicht mehr für den
  WLAN-Verbindungsaufbau — der läuft im Hintergrund, während die LED bereits den
  Herzschlag zeigt.

## 3. Konfigurationsmodus (WiFiManager AP)

- **Blinkmuster:** Auffälliger, schneller Doppel-Blink (2× 0,1 s), gefolgt von 1 s Pause.
- **Bedeutung:** Der Controller konnte das heimische WLAN nicht finden oder das Passwort
  ist falsch. Das Gerät hat deshalb ein eigenes, temporäres WLAN-Netzwerk namens
  **„TWM_IsolationVariac"** geöffnet (`STATE_WIFIMANAGER_AP`).
- **Aktion:** Mit Smartphone oder PC mit diesem Netzwerk verbinden und im Portal die
  korrekten WLAN-Zugangsdaten eingeben.
- **Nach 10 Minuten ohne Eingabe:** Das Portal schließt, das Funkmodul wird abgeschaltet
  und die LED kehrt zum Herzschlag zurück. Der Variac ist dann weiterhin voll bedienbar,
  aber **ohne Weboberfläche und ohne API**. Für einen erneuten Verbindungsversuch das
  Gerät neu starten.

## 4. Firmware-Update läuft (OTA)

- **Blinkmuster:** Sehr schnelles, nervöses Blinken (0,15 s an, 0,15 s aus).
- **Bedeutung:** Der Controller empfängt gerade eine neue Firmware oder ein neues
  Dateisystem drahtlos über das Netzwerk (`STATE_OTA_UPDATE`).
- **Aktion:** **KRITISCH:** Das Gerät darf in diesem Moment auf keinen Fall vom Strom
  getrennt oder neugestartet werden, da sonst die Firmware beschädigt wird!

## 5. Systemfehler (SOS)

- **Blinkmuster:** SOS-Morsesignal: 3× kurz, 3× lang, 3× kurz, gefolgt von 1 s Pause.
- **Bedeutung:** Ein schwerwiegender Hardware- oder Initialisierungsfehler verhindert den
  Start bzw. Betrieb des Systems (`STATE_ERROR`).
- **Mögliche Ursachen:**
  - Der MCP23017 Port-Expander antwortet nicht auf dem I2C-Bus.
  - Das LittleFS-Dateisystem konnte nicht gemountet werden (Speicher defekt oder nicht
    formatiert).
  - Das TFT-Display oder kritische Speicherbereiche (Mutex) konnten nicht geladen werden.
  - Ein OTA-Update ist fehlgeschlagen (Details dazu im Log).
