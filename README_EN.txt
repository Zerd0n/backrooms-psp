BACKROOMS PSP — REBUILT 2.0

This version was rebuilt with GPT-6.0 Astra. It helped add a main menu,
settings, camera bob, a checkpoint, map, pause screen, and other game
details. I tested the game in PPSSPP. FPS on a real PSP may differ because
my console is broken and I could not test it there.

RUN WITHOUT BUILDING
1. Extract Backrooms-PSP-Rebuilt-v2.0.zip.
2. In PPSSPP, use Load and open BACKROOMS3D/EBOOT.PBP.
3. On a PSP that can run homebrew, copy the whole BACKROOMS3D folder to
   PSP/GAME on the memory stick. Start it from Game > Memory Stick.
4. Keep the assets folder beside EBOOT.PBP. It contains the sound tracks.

WHAT TO DO
Find three yellow wall relays in each level. Walk up to one and press
Square. After all three are active, find the door with the green light and
press Square. Level 0 leads to the Poolrooms, then to the ending. The
nextbot begins moving after 14 seconds. Run and use nearby rooms to lose
it. Water slows you down.

PSP CONTROLS
Analog stick / D-pad up-down: move.
Analog stick / D-pad left-right: turn.
L / R: strafe left / right.
Hold X: sprint. The bar at the bottom shows stamina.
Square: interact.
Triangle: show the explored map (the game does not pause).
START: pause. Circle: back to the menu. X: confirm a menu item.
In PPSSPP, use the keys mapped to these PSP buttons. You can view them in
Settings > Controls > Control Mapping.

MENU
NEW GAME: begin at Level 0.
CONTINUE FROM CHECKPOINT: begin at the Poolrooms if it is unlocked;
otherwise begin at Level 0. Your position and relay state are not saved.
EXPLORE WITHOUT PURSUER: play both maps without the nextbot.
SETTINGS: turn sensitivity, brightness, volume, camera bob, and FPS.
EXIT GAME: close the game safely.

SAVES
settings.ini is created next to EBOOT.PBP. The previous valid version is
stored in settings.ini.bak. If the main file is damaged, the game loads the
backup or uses default settings. It saves when you leave settings, enter the
Poolrooms, or exit normally. Explore mode does not change the checkpoint.

TROUBLESHOOTING
No sound: make sure assets/*.raw is present. The game can run without sound;
the reason is written to backrooms.log.
Game does not start: use EBOOT.PBP, not the ELF. Check the
PSP/GAME/BACKROOMS3D folder, then try the same build in PPSSPP.
Settings do not save: check free space and write access.
After sleep mode, the game pauses. Do not turn the power off while saving.

SOURCE AND BUILD
In the repository, open README_EN.md for the full English documentation.
Main command:
    python3 tools/project.py build
Ready-to-copy game: dist/BACKROOMS3D.
Archive: release/Backrooms-PSP-Rebuilt-v2.0.zip.
