# GARGOYLE

An 8-bit drone synth for the M-VAVE FM-1 by **swtdr34mz**: drones made from the sound chips of the old home computers,
consoles and DOS games – the pulse waves and the noise register of the NES, the 4-bit waves of the Game Boy, the
arpeggio chords and the ring modulator of the C64, the dirty polynomial counters of the Atari POKEY, the FM of the
AdLib card, the 1-bit PC speaker. In a dungeon: every page is a room with its own monster.

![GARGOYLE](site/img/screen-main.png) ![WRATH](site/img/screen-wrath.png) ![Intro](site/img/screen-intro.png)

## Install

Download the `.fwsc` from [Releases](https://github.com/swtdr34mz-afk/GARGOYLE/releases) and install it like any FM-1
firmware (The Firmware Hub, or the M-Flasher). Manuals: [Deutsch](docs/HANDBUCH.md), [English](docs/MANUAL.md).

## Play

One knob, one parameter (like FIKH-S), six pages:

| Button | Page | K1 | K2 | K3 | K4 |
|---|---|---|---|---|---|
| HOME | SUMMON | CURSE (ring / sync, FM) | HORDE (octaves / detune) | RUNE (wave, duty, table) | ALCHEMY (per algorithm) |
| HOME 2× | GROOVE | DENSITY | BREATH | DISTANCE | TEMPO |
| HOME 3× | SETUP | LEVEL | HUMAN | FOLLOW | TUNE |
| FX | ECHO | ECHO (mix) | HAUNT (feedback, left ping-pong, fully out endless) | DECAY (echoes turn into a fog: duller, washed, grainy at the top) | DEPTH (time) |
| ENV | RITUAL | TORCH (low / high pass) | PORTAL (±24 semitones) | AWAKEN (attack) | SLUMBER (release) |
| EDIT | LORE | CREEP (everything glides: pitch up to 12 s, knobs up to 8 s) | WANDER (pitch, timbre, filter, pan and level drift by themselves) | BITS (16 .. 1 bit) | VOLUME |
| GLO | SPELL | AGE (sample rate down to 2 kHz) | AURA (a resonator: chip tones ring into a drone) | SPEED (arpeggios, patterns) | WIDTH |
| LFO (1–4×) | OMEN I–IV, four LFOs | RATE (free, or note values with the GROOVE) | SHAPE (sine, triangle, saw, square, stairs, chance, drift, swell, heart, bounce, chaos, turing) | POWER | PATH (where it goes, always the sound, never the stereo field: BREATH, FLAME, HAUNT, AURA, RUNE, CURSE, DECAY, STORM, MORPH) |

SELECT is **WRATH**, one macro over everything for live play (past half the dragon appears). PRESET picks the
algorithm, ALGORITHM is QUEST, a MORPH into the next algorithm, REC is FATE (dice, also the variants; held 1 s: deep). PLAY: HOLD, ARP: FREEZE (the
echo holds), SEQ held: factory sound, SAVE + key / SEL + key: 8 snapshots. MIDI: notes, pitch bend,
CC 1 RUNE, CC 70–73 CURSE HORDE RUNE ALCHEMY, CC 75 WRATH, clock / start / stop for the GROOVE.

**The 16 algorithms:** TORCHLIGHT (NES pulse pair), CRYPT CHOIR (Game Boy: two pulse channels, the 4-bit wave channel, sweep), GOBLIN CHORDS (C64 arpeggio),
DRAGON BREATH (NES noise), PC SPEAKER (1-bit beeper), POKEY CURSE (Atari polys), SID RITUAL (C64 ring / sync), ADLIB
HALL (OPL FM), BEHOLDER (tracker vibrato), TREASURE (FM bells), SKELETON MARCH (triangle and ticks), MAGE TOWER (wave
morph), THRONE OF BONES (octave arpeggio), PIT OF DOOM (divider logic), WIND OF ABYSS (filtered noise), SPELLBOOK
(random notes). No reverb: an echo that DECAY turns into a fog. Every sound fades in slowly from the factory, WANDER makes it drift.

**GROOVE:** ten styles of dungeon noises as the old consoles made them (square, 4-bit triangle, the noise register,
16 volume steps, 60 frames a second, 6 bits): HEARTBEAT, FOOTSTEPS, STONES, CHAINS, WINGS, RITUAL, BELL, BONES, RUMBLE,
TORCH, tuned to the drone, with a pump (BREATH).

## Building

`X0X_FONTSET=terminus python3 fm1-base/tools/build.py . --host`, then `build/host/gargoyle_host tests/basic.txt
build/host` (also `groove`, `algos`, `snaps`, `battery`). The pictures: `python3 tools/gen_art.py` → art.h.

## Licence

MPL-2.0. The serif font is Liberation Serif (SIL OFL 1.1). See [CREDITS.md](CREDITS.md).
