# Partitionierung auf 16 MB umstellen (#9)

**Status: vorbereitet, noch nicht aktiv.** Die neue Tabelle liegt als
[`partitions_16mb.csv`](../partitions_16mb.csv) im Projekt; die aktive
[`partitions.csv`](../partitions.csv) bleibt bis zum Umstieg unverändert.

> **Temporäres Dokument** — nach erfolgreicher Umsetzung von #9 am Gerät wird diese
> Datei entfernt (das dauerhaft Wissenswerte — Layout + Label-Begründung — steht als
> Kommentar in der Partitionstabelle selbst und bleibt dort erhalten).

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

## Umstiegs-Prozedur Schritt für Schritt (Gerät per USB erreichbar)

**Wichtig vorab:** Die `platformio.ini` erst **unmittelbar vor dem USB-Flash**
umschalten (Schritt U3). Solange das Gerät die alte Tabelle trägt, muss auch
`platformio.ini` auf der alten bleiben — sonst bauen OTA-/`uploadfs`-Builds
Images für ein Layout, das auf dem Gerät nicht existiert.

### U1 — Konfig sichern (Sicherheitsnetz; NVS überlebt den Umstieg eigentlich)

1. Im Browser: `http://twm_variac.local/api/config?download`
2. Datei lokal ablegen.

- [ ] `config.json` heruntergeladen, Kalibrierwerte darin plausibel

### U2 — Gerät per USB verbinden

1. USB-Kabel an den ESP32-S3 (Gerät offen/zugänglich).
2. Prüfen, dass der Port erkannt wird: `pio device list` (Mac: `/dev/cu.*`).

- [ ] Serieller Port sichtbar

### U3 — Partitionstabelle umschalten

In `tt_esp32controller/platformio.ini` die aktive Zeile wechseln:

```ini
;board_build.partitions = partitions.csv
board_build.partitions = partitions_16mb.csv
```

- [ ] Nur die Partitions-Zeile geändert, sonst nichts

### U4 — Firmware per USB flashen (schreibt Bootloader + NEUE Tabelle + App)

```bash
cd tt_esp32controller
~/.platformio/penv/bin/pio run -e esp32s3_usb -t upload
```

- [ ] Upload ohne Fehler, Gerät bootet (Display zeigt Normalanzeige/Homing)

### U5 — Filesystem neu bespielen (FS-Partition ist durch die Größenänderung leer)

```bash
~/.platformio/penv/bin/pio run -e esp32s3_usb -t uploadfs
```

- [ ] `uploadfs` ohne Fehler (schreibt jetzt das ~9,9-MB-Image)

### U6 — Verifizieren

1. Settings-Seite öffnen → Kalibrierung/Presets prüfen.
2. `http://twm_variac.local/api/files` → Dateiliste prüfen.
3. Hauptseite + Doku-Seite (`doc_api.html`) laden.
4. Einmal OTA testen: `pio run -e esp32s3_ota -t upload`.

- [ ] **Kalibrierung/Presets unverändert da** (Beweis: NVS hat überlebt)
- [ ] Webseiten vollständig (inkl. RapiDoc)
- [ ] OTA funktioniert weiterhin

### U7 — Abschließen

1. `platformio.ini`-Änderung committen (ab jetzt bauen alle Builds gegen die neue Tabelle).
2. Aufräumen im selben Commit: Inhalt von `partitions_16mb.csv` nach `partitions.csv`
   übernehmen (bzw. Datei umbenennen), Umschalt-Kommentar in `platformio.ini` entfernen,
   dieses Dokument löschen, BACKLOG #9 abhaken.

- [ ] Commit erstellt, #9 im BACKLOG abgehakt

## Rollback (falls etwas schiefgeht)

`board_build.partitions = partitions.csv` zurückstellen und U4 + U5 erneut per USB
ausführen. NVS (Konfiguration/Kalibrierung) bleibt auch dabei erhalten.
