#ifndef BR_GAME_H
#define BR_GAME_H

#include "world.h"

enum {
    BTN_UP=1u<<0, BTN_DOWN=1u<<1, BTN_LEFT=1u<<2, BTN_RIGHT=1u<<3,
    BTN_ACCEPT=1u<<4, BTN_BACK=1u<<5, BTN_USE=1u<<6, BTN_MAP=1u<<7,
    BTN_PAUSE=1u<<8, BTN_L=1u<<9, BTN_R=1u<<10
};
enum { EVENT_SAVE=1, EVENT_QUIT=2 };
typedef enum { SCREEN_TITLE, SCREEN_PLAY, SCREEN_PAUSE, SCREEN_SETTINGS,
               SCREEN_DEAD, SCREEN_TRANSITION, SCREEN_WIN } Screen;
typedef struct {
    float sensitivity, brightness;
    int volume, head_bob, show_fps, checkpoint;
} Settings;
typedef struct { float forward, turn; uint32_t held, pressed; } Input;
typedef struct {
    World world;
    Settings settings;
    Screen screen, settings_return;
    Vec2 player, enemy, enemy_goal;
    float yaw, stamina, step_phase, level_time, run_time, transition_time;
    float path_timer, notice_time, threat;
    int16_t distances[WORLD_CELLS];
    uint8_t visited[WORLD_CELLS];
    bool relays[RELAY_COUNT], practice, map_open, exhausted, save_failed;
    int menu, relay_total, revision, events;
    char notice[80];
} Game;

void settings_defaults(Settings *settings);
void game_init(Game *game, const Settings *settings);
void game_start(Game *game, int level, bool practice);
void game_step(Game *game, Input input, float dt);
int game_near_relay(const Game *game);
bool game_near_exit(const Game *game);
void game_notice(Game *game, const char *message);

#endif
