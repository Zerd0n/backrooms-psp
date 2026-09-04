#ifndef BR_PLATFORM_H
#define BR_PLATFORM_H

#include "../core/game.h"

void log_open(void);
void log_write(const char *format, ...) __attribute__((format(printf,1,2)));
void log_close(void);
void settings_load(Settings *settings);
bool settings_save(const Settings *settings);
bool renderer_init(void);
bool renderer_world(const Game *game);
void renderer_draw(const Game *game, float fps, bool audio_available);
void renderer_shutdown(void);
bool audio_init(void);
void audio_update(const Game *game);
void audio_shutdown(void);

#endif
