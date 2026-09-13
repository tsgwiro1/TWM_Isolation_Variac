"""Prüft nach einem OTA-Upload, ob das Gerät dem Stand im Repo entspricht.

Läuft auf dem Build-PC, nicht auf dem Gerät — und zwar notwendigerweise: Das Gerät
kennt nur, was auf ihm liegt, und kann deshalb nur seine innere Stimmigkeit prüfen
(Firmware gegen Filesystem). Ob dieser Stand dem entspricht, was im Repo steht,
weiss allein die Build-Maschine, weil ihr beide Seiten vorliegen.

Als Vergleichswert dient `data/version.json`. Die Datei hat der Pre-Hook
(scripts/version.py) in genau diesem Build geschrieben — damit gibt es nur eine
Stelle, an der der Hash berechnet wird, und die beiden Skripte können nicht
auseinanderlaufen.

**Die Prüfung ist eine Zugabe, kein Tor.** Sie bricht den Build unter keinen
Umständen ab: Bei einem USB-Flash wird sie übersprungen, bei einem nicht
erreichbaren Gerät gibt sie einen Hinweis aus, und jeder unerwartete Fehler in ihr
selbst wird abgefangen. Ein geglückter Upload darf nicht daran scheitern, dass eine
Kontrolle danach nicht durchkam.
"""

import json
import pathlib
import time
import urllib.request

Import("env")  # noqa: F821

# Nach dem Upload startet das Gerät neu und ist rund zehn Sekunden nicht
# erreichbar — so lange muss die Prüfung geduldig sein. Länger aber nicht: Ist das
# Gerät schlicht aus, soll der Build nicht minutenlang daran hängen.
ATTEMPTS = 4
DELAY_SECONDS = 3
TIMEOUT_SECONDS = 3
# Der Upload gilt als fertig, bevor das Gerät neu gestartet hat. Ohne diese Pause
# könnte die erste Abfrage noch die alte Firmware treffen und einen Versatz melden,
# den es gar nicht gibt.
SETTLE_SECONDS = 5


def fetch_status(host):
    """Holt /api/status, mit Geduld für den Neustart nach dem Upload."""
    last = None
    time.sleep(SETTLE_SECONDS)
    for attempt in range(ATTEMPTS):
        if attempt:
            if attempt == 1:
                print(f"  Geraetepruefung: warte auf {host} ...")
            time.sleep(DELAY_SECONDS)
        try:
            with urllib.request.urlopen(
                f"http://{host}/api/status", timeout=TIMEOUT_SECONDS
            ) as response:
                return json.loads(response.read().decode("utf-8")), None
        except Exception as error:   # noqa: BLE001 — jede Ursache ist hier gleich gutartig
            last = error
    return None, last


def check(source, target, env):   # noqa: ARG001 — von SCons so aufgerufen
    try:
        if env.GetProjectOption("upload_protocol", "") != "espota":
            print("  Geraetepruefung: uebersprungen, kein OTA-Upload.")
            return

        host = env.GetProjectOption("upload_port", "")
        if not host:
            print("  Geraetepruefung: uebersprungen, keine Adresse konfiguriert.")
            return

        stamp_path = pathlib.Path(env["PROJECT_DATA_DIR"]) / "version.json"
        if not stamp_path.is_file():
            print("  Geraetepruefung: uebersprungen, data/version.json fehlt.")
            return
        local = json.loads(stamp_path.read_text(encoding="utf-8"))

        status, error = fetch_status(host)
        if status is None:
            print(f"  Geraetepruefung: {host} nicht erreichbar ({error}) - uebersprungen.")
            return

        device_fs = status.get("fs_content", "")
        device_fs_version = status.get("fs_version", "")
        device_fw = status.get("fw_version", "")

        print("")
        if not device_fs_version:
            print("  Geraetepruefung: Das Geraet meldet keinen Filesystem-Stand.")
            print("  Vermutlich liegt dort noch ein Image von vor der Stempelung.")
            print("  'uploadfs' ausfuehren, dann stimmen beide Seiten wieder ueberein.")
        elif device_fs != local.get("content", ""):
            print("  Geraetepruefung: Das Filesystem auf dem Geraet ist nicht der Stand aus data/.")
            print(f"  Geraet: {device_fs_version} ({device_fs})   data/: "
                  f"{local.get('version')} ({local.get('content')})")
            print("  'pio run -t uploadfs' ausfuehren.")
        elif local.get("version", "") not in device_fw:
            print("  Geraetepruefung: Die Firmware auf dem Geraet ist nicht der gebaute Stand.")
            print(f"  Geraet: {device_fw}   erwartet: {local.get('version')}")
            print("  'pio run -t upload' ausfuehren.")
        else:
            print(f"  Geraetepruefung: {host} entspricht dem Stand im Repo - "
                  f"{local.get('version')} ({local.get('content')}).")
        print("")

    except Exception as error:   # noqa: BLE001
        # Niemals den Build scheitern lassen, nur weil die Kontrolle danach stolpert.
        print(f"  Geraetepruefung uebersprungen: {error}")


env.AddPostAction("upload", check)      # noqa: F821
env.AddPostAction("uploadfs", check)    # noqa: F821
