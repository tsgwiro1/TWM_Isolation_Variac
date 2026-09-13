"""Versionsstempel für Firmware und Filesystem-Image.

Läuft als pre-Skript vor jedem Build — auch vor `buildfs` und `uploadfs`, weil
PlatformIO die pre-Skripte ausführt, bevor der Plattform-Builder das Ziel
überhaupt anlegt. Drei Aufgaben:

1. `version.txt` lesen — die einzige Stelle, an der die Versionsnummer steht.
2. `data/version.json` schreiben, damit das Gerät seinen eigenen Stand kennt und
   ihn über /api/status melden kann.
3. Die Version als `FW_VERSION` an den Compiler reichen, damit `state.h` sie
   nicht ein zweites Mal führt.

Nebenbei die eigentliche Absicherung: Hat sich `data/` geändert, ohne dass die
Version erhöht wurde, sagt der Build das. Wurde die Version erhöht, stempelt er
den neuen Stand still nach.
"""

import hashlib
import json
import pathlib
import re

Import("env")  # noqa: F821  (von SCons bereitgestellt)

STAMP_FILE = "version.txt"
IMAGE_FILE = "version.json"   # liegt in data/ und wird nie mitgehasht


def read_fields(path):
    """Liest `schluessel = wert`-Zeilen; Kommentare und Leerzeilen fallen weg."""
    fields = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        key, _, value = line.partition("=")
        fields[key.strip()] = value.strip()
    return fields


def write_fields(path, fields):
    """Schreibt die Werte zurück und lässt Kommentare und Reihenfolge stehen."""
    out = []
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.match(r"^(\s*)([A-Za-z_]+)(\s*=\s*)(.*)$", line)
        if match and match.group(2) in fields:
            out.append(f"{match.group(1)}{match.group(2)}{match.group(3)}{fields[match.group(2)]}")
        else:
            out.append(line)
    path.write_text("\n".join(out) + "\n", encoding="utf-8")


def hash_data(data_dir):
    """Inhalts-Hash über data/ — Pfade und Bytes, stabil sortiert.

    `version.json` bleibt aussen vor: Es wird von diesem Skript selbst erzeugt,
    und ein Hash, der sich selbst enthält, ändert sich bei jedem Lauf.
    """
    digest = hashlib.sha256()
    for path in sorted(p for p in data_dir.rglob("*") if p.is_file()):
        relative = path.relative_to(data_dir).as_posix()
        if relative == IMAGE_FILE:
            continue
        digest.update(relative.encode("utf-8"))
        digest.update(path.read_bytes())
    return digest.hexdigest()[:12]


project = pathlib.Path(env["PROJECT_DIR"])          # noqa: F821
data_dir = pathlib.Path(env["PROJECT_DATA_DIR"])    # noqa: F821
stamp_path = project / STAMP_FILE

fields = read_fields(stamp_path)
version = fields.get("version", "")
if not version:
    raise SystemExit(f"{STAMP_FILE}: Feld 'version' fehlt oder ist leer.")

current_hash = hash_data(data_dir) if data_dir.is_dir() else ""

if fields.get("stamped_version") != version:
    # Version wurde erhöht: den jetzigen Stand von data/ als neuen Bezug festhalten.
    write_fields(stamp_path, {"stamped_version": version, "stamped_hash": current_hash})
    print(f"version: {version} gesetzt, Stand von data/ gestempelt ({current_hash}).")
elif current_hash != fields.get("stamped_hash", ""):
    print("")
    print(f"  data/ hat sich geaendert, version.txt steht weiterhin auf {version}.")
    print(f"  gestempelt: {fields.get('stamped_hash', '-')}   aktuell: {current_hash}")
    print("  Version in version.txt erhoehen, oder die Aenderung bewusst ohne Bump hochladen.")
    print("")

# Stand ins Filesystem-Image legen, damit das Geraet ihn melden kann.
if data_dir.is_dir():
    (data_dir / IMAGE_FILE).write_text(
        json.dumps({"version": version, "content": current_hash}), encoding="utf-8"
    )

# Und an den Compiler, damit state.h die Nummer nicht ein zweites Mal fuehrt.
env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(version))])  # noqa: F821
