# Checkliste — 16-MB-Partitionierung auf deinem Gerät nachziehen

**Für:** Michael · **Stand:** 2026-07-19 · **Dauer:** ca. 10 min · **Nötig:** USB-Kabel ans Gerät

> **Temporäres Dokument** — wird nach erledigtem Umstieg wieder aus dem Repo entfernt.

## Worum es geht

Seit Paket #9 ist die Filesystem-Partition (`spiffs`/LittleFS) von 2 MB auf **9,9 MB**
vergrößert. Dein Gerät läuft per OTA zwar schon auf der neuen Firmware (die App-Partitionen
sind gleich groß geblieben), hat aber noch die **alte 2-MB-Filesystem-Partition**. Deshalb
passt das neue Filesystem-Image (~10,4 MB) nicht mehr rein — genau das ist der Fehler beim
`uploadfs`.

Der Grund, warum das nur per USB geht: Die Partitionstabelle liegt bei `0x8000` und lässt
sich **nicht über OTA** schreiben. Also einmal per Kabel flashen — danach läuft wieder alles
über OTA.

**Deine Kalibrierung bleibt erhalten:** NVS, otadata und beide App-Slots liegen an
identischen Offsets wie vorher; nur die `spiffs`-Partition wächst. Der USB-Upload schreibt
nur Bootloader/Tabelle/Firmware und rührt den NVS-Bereich nicht an (auf Rogers Gerät
nachweislich so passiert). Schritt 1 ist trotzdem als Sicherheitsnetz gedacht.

---

## Schritt 1 — Config sichern (Sicherheitsnetz)

Deine Kalibrierung ist gerätespezifisch, deshalb vorher sichern. Direkt im Browser aufrufen
(funktioniert auch mit der alten Web-Oberfläche, weil es ein Firmware-Endpunkt ist):

```
http://<deine-Geräte-IP>/api/config?download
```

- [ ] JSON-Datei gespeichert (das ist dein Backup — nur nötig, falls doch etwas verloren geht)

## Schritt 2 — USB flashen (BEIDE Befehle)

Gerät per USB anschließen. Alle Befehle im Ordner `tt_esp32controller` ausführen:

```bash
pio run -e esp32s3_usb -t upload
```
```bash
pio run -e esp32s3_usb -t uploadfs
```

- Erster Befehl: Bootloader + **neue Partitionstabelle** + Firmware.
- Zweiter Befehl: das **neue 9,9-MB-Filesystem** (dauert etwas länger, ~10 MB).

> **Beide Schritte sind Pflicht.** Nach der Tabellenänderung ist das alte Filesystem an der
> Stelle ungültig — bis `uploadfs` durch ist, lädt die Web-Oberfläche nicht. Erst der zweite
> Befehl bringt die Webseiten zurück.

- [ ] `upload` ohne Fehler durchgelaufen
- [ ] `uploadfs` ohne Fehler durchgelaufen

**Falls der USB-Port nicht gefunden wird** → Download-Mode: **BOOT** gedrückt halten,
**RESET** kurz drücken und loslassen, dann **BOOT** loslassen. Danach den Befehl erneut
starten. (Details: [`USB CDC.md`](USB%20CDC.md).)

## Schritt 3 — Verifizieren

- [ ] Gerät bootet normal (Status-LED „Herzschlag", **kein** SOS-Blinken)
- [ ] Web-Oberfläche öffnet sich wieder (`http://<deine-Geräte-IP>/`)
- [ ] Einstellungsseite zeigt **Firmware V4.3.1**
- [ ] Kalibrierung (Min/Max Position + Spannung) und Presets sind noch da

**Falls Kalibrierung/Presets wider Erwarten weg sind:** Auf der (jetzt neuen)
Einstellungsseite unter „Konfiguration sichern" → **„Konfig. hochladen"** mit dem Backup aus
Schritt 1 wiederherstellen.

---

## Ergebnis

Dein Gerät hat jetzt die 9,9-MB-Partition. Künftige Filesystem-Updates (`uploadfs`) laufen
wieder ganz normal über OTA — dieser USB-Umstieg ist einmalig.

Bei Problemen: kurze Rückmeldung an Roger (am besten mit dem genauen Wortlaut einer
eventuellen Fehlermeldung).
