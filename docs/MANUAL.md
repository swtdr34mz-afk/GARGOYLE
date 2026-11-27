# GARGOYLE – Manual

GARGOYLE is a drone synth for the M-VAVE FM-1, made for playing live. The sound comes from FIKH-S, the controls are
new: the 22 algorithms are 22 rooms of a dungeon, and four macros do the same in every room. Plus a GROOVE of dungeon
noises instead of drums.

## Start

Two torches light up, under them GARGOYLE appears in gold letters on a slab of rock. Any key or button skips the intro. HOLD is on from the start: a key starts the drone and it stays;
further keys only change the root.

## The screen

- **Top left:** the view down the corridor, like the dungeon games of the 90s; Every page has its own corridor and its own
  monster lurking in the dark, so you always know where you are: PLAY red bricks and a dragon, GROOVE the crypt and
  rats, SETUP sandstone and a spider, DEEP moss and an eye, FX blue stone and a ghost. Torches and eyes glow with the
  sound, FREEZE turns it ice blue.
- **Right:** the dome with the room's number (on the GROOVE pages the 16 steps), the page, HOLD, octave, battery.
- **The four heroes:** knobs K1–K4, each with a fixed colour and weapon on every page: **red** K1, **orange** K2,
  **green** K3, **blue** K4. The light mark in a bar is the sweetspot.
- **Bottom:** the message box: the room, and what you are turning.

## Pages

| Button | Page | K1 | K2 | K3 | K4 |
|---|---|---|---|---|---|
| HOME | PLAY | TONE | MOVE | GRIT | SPACE |
| HOME 2× | GROOVE | DENSITY | BREATH | DISTANCE | TEMPO |
| HOME 3× | GROOVE SETUP | LEVEL | HUMAN | FOLLOW | TUNE |
| EDIT | DEEP | COLOR | GLIDE | WIDTH | LEVEL |
| FX | FX | MODE | X | ECHO | AGE |

**The macros:** TONE (dark – bright: the filter and the reverb's tone), MOVE (still – churning: drift, spread, the
reverb's movement), GRIT (clean – broken: cross modulation, saturation, feedback, frequency shift at the top), SPACE
(dry – endless: reverb, delay, diffusion; fully right the reverb never ends). The middle (0) is the room's factory
sound, the sweetspot: turning through it, the knob stops there for two detents. One detent moves a macro by 2.5 (7.5
when turned fast).

**DEEP:** COLOR (MORPH in FIKH-S), GLIDE, WIDTH, LEVEL of the room. **FX** (the same for all rooms): MODE (DREAM, DARK,
RISE, LIGHT, RAIN), X (VIBRATO, OCT DOWN, SWELL, SHIMMER or DIFFUSE, by mode), ECHO (the delay time), AGE (older,
darker, grainier reverb).

## Rooms

PRESET changes the room (the view goes dark for a step). SELECT is WRATH: one macro over everything for live play – the monster at the end of the corridor wakes up; 4 a detent, it stays when you change rooms. Each room keeps its macros, DEEP, SEED and
SCAN. 1 CRYPT, 2 FORGE, 3 CRYSTAL CAVE, 4 WATCHTOWER, 5 CHAPEL, 6 THRONE HALL, 7 BELFRY, 8 THE WELL, 9 MIST HALL,
10 THE VOID, 11 ECHO SHAFT, 12 DUNGEON, 13 ROOST, 14 ORACLE, 15 STORM GATE, 16 MASONRY, 17 ABYSS, 18 GREAT HALL,
19 RUNE VAULT, 20 LABYRINTH, 21 TREASURY, 22 GHOST HALL (the algorithms of FIKH-S in their order).

ALGORITHM scans timbres (SCAN, coarse). REC dices SEED and SCAN (held 1 s: also COLOR and ECHO). SEQ held 1 s: the room
back to its factory sound.

## GROOVE

HOME twice. REC switches it on and off, ALGORITHM picks the style: HEARTBEAT, FOOTSTEPS, DRIPS, CHAINS, WINGS, RITUAL,
BELL, BONES, RUMBLE, TORCH – all synthesized and tuned to the root. DENSITY (sparse – dense), BREATH (how much the drone
breathes with it), DISTANCE (near and dry – far away in the reverb), TEMPO (30–140 BPM). SETUP (HOME 3×): LEVEL, HUMAN
(late and uneven on purpose), FOLLOW (follow the root), TUNE (±12 semitones). With a MIDI clock it follows the clock;
start restarts it, stop holds it.

## More

PLAY: HOLD. ARP: FREEZE. GLO: VOL (output levels). SAVE: save (otherwise automatic when quiet). Snapshots: hold SAVE +
one of the first eight white keys to store the whole sound, hold SEL + the key to recall it. MIDI: notes on any
channel, pitch bend ±2, CC 1 COLOR, CC 70–73 TONE, MOVE, GRIT, SPACE, CC 75 WRATH, clock / start / stop. The battery: three hearts
(blinking under 8 %, a bolt on USB). ECO: fewer oscillators when the CPU is short.
