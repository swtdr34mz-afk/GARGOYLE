# GARGOYLE – Handbuch

GARGOYLE ist ein 8-Bit-Drone-Synth für das M-VAVE FM-1. Die Klänge kommen aus den Soundchips der alten Heimcomputer,
Konsolen und DOS-Spiele: Pulswellen und Rauschen des NES, die 4-Bit-Wellen des Game Boy, Arpeggio-Akkorde und
Ringmodulator des C64, die schmutzigen Zähler des Atari POKEY, die FM der AdLib-Karte, der 1-Bit-PC-Lautsprecher.
Daraus werden Drones. Kein Hall – stattdessen ein Echo, das in 8 Bit speichert.

## Start

Zwei Fackeln entzünden sich, darunter erscheint GARGOYLE in Goldbuchstaben auf einem Felsbrocken. Eine Taste oder ein
Knopf überspringt das. HOLD ist von Anfang an an: Eine Taste startet den Drone und er bleibt stehen.

## Der Bildschirm

- **Links oben:** der Raum. Jede Seite hat ihren eigenen Raum mit eigenem Monster, so weiß man sofort, wo man ist:
  SUMMON ein Ork, GROOVE ein Skelett, SETUP die Schatzkammer, ECHO die Kristallkugel, RITUAL ein Schleim, LORE ein
  Betrachter, SPELL das Wasser mit dem Tentakelmonster. Mit WRATH ab der Hälfte erscheint der Drache. FREEZE macht
  alles eisblau, gehaltenes SAVE zeigt die Schatzkammer.
- **Rechts:** die Kuppel mit der Nummer des Algorithmus (im GROOVE die 16 Schritte), die Seite, HOLD, Oktave, Akku
  (drei Herzen).
- **Die vier Helden:** die Regler K1–K4, jeder mit fester Farbe und Waffe: Rot (Schwert), Orange (Axt), Grün (Bogen),
  Blau (Stab).
- **Unten:** der Algorithmus und was gerade gedreht wird.

## Die Seiten – ein Regler, ein Parameter

| Taste | Seite | K1 | K2 | K3 | K4 |
|---|---|---|---|---|---|
| HOME | SUMMON | CURSE | HORDE | RUNE | ALCHEMY |
| HOME 2× | GROOVE | DENSITY | BREATH | DISTANCE | TEMPO |
| HOME 3× | SETUP | LEVEL | HUMAN | FOLLOW | TUNE |
| FX | ECHO | ECHO | HAUNT | DECAY | DEPTH |
| ENV | RITUAL | TORCH | PORTAL | AWAKEN | SLUMBER |
| EDIT | LORE | CREEP | WANDER | BITS | VOLUME |
| LFO | SPELL | AGE | VIBRATO | SPEED | WIDTH |

- **CURSE** (Mitte neutral): Kreuzmodulation – links Ringmodulation, rechts Sync oder FM (je Algorithmus).
- **HORDE** (Mitte neutral): die Stimmen – links kommen Oktaven darunter dazu, rechts werden sie verstimmt.
- **RUNE:** die Wellenform, die Pulsbreite oder die Tabelle.
- **ALCHEMY:** je Algorithmus etwas anderes – beim Drehen steht unten, was.
- **ECHO** (trocken/nass), **HAUNT** (Rückkopplung; links Ping-Pong), **DECAY** (die Wiederholungen werden dumpfer und
  körniger), **DEPTH** (die Zeit, 5 ms bis 0,7 s).
- **TORCH** (Mitte offen; links Tiefpass, rechts Hochpass), **PORTAL** (Tonhöhe, eine Raste = ein Halbton, ±24),
  **AWAKEN** (Anstieg), **SLUMBER** (Ausklingen).
- **CREEP** (Gleiten), **WANDER** (die Stimmen driften), **BITS** (16 bis 1 Bit), **VOLUME**.
- **AGE** (die Abtastrate, bis hinunter auf 2 kHz), **VIBRATO** (in Stufen wie im Tracker), **SPEED** (Vibrato,
  Arpeggios und Muster, 1–60 Hz – die Bildrate der alten Spiele ist 50/60), **WIDTH**. Die SPELL-Seite gilt für alle
  Algorithmen gleich.

Bipolare Regler rasten in der Mitte zwei Rasten lang ein.

## Die 16 Algorithmen (PRESET)

| Nr. | Name | Woher | ALCHEMY |
|---|---|---|---|
| 1 | TORCHLIGHT | zwei NES-Pulswellen | wie schnell die Pulsbreite wandert |
| 2 | CRYPT CHOIR | vier Game-Boy-Wellen | Lage: Einklang, Oktaven, Quinten |
| 3 | GOBLIN CHORDS | C64-Arpeggio als Akkord | über wie viele Oktaven |
| 4 | DRAGON BREATH | NES-Rauschen, fast ein Ton | Rauschen / Puls |
| 5 | PC SPEAKER | 1 Bit, drei Töne im Wechsel | RealSound |
| 6 | POKEY CURSE | Atari-Polynom-Zähler | der zweite Kanal |
| 7 | SID RITUAL | C64 Ring und Sync | das Verhältnis |
| 8 | ADLIB HALL | OPL-FM | der Multiplikator |
| 9 | BEHOLDER | drei Pulse mit Tracker-Vibrato | wie wild das Vibrato |
| 10 | TREASURE | FM-Glöckchen | das Glockenverhältnis |
| 11 | SKELETON MARCH | Dreieck-Bass und Rausch-Ticks | wie dicht |
| 12 | MAGE TOWER | Welle wandert Puls → Säge → Dreieck → Tabelle | wie schnell von selbst |
| 13 | THRONE OF BONES | Oktav-Arpeggio | wie viele Oktaven |
| 14 | PIT OF DOOM | Frequenzteiler, logisch gemischt | wie tief |
| 15 | WIND OF ABYSS | Rauschen durch einen wandernden Bandpass | wie schnell der Sturm |
| 16 | SPELLBOOK | zufällige Töne einer Tonleiter | wie weit sie springen |

**ALGORITHM** dreht QUEST: die Varianten (Akkord, Tonleiter, Tabelle …), grob. **REC** = FATE: würfelt
Tabellen, Verstimmungen und Muster neu (1 s gehalten: auch RUNE und ALCHEMY). **SEQ** 1 s: zurück auf den Werksklang.

## WRATH (SELECT)

Ein Makro über allem, gemacht für live: CURSE bis zum Anschlag, die Horde verstimmt, das Echo spukt, die Bits brechen
weg, alles wird älter und schneller. 4 Schritte pro Raste; ab der Hälfte erscheint der Drache. Bleibt beim Wechsel.

## Der GROOVE

HOME zweimal, REC schaltet an und aus, ALGORITHM wählt den Style: HEARTBEAT, FOOTSTEPS, DRIPS, CHAINS, WINGS, RITUAL,
BELL, BONES, RUMBLE, TORCH. Die Geräusche klingen wie die Soundeffekte alter Konsolen und DOS-Spiele: Rechteck,
4-Bit-Dreieck, das Rausch-Register, Lautstärke in 16 Stufen, 60 Änderungen pro Sekunde, 6 Bit am Ende. Alle sind auf
den Grundton gestimmt. DENSITY, BREATH (der Drone pumpt mit), DISTANCE (weit weg im Echo), TEMPO; im SETUP LEVEL,
HUMAN, FOLLOW, TUNE. Eine MIDI-Clock übernimmt das Tempo.

## Weitere Tasten

PLAY: HOLD. ARP: FREEZE (das Echo hält). GLO: VOL. SAVE: speichern. Snapshots: SAVE halten + eine der ersten acht weißen
Tasten legt den Klang ab, SEL halten + die Taste holt ihn zurück.

## MIDI

Noten auf allen Kanälen, Pitch Bend ±2, CC 1 RUNE, CC 70–73 CURSE, HORDE, RUNE, ALCHEMY, CC 75 WRATH, Clock / Start /
Stop für den GROOVE.
