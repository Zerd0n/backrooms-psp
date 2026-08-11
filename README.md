# Backrooms PSP

This is a small Backrooms-style homebrew game for the Sony PSP. It began as a practical test: how capable is GPT-5.6 of helping revive indie game development on the PSP right now, when a person directs the work, tests every build, and rejects the bad iterations?

The current result is a playable two-level build, not a finished commercial game. It has a custom software raycaster, a pursuing nextbot, ambient sound, chase music, and a ready-to-run PSP package. The code, asset conversion tools, and build scripts are all included in this repository.

## About the experiment

The point of this project is to test GPT-5.6 through an actual constrained game-development job. The PSP has limited memory, a 333 MHz CPU, a 480 x 272 display, and a toolchain very different from modern desktop engines. That makes it a more useful test than asking a model to generate a few isolated code samples.

A person set the direction, supplied and reviewed assets, tested the game in PPSSPP, and asked for corrections. GPT-5.6 wrote and revised much of the implementation and tooling. This repository is a snapshot of that collaboration. It is an experiment, not proof that a model can replace a game developer.

## What is in this build

- Level 0 and Poolrooms maps
- A C software raycaster written for the PSP
- 480 wall columns and a lighter logical resolution for floors and ceilings
- Distance fog, texture mipmaps, and smoothed floor sampling
- Dark Poolrooms lighting
- Nextbot pursuit on both levels with grid-based pathfinding
- Separate ambient loops for Level 0 and Poolrooms
- Distance-based chase audio
- Analog stick and D-pad controls
- A ready-to-run `BACKROOMS3D` folder and a downloadable ZIP in GitHub Releases

## Download and install

Open the [latest GitHub release](../../releases/latest) and download `Backrooms-PSP-v0.1.0.zip`.

For PPSSPP:

1. Extract the ZIP.
2. Open `BACKROOMS3D/EBOOT.PBP` in PPSSPP.

For a real PSP:

1. Use a PSP that can run homebrew.
2. Extract the ZIP.
3. Copy the whole `BACKROOMS3D` folder to `/PSP/GAME/` on the Memory Stick.
4. Start **Backrooms PSP** from **Game > Memory Stick**.

The final path must be `/PSP/GAME/BACKROOMS3D/EBOOT.PBP`. An extra nested `BACKROOMS3D` folder will stop the game from appearing correctly.

## Controls

| Input | Action |
| --- | --- |
| Analog up / down | Move forward / backward |
| Analog left / right | Turn |
| D-pad | Movement and turning fallback |
| X, held | Run |
| X on game over | Restart |
| L + R + Triangle | Load Poolrooms |
| L + R + Circle | Load Level 0 |
| L + R + Square | Toggle the nextbot |
| L + Square | Respawn the nextbot farther away |

## Building from source

### Chosen language

The game is written in C because PSPSDK exposes the console's display, audio, controller, and kernel APIs directly with very little runtime overhead. Python is used only for deterministic texture and audio conversion on the development machine, while Bash keeps the Docker build reproducible.

### Requirements

- macOS or Linux
- Docker Desktop or another working Docker engine
- Python 3.9 or newer
- About 1 GB of free space for the PSPDEV image and build files

No Python packages are required. The preprocessing scripts use the standard library.

### Build

```bash
git clone https://github.com/Zerd0n/backrooms-psp-gpt56.git
cd backrooms-psp-gpt56
./tools/build.sh
```

The script converts the source assets, builds the game in `pspdev/pspdev:latest`, retries transient container failures, verifies the EBOOT and audio files, and writes timestamped logs to `logs/`. The finished package is placed in `dist/BACKROOMS3D`.

To install it on a mounted PSP Memory Stick:

```bash
./tools/install_to_psp.sh /Volumes/PSP
```

The installer backs up an existing copy before replacing it. See [README_RU.txt](README_RU.txt) for the older detailed build, rollback, and troubleshooting notes.

## Repository layout

```text
assets/          Generated PSP menu art and raw audio
dist/            Ready-to-run game folder
source_assets/   Original textures and audio used by the converters
src/             C source and generated texture headers
tools/           Build, conversion, verification, and install scripts
Makefile         PSPSDK build configuration
```

## Known limitations

- This is an experimental snapshot and has no save system or settings menu.
- Visual quality is bounded by PSP performance and the software-rendered pipeline.
- Controls and audio were tested primarily in PPSSPP. Real hardware and custom firmware combinations may behave differently.
- Poolrooms is deliberately dark. That is part of the current level direction, not a display calibration target.

## Asset note

The current nextbot image and chase audio were supplied by the project owner for testing. Their copyright and redistribution status has not been independently verified. Check that you have the necessary rights before publishing a public mirror or redistributing a modified release. No license in this repository should be read as granting rights to third-party media.

---

# Backrooms PSP на русском

Это небольшая homebrew-игра по мотивам Backrooms для Sony PSP. Проект начался как практический тест: насколько GPT-5.6 сейчас способна помочь возродить инди-геймдев на PSP, если человек задаёт направление, проверяет каждую сборку и отправляет неудачные варианты на переделку?

Сейчас это рабочая версия с двумя уровнями, а не законченная коммерческая игра. В ней есть собственный программный raycaster, преследующий игрока некстбот, эмбиент, музыка погони и готовая папка для запуска на PSP. Исходный код, конвертеры ассетов и скрипты сборки лежат в этом репозитории.

## Зачем сделан этот проект

Задача эксперимента: проверить GPT-5.6 на настоящей разработке с жёсткими ограничениями. У PSP мало памяти, процессор с частотой до 333 МГц, экран 480 x 272 и непривычный по современным меркам инструментарий. Поэтому такой проект говорит о возможностях модели больше, чем несколько отдельных примеров кода.

Человек задавал направление, подбирал и проверял ассеты, запускал игру в PPSSPP и требовал исправлений. GPT-5.6 написала и неоднократно переработала значительную часть кода и инструментов. Этот репозиторий фиксирует результат совместной работы. Это эксперимент, а не доказательство того, что модель может заменить разработчика игр.

## Что есть в этой версии

- Уровни Level 0 и Poolrooms
- Программный raycaster на C, рассчитанный на PSP
- 480 столбцов для стен и облегчённое внутреннее разрешение пола и потолка
- Дистанционный туман, mipmap-текстуры и сглаживание пола
- Специально затемнённый Poolrooms
- Погоня некстбота на обоих уровнях с поиском пути по сетке
- Отдельный эмбиент для Level 0 и Poolrooms
- Музыка погони, громкость которой зависит от расстояния
- Управление стиком и крестовиной
- Готовая папка `BACKROOMS3D` и ZIP-архив в GitHub Releases

## Как скачать и запустить

Откройте [последний релиз GitHub](../../releases/latest) и скачайте `Backrooms-PSP-v0.1.0.zip`.

Для PPSSPP:

1. Распакуйте ZIP.
2. Откройте `BACKROOMS3D/EBOOT.PBP` в PPSSPP.

Для настоящей PSP:

1. Нужна консоль, на которой разрешён запуск homebrew.
2. Распакуйте ZIP.
3. Скопируйте всю папку `BACKROOMS3D` в `/PSP/GAME/` на карте памяти.
4. Запустите **Backrooms PSP** через **Игра > Memory Stick**.

Итоговый путь должен выглядеть так: `/PSP/GAME/BACKROOMS3D/EBOOT.PBP`. Если случайно создать ещё одну вложенную папку `BACKROOMS3D`, игра может не появиться в меню.

## Управление

| Кнопка | Действие |
| --- | --- |
| Стик вверх / вниз | Идти вперёд / назад |
| Стик влево / вправо | Поворачивать |
| Крестовина | Запасной вариант движения и поворота |
| Удерживать X | Бежать |
| X после проигрыша | Начать заново |
| L + R + Triangle | Перейти в Poolrooms |
| L + R + Circle | Перейти в Level 0 |
| L + R + Square | Включить или выключить некстбота |
| L + Square | Переместить некстбота подальше |

## Сборка из исходников

### Почему C

Игра написана на C, потому что PSPSDK даёт прямой доступ к экрану, звуку, управлению и системным функциям PSP без тяжёлой среды выполнения. Python нужен только для предсказуемой конвертации текстур и звука на компьютере, а Bash собирает всё одной воспроизводимой командой через Docker.

### Что Понадобится

- macOS или Linux
- Docker Desktop или другой рабочий Docker Engine
- Python 3.9 или новее
- Около 1 ГБ свободного места под образ PSPDEV и файлы сборки

Ставить пакеты через `pip` не нужно. Скрипты используют стандартную библиотеку Python.

### Команды

```bash
git clone https://github.com/Zerd0n/backrooms-psp-gpt56.git
cd backrooms-psp-gpt56
./tools/build.sh
```

Скрипт конвертирует исходные ассеты, собирает игру в `pspdev/pspdev:latest`, повторяет временно упавшие операции, проверяет EBOOT и аудиофайлы, а затем сохраняет логи с датой и временем в `logs/`. Готовая игра появится в `dist/BACKROOMS3D`.

Чтобы поставить сборку на подключённую карту памяти PSP:

```bash
./tools/install_to_psp.sh /Volumes/PSP
```

Перед заменой installer сохраняет предыдущую версию. Старое подробное руководство по сборке, откату и диагностике находится в [README_RU.txt](README_RU.txt).

## Структура репозитория

```text
assets/          Подготовленные картинки меню и звук для PSP
dist/            Готовая папка игры
source_assets/   Исходные текстуры и аудио для конвертеров
src/             Код на C и сгенерированные заголовки текстур
tools/           Сборка, конвертация, проверка и установка
Makefile         Настройки сборки PSPSDK
```

## Ограничения текущей версии

- Это экспериментальная сборка без сохранений и меню настроек.
- Качество картинки ограничено производительностью PSP и программным рендерингом.
- Управление и звук в основном проверялись в PPSSPP. На разных моделях PSP и версиях CFW результат может отличаться.
- Poolrooms намеренно сделан тёмным. Это выбранное оформление уровня, а не эталон яркости экрана.

## Примечание об ассетах

Текущее изображение некстбота и музыка погони были переданы владельцем проекта для тестирования. Их авторские права и возможность свободного распространения отдельно не проверялись. Перед публикацией открытого зеркала или изменённого релиза убедитесь, что у вас есть нужные разрешения. Никакая лицензия этого репозитория не даёт прав на сторонние медиафайлы.
