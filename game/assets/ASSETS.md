# Third-party assets — provenance & licenses

Every file under `assets/` is listed here with its source, author, license,
and any transformation we applied. The engine never *requires* assets: if a
file is missing, `fd_audio.h` falls back to its built-in procedural synth.

## Sound effects (`assets/sfx/`)

All five SFX come from two Kenney.nl packs, both **Creative Commons Zero
(CC0 1.0)** — public domain dedication, no attribution required (we credit
anyway; Kenney asks only optionally). Original `License.txt` files are kept
verbatim alongside this file.

| file | original | pack | author | license |
|---|---|---|---|---|
| `sfx/jump.wav` | `phaseJump1.ogg` | [Digital Audio](https://kenney.nl/assets/digital-audio) | Kenney (kenney.nl) | CC0 1.0 |
| `sfx/land.wav` | `impactSoft_heavy_001.ogg` | [Impact Sounds](https://kenney.nl/assets/impact-sounds) | Kenney (kenney.nl) | CC0 1.0 |
| `sfx/step.wav` | `footstep_concrete_002.ogg` | [Impact Sounds](https://kenney.nl/assets/impact-sounds) | Kenney (kenney.nl) | CC0 1.0 |
| `sfx/bump.wav` | `impactWood_medium_000.ogg` | [Impact Sounds](https://kenney.nl/assets/impact-sounds) | Kenney (kenney.nl) | CC0 1.0 |
| `sfx/blip.wav` | `pepSound3.ogg` | [Digital Audio](https://kenney.nl/assets/digital-audio) | Kenney (kenney.nl) | CC0 1.0 |

- **Retrieved:** 2026-06-11, from kenney.nl directly.
- **Transformation:** converted OGG → WAV (PCM s16, mono, 22050 Hz) with
  ffmpeg to match the engine mixer's native format. No other edits.
- **License texts:** `LICENSE-kenney-impact-sounds.txt`,
  `LICENSE-kenney-digital-audio.txt` (verbatim from the packs).
- CC0 reference: <https://creativecommons.org/publicdomain/zero/1.0/>

## Ground rules for adding assets

1. CC0 strongly preferred; CC-BY acceptable WITH the attribution recorded
   here and in any distributed credits screen. No NC/ND/SA licenses — they
   conflict with shipping a game.
2. Every addition gets a row in the table above: file, original name, source
   URL, author, license, retrieval date, transformations.
3. Keep the original license text file next to this one.
4. Assets are always optional at runtime — procedural fallback stays.
