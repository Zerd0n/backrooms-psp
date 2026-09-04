# Backrooms PSP

[Русская версия](README.md)

A small PSP horror game set first in Level 0, then in the Poolrooms. You need to power three relays, open the exit, and stay ahead of the pursuing nextbot.

I rewrote this version from scratch in C99 for PSPSDK. The old code is gone. The familiar textures, PSP menu art, chase sound, and nextbot sprite remain, but the game now has a new renderer, level system, and gameplay code.

## What's in this version

GPT-6.0 Astra exceeded my expectations and pushed the project much further than I expected. It helped add camera bob while walking, a checkpoint after the first level, a main menu, pause screen, sensitivity, brightness and volume settings, an explored-room map, sprinting with stamina, and an exploration mode without the enemy.

The nextbot finds its way through the map and the chase music gets louder as it closes in. Water in the Poolrooms slows the player down. The game stores settings and the unlocked checkpoint next to `EBOOT.PBP`.

## Running the game

The ready-to-copy build is in `dist/BACKROOMS3D`. You can also create `release/Backrooms-PSP-Rebuilt-v2.0.zip` with `python3 tools/project.py build`.

In PPSSPP, open `BACKROOMS3D/EBOOT.PBP`. On a PSP, copy the whole `BACKROOMS3D` folder to `PSP/GAME/` on the memory stick. Keep the `assets` folder next to `EBOOT.PBP` or the game will have no sound.

I tested the game in PPSSPP 1.20.4. FPS on a real PSP may be different: I could not test this version on my own console because it is broken. The roughly 60 FPS counter in the emulator is not a hardware benchmark.

## Controls

| Button | Action |
| --- | --- |
| Analog stick or D-pad up/down | Move forward or backward |
| Analog stick or D-pad left/right | Turn |
| L / R | Strafe |
| Hold X | Sprint |
| Square | Activate a relay or open the exit |
| Triangle | Open the explored map |
| Start | Pause |
| Circle | Go back to the menu |

Each level has three relays. When all of them are on, the exit door opens. Level 0 leads to the Poolrooms, and the Poolrooms lead to the ending.

## Building

You need Python 3, Docker Desktop with its engine running, and about 2 GB of free disk space. The build script uses a pinned PSPDEV image and does not install Python packages.

```sh
git clone https://github.com/Zerd0n/backrooms-psp.git
cd backrooms-psp
python3 tools/project.py build
```

The command runs logic and storage tests, builds `EBOOT.PBP`, checks the package, and creates a ZIP file in `release`. Logs go to `logs`.

To check an existing build:

```sh
python3 tools/project.py verify
```

To install to a mounted memory stick:

```sh
python3 tools/project.py install /Volumes/PSP
```

The installer saves the previous game folder as `BACKROOMS3D.backup.<date>`. Restore it with:

```sh
python3 tools/project.py restore /Volumes/PSP BACKROOMS3D.backup.<date>
```

## Testing

The automated checks cover maps, collision, nextbot pathfinding, death and retry, checkpoints, and settings writes. I also ran the game through a separate PPSSPP scenario. It showed the main menu, both levels, the map, pause menu, settings, nextbot sprite, death screen, and ending. Screenshots are in `docs/screenshots`; [RELEASE_NOTES.md](RELEASE_NOTES.md) has the full test record.

[docs/SOURCE.md](docs/SOURCE.md) lists the full source. The repository contains only the files needed to build and run the game: embedded textures, runtime assets, and source code.
