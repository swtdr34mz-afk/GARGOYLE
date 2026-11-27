# Credits

GARGOYLE by swtdr34mz. The sound core is the one of FIKH-S (also by swtdr34mz: its twenty-two algorithms, the
processors, the ambient reverb), used here under four macros. The GROOVE (dsp/garg_groove.c), the screens, the
corridors (rendered from code by tools/gen_art.py into art.h) and everything else are written for this project; all
sounds are synthesized in the firmware: no samples. The look is inspired by the dungeon games of the early 90s; no
artwork, logo or name of any game is used.

- The filter is a state-variable filter after Vadim Zavalishin's published topology-preserving form.
- The frequency shifter's 90° allpass pair uses the coefficients Olli Niemitalo published for a Hilbert transformer.

## Built on

| Project | Author | Licence | Used for |
| --- | --- | --- | --- |
| [fm1-base](https://github.com/charlesvestal/fm1-base) | Charles Vestal | MPL-2.0 | the platform: screen, panel, audio, MIDI, storage, simulator, build tools, the Terminus pixel fonts it carries (SIL OFL 1.1) |
| [felucca-bsp](https://github.com/hugelton/felucca-bsp) | Leo Kuroshita (Hügelton Instruments) and contributors | MPL-2.0 | the hardware layer, update loader, packaging |
| [Liberation Serif](https://github.com/liberationfonts) | Red Hat, Inc.; digitized data Google Corporation | SIL OFL 1.1 | the engraved serif labels (ui_serif.h, rendered by tools/gen_serif.py) |
| JieLi's AC79 SDK | JieLi Technology | Apache-2.0 | three files in every `.fwsc` package (fetched at build time, never copied here) |
