# PSP display hotfix 2.0.1 / исправление изображения

## English

The report concerns the latest GitHub release, Rebuilt 2.0.0: wrong or inverted
colors and two images on a real PSP. The console model and firmware were not
provided. No physical PSP was available for this fix, so hardware confirmation
remains open.

### Confirmed defect

The original linked ELF places `display_list` at `0x088d1190` and the mutable
`initialized`, `overflow`, and `used` variables at `0x088d1180`, `0x088d1181`,
and `0x088d1184`. They share the same 64-byte CPU cache line. The list was only
aligned to 16 bytes. Its end also shared a cache line with `batches`.

PSPSDK's [sceGuStart implementation](https://github.com/pspdev/pspsdk/blob/master/src/gu/sceGuStart.c)
writes commands through an uncached address. A later writeback of a dirty
cached neighbor can overwrite those commands with stale bytes. Framebuffer
format is one of the initialization commands; losing it can leave GE drawing
16-bit pixels while the display reads 32-bit pixels. This is a plausible
explanation for the reported combination of distorted colors and duplicate
images, not a claim that the physical symptom was reproduced here.

### Changes

- Align and size command lists to whole 64-byte cache lines; separate cached
  geometry and UI vertex buffers in the same way.
- Write back and invalidate the command-list cache alias before its first use.
- Reassert `GU_PSM_8888`, the draw address and the 512-pixel stride each frame.
  PSPSDK's `sceGuStart` itself restores address/stride, but not pixel format.
- Inspect actual linked ELF symbols during every build. Reject shared first
  or last cache lines, even if the source appears correctly aligned.
- Test the original defective address, a partial final line, missing symbols
  and truncated ELF data. The PSP smoke driver also checks GE/LCD format and
  stride each frame while forcing CPU cache writebacks.

The release executable is separate from the smoke executable. The old 2.0 ZIP
is retained. Existing game folders are backed up by `tools/project.py install`.

### Verification on 2026-10-14

- Production ELF: `display_list=0x088d1280`, `geometry=0x089d1900`,
  `vertices=0x08a5e340`; all starts and allocation sizes are multiples of 64.
- PSPDEV build passed with `-Wall -Wextra -Werror`.
- Core and persistence tests passed under ASan/UBSan; eight Python tests passed.
- PPSSPP completed 1320 frames with matching GE/LCD `8888/512` state, eleven
  captures and a clean shutdown. Menu, Level 0 and Poolrooms captures were
  visually inspected. The [run log](display-fix-ppsspp.log) is retained.
- PPSSPP does not establish that the physical PSP cache fault is resolved.
  Sleep/resume and the reported visual symptom still need console testing.

Re-run the normal build and tests with `python3 tools/project.py build`.
Python regression tests: `python3 -m unittest discover -s tests -p test_project.py -v`.
If macOS reports `libSystem.tbd: unknown architecture` because its selected SDK
and linker differ, use the SDK belonging to Xcode:
`SDKROOT="$(xcrun --sdk macosx --show-sdk-path)" python3 tools/project.py build`.

### Check on a PSP

1. Close the game completely. Back up the existing `PSP/GAME/BACKROOMS3D` folder.
2. Extract `Backrooms-PSP-Rebuilt-v2.0.1.zip` and copy its `BACKROOMS3D` folder
   to `PSP/GAME/`. Keep your `settings.ini` and `settings.ini.bak` files.
3. Start from the PSP menu, not a saved emulator state. Verify that
   `backrooms.log` starts with `rewrite 2.0.1` and includes `Video 2.0.1`.
4. Check menu colors, Level 0, Poolrooms, map and pause, then sleep/resume.
   There should be one full-width image with legible text and normal colors.
5. If the symptom remains, provide the PSP model, firmware/CFW version,
   `backrooms.log` and a photo. The old folder can be restored to roll back.

## Русский

Сообщение относится к последнему релизу GitHub, Rebuilt 2.0.0: неправильные
или инвертированные цвета и два изображения на настоящей PSP. Модель консоли
и прошивка неизвестны. Физической PSP для проверки исправления здесь нет.

В готовом ELF найден дефект: список команд GPU и изменяемые переменные
попадали в одну строку CPU-кэша. GPU-команды записываются в обход кэша,
а запись соседних переменных через кэш может затереть их устаревшими данными.
Если так теряется команда формата кадра, GPU и экран начинают по-разному
трактовать размер пикселя. Это объясняет сочетание неверных цветов и двойной
картинки, но окончательное подтверждение требует запуска на консоли.

В 2.0.1 списки команд и буферы вершин изолированы по границам 64 байт.
Перед первым использованием списка очищается его кэшированный адрес,
а формат 8888 и шаг строки 512 пикселей задаются заново каждый кадр.
Проверка сборки читает адреса из готового ELF и отклоняет опасное размещение.

Для проверки полностью закройте игру, сохраните копию старой папки и
замените её содержимым архива 2.0.1. Сохраните `settings.ini` и
`settings.ini.bak`. Запускайте из меню PSP. В `backrooms.log` должны быть
строки `rewrite 2.0.1` и `Video 2.0.1`. Проверьте меню, оба уровня, карту,
паузу и выход из сна. При повторении проблемы нужны модель PSP, версия CFW,
лог и фотография экрана. Для отката верните сохранённую папку.
