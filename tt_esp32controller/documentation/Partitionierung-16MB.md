# Partitionierung auf 16 MB umstellen (#9)

**Status: vorbereitet, noch nicht aktiv.** Die neue Tabelle liegt als
[`partitions_16mb.csv`](../partitions_16mb.csv) im Projekt; die aktive
[`partitions.csv`](../partitions.csv) bleibt bis zum Umstieg unverändert.

## Warum

Der ESP32-S3 (N16R2) hat 16 MB Flash, die aktuelle Tabelle nutzt nur 8 MB:

| Partition | heute | neu (16 MB) | Offset (unverändert!) |
|-----------|------:|------------:|-----------------------|
| nvs (Konfig/Kalibrierung) | 20 KB | 20 KB | 0x9000 |
| otadata | 8 KB | 8 KB | 0xe000 |
| app0 (OTA-Slot 0) | 3 MB | 3 MB | 0x10000 |
| app1 (OTA-Slot 1) | 3 MB | 3 MB | 0x310000 |
| Filesystem (LittleFS) | **2 MB** | **~9,9 MB** | 0x610000 |

Motivation: Das LittleFS wird mit RapiDoc (843 KB) + Web-Redesign (Paket G) knapp.
Die App-Slots bleiben bei 3 MB (Firmware nutzt ~43 %) — **alle Offsets außer der
FS-Größe sind identisch**, dadurch minimales Umstiegsrisiko:

- NVS bleibt an Ort und Stelle → **Konfiguration/Kalibrierung überlebt**.
- Die installierte Firmware bleibt gültig (App-Slots unverändert).
- Nur das Filesystem ist nach dem Umstieg leer (Größe geändert → Neuformat).

## Warum nicht per OTA?

Die Partitionstabelle liegt an fester Adresse `0x8000` und ist selbst keine
Partition — OTA (App/Filesystem) schreibt sie nie. Ein Software-Rewrite aus der
laufenden App wäre technisch möglich, aber ein Stromausfall im Schreibfenster
macht das Gerät unbootbar (Rettung nur per USB = Gerät öffnen). Deshalb:
**Umstieg nur per USB am offenen/zugänglichen Gerät.**

## Umstiegs-Prozedur (wenn das Gerät per USB erreichbar ist)

1. **Konfig sichern** (Sicherheitsnetz, NVS überlebt eigentlich):
   `http://twm_variac.local/api/config?download`
2. In `platformio.ini` umschalten:
   ```ini
   board_build.partitions = partitions_16mb.csv
   ```
3. **Firmware per USB flashen** (schreibt Bootloader + neue Tabelle + App):
   ```bash
   pio run -e esp32s3_usb -t upload
   ```
4. **Filesystem neu bespielen** (Partition ist neu/leer; USB oder danach OTA):
   ```bash
   pio run -e esp32s3_usb -t uploadfs
   ```
5. **Verifizieren:** Boot ok · Settings-Seite zeigt bekannte Kalibrierung/Presets
   (NVS-Beweis) · Webseiten laden · `/api/files` zeigt die Dateien.
6. Commit der `platformio.ini`-Änderung — ab dann bauen alle (auch OTA-)Builds
   gegen die neue Tabelle.

**Wichtig:** Schritt 2 erst unmittelbar vor dem USB-Flash machen. Solange das
Gerät die alte Tabelle trägt, muss auch `platformio.ini` auf der alten bleiben —
sonst bauen OTA-/`uploadfs`-Builds Images für ein Layout, das auf dem Gerät
nicht existiert.

## Rollback

`board_build.partitions = partitions.csv` zurückstellen und erneut per USB
flashen (+ `uploadfs`). NVS bleibt auch dabei erhalten.
