BACKROOMS PSP — стабильный MVP для реальной Sony PSP
=====================================================

1. ВЫБРАННЫЙ ЯЗЫК
------------------
C — нативный язык PSPSDK с минимальными накладными расходами и прямым доступом
к PSP display/audio/controller API. Python используется только на Mac для
одноразовой безопасной конвертации PNG/WAV, Bash — для воспроизводимой сборки
и установки.

2. ТРЕБОВАНИЯ И УСТАНОВКА
-------------------------
Рекомендуемый способ:

  - macOS с Docker Desktop;
  - Docker-образ pspdev/pspdev:latest;
  - Python 3.9+ (только для preprocessing/verification, внешние пакеты не нужны);
  - около 1 GB свободного места для Docker-образа и артефактов.

Первый запуск:

  1) Откройте Terminal.
  2) Перейдите в каталог проекта:
       cd /Users/zerdonov/Documents/Backrooms_PSP
  3) Запустите:
       ./tools/build.sh
  4) Дождитесь строки Build and verification succeeded.

Точные Docker-команды без wrapper:

  docker run --rm -v "${PWD}:/src" -w /src pspdev/pspdev:latest make clean
  docker run --rm -v "${PWD}:/src" -w /src pspdev/pspdev:latest make
  python3 tools/verify_project.py

Если Docker Desktop сломан, официальный native fallback для Apple Silicon:

  1) Скачайте pspdev-macos-latest-arm64.tar.gz по официальной инструкции:
     https://pspdev.github.io/installation/macos.html
  2) Установите host-зависимости:
       brew install libmpc zstd
  3) Распакуйте SDK, задайте PSPDEV и добавьте $PSPDEV/bin в PATH.
  4) Выполните make clean, затем make и python3 tools/verify_project.py.

3. КОНФИГУРАЦИЯ
----------------
Игра не требует секретов, API-ключей или сети. Никогда не помещайте секреты в
.env или исходный код.

Необязательные переменные окружения приведены в .env.example:

  BACKROOMS_ASSET_DIR       каталог исходных PNG (source_assets по умолчанию)
  BACKROOMS_CHASE_WAV       исходный chase WAV (source_assets/GAZ.wav)
  BACKROOMS_DOCKER_IMAGE    Docker image (pspdev/pspdev:latest)
  BACKROOMS_DOCKER_CONFIG   каталог безопасного anonymous Docker config

Пример временного переопределения:

  export BACKROOMS_ASSET_DIR="/absolute/path/to/assets"
  python3 tools/convert_textures.py

Исходные ассеты проверяются по точным именам, PNG CRC/формату и размерам.
RGB565 проходит self-test: red=F800, green=07E0, blue=001F.

4. КАК ЗАПУСТИТЬ
----------------
Сначала соберите release-папку командой:

  python3 tools/verify_project.py

Подключите PSP по USB. Если карта памяти смонтирована как /Volumes/PSP:

  ./tools/install_to_psp.sh /Volumes/PSP

Или вручную скопируйте весь каталог:

  dist/BACKROOMS3D  ->  /PSP/GAME/BACKROOMS3D

На PSP откройте Game -> Memory Stick -> Backrooms PSP.

Управление:

  Analog Up/Down          идти вперёд/назад
  Analog Left/Right       поворот
  D-pad / стрелки         полный fallback движения и поворота (удобно в PPSSPP)
  X (удерживать)          бег
  X на Game Over          перезапуск
  L+R+Triangle            Poolrooms
  L+R+Circle              Level 0
  L+R+Square              включить/выключить nextbot
  L+Square                далеко переродить nextbot

Square без модификаторов ничего не отключает.

Графический режим использует 480 горизонтальных лучей для стен и nextbot,
сглаженные текстуры пола/потолка, mipmap-фильтрацию 128/64/32/16 и мягкий
дистанционный туман. Фон рассчитывается в облегчённом режиме. Это уменьшает
самые заметные квадратные пиксели и мерцание без чрезмерной нагрузки на PSP.

Nextbot преследует игрока на обоих уровнях. В Poolrooms он строит маршрут через
проходимые клетки воды и обходы между стенами. Освещение Poolrooms специально
сильно приглушено: поверхности сохраняют примерно половину исходной яркости,
а дальний туман имеет почти чёрный холодный оттенок.

5. КАК ПЛАНИРОВАТЬ / РАЗВЕРНУТЬ
--------------------------------
Игру по расписанию запускать не нужно. Для регулярной ночной проверки сборки
можно добавить в crontab Mac (например, ежедневно в 03:00):

  0 3 * * * cd /Users/zerdonov/Documents/Backrooms_PSP && ./tools/build.sh

Для CI используйте Linux runner с Docker и те же две команды из раздела 2.
Release-артефакт должен содержать ровно:

  BACKROOMS3D/EBOOT.PBP
  BACKROOMS3D/assets/chase.raw
  BACKROOMS3D/assets/ambient_level0.raw
  BACKROOMS3D/assets/ambient_poolrooms.raw

На реальную PSP deploy выполняется только копированием этого каталога. Не
отключайте USB и не извлекайте карту памяти во время копирования.

6. КАК ОТКАТИТЬ / ОТМЕНИТЬ
---------------------------
Installer перед заменой переносит старую версию в:

  PSP/GAME/BACKROOMS3D.backup.YYYYMMDD_HHMMSS

Откат:

  ./tools/install_to_psp.sh --rollback /Volumes/PSP BACKROOMS3D.backup.YYYYMMDD_HHMMSS

Для полного удаления игры удалите только каталог PSP/GAME/BACKROOMS3D на карте
памяти. Логи runtime называются backrooms_YYYYMMDD_HHMMSS.log рядом с EBOOT.

Очистка локальных build-артефактов (исходники и ассеты сохраняются):

  docker run --rm -v "${PWD}:/src" -w /src pspdev/pspdev:latest make clean

7. УСТРАНЕНИЕ НЕПОЛАДОК
-----------------------
Docker зависает на credentials:
  Wrapper использует tools/docker-config/config.json без credentials. Глобальные
  Docker-учётные данные не изменяются.

Docker зависает на container start:
  Выполните docker desktop stop --force --detach, затем docker desktop start
  --detach. Если native Alpine также не запускается, используйте официальный
  native arm64 PSPDEV fallback из раздела 2.

Missing libmpc.3.dylib или libzstd.1.dylib:
  Выполните brew install libmpc zstd.

Нет эмбиента или музыки, но игра запускается:
  Это штатный graceful fallback. Проверьте рядом с EBOOT файлы assets/chase.raw,
  assets/ambient_level0.raw и assets/ambient_poolrooms.raw. Формат каждого:
  mono signed PCM16, 11025 Hz. При погоне эмбиент автоматически приглушается.

Искажены цвета / nextbot синий:
  Повторите python3 tools/convert_textures.py и убедитесь, что self-test печатает
  red=F800 green=07E0 blue=001F PASS.

EBOOT виден, но не запускается:
  Нужна PSP с homebrew-capable custom firmware. Убедитесь, что путь ровно
  PSP/GAME/BACKROOMS3D/EBOOT.PBP, а не дополнительный вложенный каталог.

Черный экран на реальном PSP:
  Текущая версия использует два framebuffer в EDRAM через uncached alias и не
  подключает pspDebugScreen к gameplay framebuffer. Приложите runtime log и
  модель PSP/версию CFW для диагностики.

Все build/preprocessing/install операции пишут timestamped logs в logs/.
