# Credits

GARGOYLE by swtdr34mz. The sound core (dsp/chip_core.c), the GROOVE (dsp/garg_groove.c), the screens and everything
else are written for this project; all sounds are synthesized in the firmware, after the principles of the old sound
chips (no code or sound of any chip or game). The pictures of the rooms were made by swtdr34mz with ChatGPT
(tools/art/scenes.png, cut and reduced to a palette by tools/gen_art.py into art.h); the title lettering and the torch
flames are drawn from code. No artwork, logo or name of any game is used.


## Built on

| Project | Author | Licence | Used for |
| --- | --- | --- | --- |
| [fm1-base](https://github.com/charlesvestal/fm1-base) | Charles Vestal | MPL-2.0 | the platform: screen, panel, audio, MIDI, storage, simulator, build tools, the Terminus pixel fonts it carries (SIL OFL 1.1) |
| [felucca-bsp](https://github.com/hugelton/felucca-bsp) | Leo Kuroshita (Hügelton Instruments) and contributors | MPL-2.0 | the hardware layer, update loader, packaging |
| [Liberation Serif](https://github.com/liberationfonts) | Red Hat, Inc.; digitized data Google Corporation | SIL OFL 1.1 | the engraved serif labels (ui_serif.h, rendered by tools/gen_serif.py) |
| JieLi's AC79 SDK | JieLi Technology | Apache-2.0 | three files in every `.fwsc` package (fetched at build time, never copied here) |
