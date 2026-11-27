# GARGOYLE – Handbuch

GARGOYLE ist ein Drone-Synth für das M-VAVE FM-1, gebaut zum Live-Spielen. Der Klang kommt von FIKH-S, die Bedienung
ist neu: Die 22 Algorithmen sind 22 Räume eines Dungeons, und vier Makros tun in jedem Raum dasselbe. Dazu kommt ein
GROOVE aus Dungeon-Geräuschen statt Drums.

## Der Start

Der Bildschirm ist schwarz, der Schriftzug GARGOYLE erscheint, darüber öffnen sich zwei glühende Augen – ein Blitz,
und für einen Augenblick sieht man den Gargoyle. Eine Taste oder ein Knopf überspringt das Intro.

HOLD ist von Anfang an an: Eine Taste startet den Drone, und er bleibt stehen. Weitere Tasten ändern nur noch den
Grundton.

## Der Bildschirm

- **Oben:** die Nummer des Raums im roten Siegel, sein Name, darunter der Algorithmus. Rechts ein Herz: Es schlägt mit
  dem GROOVE.
- **Mitte:** die Kammer mit zwei Fackeln und dem Gargoyle. Die Fackeln und seine Augen leuchten mit dem Klang, der
  Nebel unten ist das echte Signal. Mit FREEZE wird alles eisblau. Links und rechts die Gänge zu den Nachbarräumen.
- **Vier Zeilen:** die vier Regler K1–K4. Jeder hat eine feste Farbe, auf jeder Seite gleich: **Rot** K1, **Orange** K2,
  **Grün** K3, **Blau** K4. Der helle Strich im Balken ist der Sweetspot.
- **Unten:** die Anzeige (was gerade gedreht wird), dann HOLD, FREEZE, die Oktave und der Akku als drei Herzen.

## Die Seiten

| Taste | Seite | K1 | K2 | K3 | K4 |
|---|---|---|---|---|---|
| HOME | PLAY | TONE | MOVE | GRIT | SPACE |
| HOME 2× | GROOVE | DENSITY | BREATH | DISTANCE | TEMPO |
| HOME 3× | GROOVE SETUP | LEVEL | HUMAN | FOLLOW | TUNE |
| EDIT | DEEP | COLOR | GLIDE | WIDTH | LEVEL |
| FX | FX | MODE | X | ECHO | AGE |

### PLAY: die vier Makros

- **TONE** – dunkel bis hell: das Filter schließt sich (links) oder öffnet sich ganz (rechts), der Hall wird dunkler
  oder heller.
- **MOVE** – still bis brodelnd: Drift der Oszillatoren, Spreizung und das Schweben im Hall.
- **GRIT** – sauber bis kaputt: Kreuzmodulation, Sättigung und Rückkopplung; ganz rechts kommt die
  Frequenzverschiebung dazu.
- **SPACE** – trocken bis endlos: Hall, Delay und Diffusion; ganz rechts hört der Hall nicht mehr auf.

Die **Mitte** (0) ist der Werksklang des Raums, der Sweetspot. Dreht man hindurch, bleibt der Regler dort zwei Rasten
lang stehen. Eine Raste bewegt einen Makro um 2,5 Schritte, schnell gedreht um 7,5 – jede Raste hört man. Unten steht,
in welche Richtung es geht (z. B. „TONE -10 DARK“).

### DEEP (EDIT)

COLOR (die Klangfarbe, MORPH bei FIKH-S), GLIDE (Gleiten zwischen Noten), WIDTH (Stereobreite), LEVEL (Lautstärke des
Raums).

### FX

- **MODE**: der Hall – DREAM, DARK, RISE, LIGHT, RAIN (eine Raste, ein Modus).
- **X**: heißt je nach Modus anders – VIBRATO (DREAM), OCT DOWN (DARK), SWELL (RISE), SHIMMER (LIGHT), DIFFUSE (RAIN).
- **ECHO**: die Länge des Delays (Mitte: wie ab Werk).
- **AGE**: macht den Hall älter, dunkler und körniger.

FX gilt für alle Räume gleich.

## Die Räume

SELECT (oder PRESET) wechselt den Raum. Kurz erscheint der Dungeon-Plan mit allen 22 Räumen, der aktuelle brennt.
Jeder Raum merkt sich seine Makros, DEEP, SEED und SCAN.

| Nr. | Raum | Algorithmus | Nr. | Raum | Algorithmus |
|---|---|---|---|---|---|
| 1 | CRYPT | METAL PAIR | 12 | DUNGEON | NOTCH STACK |
| 2 | FORGE | SHAPER PAIR | 13 | ROOST | SINE SWARM |
| 3 | CRYSTAL CAVE | SINE CLUSTER | 14 | ORACLE | RADIO |
| 4 | WATCHTOWER | WAVE QUAD | 15 | STORM GATE | WEATHER |
| 5 | CHAPEL | DIATONIC | 16 | MASONRY | ARCHITECT |
| 6 | THRONE HALL | CHORD SCAN | 17 | ABYSS | FOLD BASS |
| 7 | BELFRY | PARTIALS | 18 | GREAT HALL | SUPERSAW |
| 8 | THE WELL | COMB SWARM | 19 | RUNE VAULT | WAVE PM |
| 9 | MIST HALL | GRAIN CLOUD | 20 | LABYRINTH | WAVE FOLD |
| 10 | THE VOID | ZERO CARRIER | 21 | TREASURY | METAL CLUSTER |
| 11 | ECHO SHAFT | DISPERSOR | 22 | GHOST HALL | DIFFUSE CLUSTER |

**ALGORITHM** fährt SCAN grob durch zufällige Klangfarben. **REC** würfelt SEED und SCAN neu, 1 s gehalten auch COLOR
und ECHO. **SEQ** 1 s gehalten setzt den Raum auf den Werksklang zurück.

## Der GROOVE

HOME zweimal drücken: die GROOVE-Seite. **REC** schaltet den GROOVE an und aus, **ALGORITHM** wählt den Style. Statt
Drums sind es Geräusche aus dem Dungeon, alle auf den Grundton gestimmt:

| Style | Was man hört |
|---|---|
| HEARTBEAT | ein tiefes Lub-Dub; der Drone pumpt dabei leicht mit |
| FOOTSTEPS | schwere Schritte auf Stein, links und rechts; sie kommen näher und gehen wieder |
| DRIPS | Wassertropfen in der Gruft, unregelmäßig |
| CHAINS | Kettenglieder, metallisches Klirren |
| WINGS | Flügelschläge; sie kreisen im Stereobild |
| RITUAL | eine langsame Rahmentrommel |
| BELL | eine tiefe Glocke in der Ferne |
| BONES | trockenes Klappern von Holz und Knochen |
| RUMBLE | das Gewölbe bebt |
| TORCH | die Fackel knistert, Funken springen |

**GROOVE-Seite:** DENSITY (von vereinzelt bis dicht), BREATH (wie stark der Drone im Takt atmet), DISTANCE (nah und
trocken bis weit weg im Hall), TEMPO (30–140 BPM).

**SETUP (HOME 3×):** LEVEL (Lautstärke), HUMAN (Zufall: etwas später, ungleich laut – nicht maschinell), FOLLOW (dem
Grundton folgen: an/aus), TUNE (±12 Halbtöne).

Oben im GROOVE steht das EKG: Jeder Schlag ist ein Ausschlag, darunter laufen die 16 Schritte. HOME blitzt mit jedem
Schlag. Der GROOVE gilt für alle Räume: Ein Raumwechsel ändert den Beat nicht.

**MIDI-Clock:** Kommt eine Clock (z. B. vom Digitone), läuft der GROOVE in deren Tempo („MIDI SYNC“). Start beginnt
von vorn, Stop hält ihn an.

## Weitere Tasten

- **PLAY**: HOLD an/aus.
- **ARP**: FREEZE – Delay und Hall halten, was sie haben; Neues klingt trocken darüber.
- **GLO**: VOL – die Pegel als rote Säulen in den Gängen; rot umrandet: übersteuert.
- **SAVE**: speichern (sonst automatisch, wenn es still ist).
- **Snapshots**: SAVE halten + eine der ersten acht weißen Tasten legt den ganzen Klang dort ab (Raum, Makros, DEEP,
  FX, Oktave). SEL halten + die Taste holt ihn zurück – im selben Raum gleiten die Makros hinüber.

## MIDI

Noten auf allen Kanälen, Pitch Bend ±2, CC 1 = COLOR, CC 70 TONE, CC 71 MOVE, CC 72 GRIT, CC 73 SPACE, Clock / Start /
Stop für den GROOVE.

## Akku

Drei Herzen unten rechts, stufenlos gefüllt. Unter 8 % blinken sie. An USB steht ein Blitz davor.

## ECO

Reicht die Rechenleistung nicht, rechnet der Klang mit weniger Oszillatoren und unten steht ECO.
