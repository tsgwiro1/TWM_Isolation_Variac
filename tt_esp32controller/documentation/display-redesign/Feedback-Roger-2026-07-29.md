# Rückmeldung zum neuen Normalbetrieb-Screen

**Von:** Roger · **Datum:** 29.07.2026 · **Stand:** Branch `feature/redesign-display`,
Commit `982ce97` (Zwischenstand), am Gerät getestet mit V4.7.0-Firmware.

**Das macht was her!** Der Screen sieht **deutlich professioneller aus als das
ursprüngliche Layout — sehr cool.** Größe und Farbgebung der Kacheln (Funktions-Chips und
Presets) treffen es genau: kompakt, auf einen Blick lesbar, und die Farbcodierung sitzt.
Auch die Blinkeffekte sind eine gute Idee.

Die folgenden Punkte sind **Vorschläge für den Feinschliff** — Ideen aus dem Betrachten am
Gerät, kein Nachbesserungskatalog. Die Richtung stimmt.

## Vorschläge zum Spannungsblock

| # | Idee | Gedanke dahinter |
| :-- | :--- | :--- |
| 1 | **Ist und Ziel unterschiedlich groß** | Aktuell tragen Farbe und Fettung die Unterscheidung allein. Ein Größenunterschied macht auf einen Blick klar, welcher Wert der gemessene ist. |
| 2 | **Labels „Ist" / „Ziel" könnten entfallen** | Ergibt sich aus Position und Größe von selbst — und der gewonnene Platz fließt in einen größeren Ist-Font. |
| 3 | **Ist-Spannung größer und evtl. mittig** | Sie ist der wichtigste Wert auf dem Screen; sie darf den Platz haben. |

## Vorschläge zu den Warnungen

| # | Idee | Gedanke dahinter |
| :-- | :--- | :--- |
| 4 | **Warnung > 50 V könnte entfallen** | Ein Warnsymbol, das im normalen Arbeitsbereich fast immer steht, verliert seine Signalwirkung. |
| 5 | **Achtung-Dreieck (Strombegrenzung aus) größer** | Das ist die Warnung, auf die es ankommt — sie darf mehr Gewicht bekommen. Die Farben sind schon richtig gewählt. |
| 6 | **Warndreieck nur einblenden, wenn aktiv** | Statt dim-grauem Platzhalter: leerer Platz im Normalfall, das Symbol erscheint als echtes Ereignis. |
| 7 | **Blinken mit mehr Kontrast** | Der Effekt gefällt — mit stärkerem Unterschied zwischen den Phasen zieht er den Blick zuverlässiger an. |

Die Punkte 2 und 4–6 geben zusammen Platz frei. Fällt das 50-V-Dreieck weg und erscheint
das Achtung-Dreieck nur bei Bedarf, ist die linke Warnspalte im Normalfall leer — dann
stellt sich die schöne Frage, ob sie noch eine eigene Spalte braucht oder das Dreieck über
der Zahl sitzen kann. Das spielt gut mit Punkt 3 zusammen.

## Vorschläge zur Gliederung

| # | Idee | Gedanke dahinter |
| :-- | :--- | :--- |
| 8 | **Feine Trennlinie zwischen Werten und Kacheln** | Trennt Messwerte von Bedienzustand. Alternative mit demselben Effekt: das Wort „Presets" weglassen. |
| 9 | **Oberer Rand wirkt noch offen** | *„Irgendwie fehlt mir was nach oben."* Denkbar: eine Titelzeile, eine feine Trennlinie unter der Kopfzeile — oder eben Punkt 3 (Ist groß und mittig), was den Raum von selbst füllen würde. |

Punkt 9 ist bewusst offen formuliert: am Gerät gegeneinander probieren, dann entscheiden.

## Beobachtung: Flackern der großen Zahlen

Die großen Zahlen flackern beim Aktualisieren sichtbar. Vorschlag: **Double-Buffering
prüfen** — die Zahlenfläche in einen `TFT_eSprite` zeichnen und in einem Rutsch pushen,
statt sie zu löschen und neu zu beschriften. Der Dirty-Check pro Zone verhindert nur das
Neuzeichnen unveränderter Zonen; innerhalb einer Zone bleibt die Abfolge „löschen →
zeichnen" sichtbar. Der Sprite-Ansatz steht im README ohnehin schon als Umsetzungsthema
(Framebuffer in PSRAM) — das wäre die Gelegenheit.
