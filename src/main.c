#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspaudio.h>
#include <pspge.h>

#include <math.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "generated/level_maps.h"
#include "generated/nextbot_sprite.h"
#include "generated/pools_ceiling.h"
#include "generated/pools_damp_wall.h"
#include "generated/pools_edge.h"
#include "generated/pools_floor.h"
#include "generated/pools_light.h"
#include "generated/pools_wall.h"
#include "generated/pools_water.h"
#include "generated/texture_carpet.h"
#include "generated/texture_ceiling.h"
#include "generated/texture_dirty_wall.h"
#include "generated/texture_door.h"
#include "generated/texture_light.h"
#include "generated/texture_wall.h"

PSP_MODULE_INFO("Backrooms PSP", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(4 * 1024);

#define SCREEN_W 480
#define SCREEN_H 272
#define FRAME_STRIDE 512
#define WORLD_W SCREEN_W
#define WORLD_H 136
#define BACKGROUND_W 240
#define BACKGROUND_H WORLD_H
#define TEXTURE_MASK 127
#define PI 3.14159265358979323846f
#define FOV_PLANE 0.65f
#define PLAYER_RADIUS 0.18f
#define AUDIO_RATE 11025
#define AUDIO_UPSAMPLE 4
#define AUDIO_FRAMES 512
#define MAX_AUDIO_BYTES (1024 * 1024)
#define MAX_MAP_CELLS (LEVEL0_W * LEVEL0_H)

typedef enum {
    LEVEL_ZERO = 0,
    LEVEL_POOLROOMS = 1
} LevelId;

typedef struct {
    float x;
    float z;
    float angle;
    float eye_height;
} Player;

typedef struct {
    float x;
    float z;
    float target_x;
    float target_z;
    int enabled;
} Nextbot;

static volatile int g_running = 1;
static volatile int g_audio_running = 0;
static volatile int g_chase_mix = 0;
static volatile int g_chase_target_mix = 0;
static volatile int g_ambient_level0_mix = 0;
static volatile int g_ambient_poolrooms_mix = 0;
static int16_t *g_chase_samples = NULL;
static int g_chase_sample_count = 0;
static int16_t *g_ambient_level0_samples = NULL;
static int g_ambient_level0_sample_count = 0;
static int16_t *g_ambient_poolrooms_samples = NULL;
static int g_ambient_poolrooms_sample_count = 0;
static SceUID g_audio_thread = -1;
static int16_t g_audio_buffer[AUDIO_FRAMES] __attribute__((aligned(64)));

static uint32_t *g_front_fb = NULL;
static uint32_t *g_back_fb = NULL;
static float g_zbuffer[SCREEN_W];
static uint32_t g_background[BACKGROUND_W * BACKGROUND_H] __attribute__((aligned(64)));
static int16_t g_path_distance[MAX_MAP_CELLS];
static int16_t g_path_queue[MAX_MAP_CELLS];
static FILE *g_log_file = NULL;

static Player g_player;
static Nextbot g_nextbot;
static LevelId g_level = LEVEL_ZERO;
static int g_game_over = 0;
static uint32_t g_previous_buttons = 0;
static float g_path_timer = 0.0f;
static int g_nextbot_path_distance = NEXTBOT_INITIAL_DISTANCE;
static int g_input_logged = 0;
static int g_chase_was_audible = 0;

static void log_message(const char *level, const char *format, ...)
{
    char message[384];
    char line[448];
    va_list args;
    time_t now = time(NULL);
    struct tm *local = localtime(&now);

    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (local != NULL) {
        snprintf(line, sizeof(line), "[%04d-%02d-%02d %02d:%02d:%02d] [%s] %s",
                 local->tm_year + 1900, local->tm_mon + 1, local->tm_mday,
                 local->tm_hour, local->tm_min, local->tm_sec, level, message);
    } else {
        snprintf(line, sizeof(line), "[%s] %s", level, message);
    }
    printf("%s\n", line);
    if (g_log_file != NULL) {
        fprintf(g_log_file, "%s\n", line);
        fflush(g_log_file);
    }
}

static void open_session_log(void)
{
    char filename[96];
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    if (local != NULL) {
        snprintf(filename, sizeof(filename), "backrooms_%04d%02d%02d_%02d%02d%02d.log",
                 local->tm_year + 1900, local->tm_mon + 1, local->tm_mday,
                 local->tm_hour, local->tm_min, local->tm_sec);
    } else {
        snprintf(filename, sizeof(filename), "backrooms_session.log");
    }
    g_log_file = fopen(filename, "a");
}

static int exit_callback(int arg1, int arg2, void *common)
{
    (void)arg1;
    (void)arg2;
    (void)common;
    g_running = 0;
    g_audio_running = 0;
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    int callback_id;
    (void)args;
    (void)argp;
    callback_id = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    if (callback_id >= 0) {
        sceKernelRegisterExitCallback(callback_id);
        sceKernelSleepThreadCB();
    }
    return 0;
}

static int setup_callbacks(void)
{
    SceUID thread_id = sceKernelCreateThread("callback_thread", callback_thread, 0x11, 0x1000, 0, NULL);
    if (thread_id < 0) {
        return thread_id;
    }
    return sceKernelStartThread(thread_id, 0, NULL);
}

static uint32_t rgb565_to_8888(uint16_t color)
{
    uint32_t r = (color >> 11) & 31U;
    uint32_t g = (color >> 5) & 63U;
    uint32_t b = color & 31U;
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return 0xFF000000U | (b << 16) | (g << 8) | r;
}

static uint32_t shade_color(uint32_t color, int shade)
{
    uint32_t r = color & 0xFFU;
    uint32_t g = (color >> 8) & 0xFFU;
    uint32_t b = (color >> 16) & 0xFFU;
    r = (r * (uint32_t)shade) >> 8;
    g = (g * (uint32_t)shade) >> 8;
    b = (b * (uint32_t)shade) >> 8;
    return 0xFF000000U | (b << 16) | (g << 8) | r;
}

static void put_world_pixel(int x, int y, uint32_t color)
{
    uint32_t *top = g_back_fb + (y << 1) * FRAME_STRIDE + x;
    top[0] = color;
    top[FRAME_STRIDE] = color;
}

static uint32_t blend_color(uint32_t color, uint32_t target, int amount)
{
    int inverse = 256 - amount;
    uint32_t r = (((color & 0xFFU) * (uint32_t)inverse) + ((target & 0xFFU) * (uint32_t)amount)) >> 8;
    uint32_t g = ((((color >> 8) & 0xFFU) * (uint32_t)inverse) + (((target >> 8) & 0xFFU) * (uint32_t)amount)) >> 8;
    uint32_t b = ((((color >> 16) & 0xFFU) * (uint32_t)inverse) + (((target >> 16) & 0xFFU) * (uint32_t)amount)) >> 8;
    return 0xFF000000U | (b << 16) | (g << 8) | r;
}

static uint32_t level_fog_color(void)
{
    /* Poolrooms fog is deliberately near-black and cold: RGB(18, 27, 32). */
    return g_level == LEVEL_ZERO ? 0xFF699196U : 0xFF201B12U;
}

static int level_brightness(int shade, int poolrooms_reduction)
{
    if (g_level == LEVEL_POOLROOMS) {
        shade -= poolrooms_reduction;
    }
    if (shade < 0) return 0;
    if (shade > 256) return 256;
    return shade;
}

static int fog_amount(float distance)
{
    float start = g_level == LEVEL_ZERO ? 4.0f : 3.0f;
    float end = g_level == LEVEL_ZERO ? 18.0f : 15.0f;
    float normalized;
    if (distance <= start) return 0;
    if (distance >= end) return 224;
    normalized = (distance - start) / (end - start);
    return (int)(normalized * normalized * 224.0f);
}

static int texture_lod(float distance)
{
    if (distance < 2.75f) return 0;
    if (distance < 5.5f) return 1;
    if (distance < 10.5f) return 2;
    return 3;
}

static void present_background(void)
{
    int y;
    for (y = 0; y < BACKGROUND_H; ++y) {
        uint32_t *top = g_back_fb + (y << 1) * FRAME_STRIDE;
        int x;
        for (x = 0; x < BACKGROUND_W; ++x) {
            uint32_t color = g_background[y * BACKGROUND_W + x];
            int screen_x = x << 1;
            top[screen_x] = color;
            top[screen_x + 1] = color;
            top[FRAME_STRIDE + screen_x] = color;
            top[FRAME_STRIDE + screen_x + 1] = color;
        }
    }
}

static char map_cell(LevelId level, int x, int z)
{
    int width = level == LEVEL_ZERO ? LEVEL0_W : POOL_W;
    int height = level == LEVEL_ZERO ? LEVEL0_H : POOL_H;
    if (x < 0 || z < 0 || x >= width || z >= height) {
        return '#';
    }
    return level == LEVEL_ZERO ? LEVEL0_MAP[z][x] : POOL_MAP[z][x];
}

static int map_width(LevelId level)
{
    return level == LEVEL_ZERO ? LEVEL0_W : POOL_W;
}

static int map_height(LevelId level)
{
    return level == LEVEL_ZERO ? LEVEL0_H : POOL_H;
}

static int is_walkable(LevelId level, int x, int z)
{
    char cell = map_cell(level, x, z);
    return cell == '.' || cell == 'W' || cell == 'E';
}

static int can_stand(LevelId level, float x, float z)
{
    return is_walkable(level, (int)floorf(x - PLAYER_RADIUS), (int)floorf(z - PLAYER_RADIUS)) &&
           is_walkable(level, (int)floorf(x + PLAYER_RADIUS), (int)floorf(z - PLAYER_RADIUS)) &&
           is_walkable(level, (int)floorf(x - PLAYER_RADIUS), (int)floorf(z + PLAYER_RADIUS)) &&
           is_walkable(level, (int)floorf(x + PLAYER_RADIUS), (int)floorf(z + PLAYER_RADIUS));
}

static void reset_player(LevelId level)
{
    g_level = level;
    if (level == LEVEL_ZERO) {
        g_player.x = LEVEL0_SPAWN_X;
        g_player.z = LEVEL0_SPAWN_Z;
        g_player.angle = 0.35f;
        g_player.eye_height = 0.50f;
    } else {
        g_player.x = POOL_SPAWN_X;
        g_player.z = POOL_SPAWN_Z;
        g_player.angle = 0.15f;
        g_player.eye_height = 0.50f;
    }
    g_path_timer = 0.0f;
    g_chase_target_mix = 0;
    log_message("INFO", "Entered %s at %.2f, %.2f", level == LEVEL_ZERO ? "Level 0" : "Poolrooms", g_player.x, g_player.z);
}

static int compute_distances(LevelId level, int start_x, int start_z)
{
    int head = 0;
    int tail = 0;
    int width = map_width(level);
    int height = map_height(level);
    int i;
    if (!is_walkable(level, start_x, start_z)) {
        return 0;
    }
    for (i = 0; i < MAX_MAP_CELLS; ++i) {
        g_path_distance[i] = -1;
    }
    g_path_distance[start_z * width + start_x] = 0;
    g_path_queue[tail++] = (int16_t)(start_z * width + start_x);
    while (head < tail) {
        int index = g_path_queue[head++];
        int x = index % width;
        int z = index / width;
        int current = g_path_distance[index];
        static const int dx[4] = {1, -1, 0, 0};
        static const int dz[4] = {0, 0, 1, -1};
        int direction;
        for (direction = 0; direction < 4; ++direction) {
            int nx = x + dx[direction];
            int nz = z + dz[direction];
            int next_index = nz * width + nx;
            if (nx >= 0 && nz >= 0 && nx < width && nz < height &&
                is_walkable(level, nx, nz) && g_path_distance[next_index] < 0) {
                g_path_distance[next_index] = (int16_t)(current + 1);
                g_path_queue[tail++] = (int16_t)next_index;
            }
        }
    }
    return tail;
}

static void respawn_nextbot_far(void)
{
    int count;
    int width = map_width(g_level);
    int height = map_height(g_level);
    int best_index = -1;
    int best_distance = -1;
    int best_score = 1000000;
    int i;
    count = compute_distances(g_level, (int)g_player.x, (int)g_player.z);
    if (count <= 0) {
        log_message("ERROR", "Cannot respawn nextbot: player cell is invalid");
        g_nextbot.enabled = 0;
        return;
    }
    for (i = 0; i < width * height; ++i) {
        int distance = g_path_distance[i];
        int score = abs(distance - 28);
        if (distance >= 20 && distance <= 36 && score < best_score) {
            best_score = score;
            best_distance = g_path_distance[i];
            best_index = i;
        }
    }
    if (best_index < 0 || best_distance < 20) {
        log_message("ERROR", "Cannot find a sufficiently distant nextbot spawn");
        g_nextbot.enabled = 0;
        return;
    }
    g_nextbot.x = (float)(best_index % width) + 0.5f;
    g_nextbot.z = (float)(best_index / width) + 0.5f;
    g_nextbot.target_x = g_nextbot.x;
    g_nextbot.target_z = g_nextbot.z;
    g_nextbot_path_distance = best_distance;
    g_chase_mix = 0;
    g_chase_target_mix = 0;
    log_message("INFO", "Nextbot respawned in %s at %.1f, %.1f; BFS distance=%d",
                g_level == LEVEL_ZERO ? "Level 0" : "Poolrooms", g_nextbot.x, g_nextbot.z, best_distance);
}

static int chase_mix_for_distance(int distance)
{
    float normalized;
    float mix;
    if (distance < 0 || distance >= 13) {
        return 0;
    }
    if (distance >= 8) {
        normalized = (13.0f - (float)distance) / 5.0f;
        mix = 0.08f * normalized * normalized;
    } else if (distance >= 4) {
        normalized = (8.0f - (float)distance) / 4.0f;
        mix = 0.08f + 0.42f * normalized * normalized;
    } else {
        normalized = (4.0f - (float)distance) / 4.0f;
        mix = 0.50f + 0.50f * normalized * normalized;
    }
    return (int)(mix * 256.0f + 0.5f);
}

static void update_nextbot_path(void)
{
    int bot_x;
    int bot_z;
    int best_x;
    int best_z;
    int best_distance;
    int width = map_width(g_level);
    int height = map_height(g_level);
    int direction;
    static const int dx[4] = {1, -1, 0, 0};
    static const int dz[4] = {0, 0, 1, -1};

    if (!g_nextbot.enabled || g_game_over) {
        g_chase_target_mix = 0;
        return;
    }
    compute_distances(g_level, (int)g_player.x, (int)g_player.z);
    bot_x = (int)g_nextbot.x;
    bot_z = (int)g_nextbot.z;
    best_x = bot_x;
    best_z = bot_z;
    best_distance = g_path_distance[bot_z * width + bot_x];
    g_nextbot_path_distance = best_distance;
    for (direction = 0; direction < 4; ++direction) {
        int nx = bot_x + dx[direction];
        int nz = bot_z + dz[direction];
        if (nx >= 0 && nz >= 0 && nx < width && nz < height) {
            int distance = g_path_distance[nz * width + nx];
            if (distance >= 0 && (best_distance < 0 || distance < best_distance)) {
                best_distance = distance;
                best_x = nx;
                best_z = nz;
            }
        }
    }
    g_nextbot.target_x = (float)best_x + 0.5f;
    g_nextbot.target_z = (float)best_z + 0.5f;
    g_chase_target_mix = chase_mix_for_distance(g_nextbot_path_distance);
}

static void update_chase_mix(void)
{
    if (g_chase_mix < g_chase_target_mix) {
        ++g_chase_mix;
    } else if (g_chase_mix > g_chase_target_mix) {
        g_chase_mix -= 3;
        if (g_chase_mix < g_chase_target_mix) {
            g_chase_mix = g_chase_target_mix;
        }
    }
    if (g_chase_mix < 0) {
        g_chase_mix = 0;
    } else if (g_chase_mix > 256) {
        g_chase_mix = 256;
    }
    if (g_chase_mix > 0 && !g_chase_was_audible) {
        g_chase_was_audible = 1;
        log_message("INFO", "Chase music fade-in started; nextbot BFS distance=%d", g_nextbot_path_distance);
    } else if (g_chase_mix == 0 && g_chase_was_audible && g_chase_target_mix == 0) {
        g_chase_was_audible = 0;
        log_message("INFO", "Chase music faded out");
    }
}

static void update_ambient_mix(void)
{
    int level0_target = g_level == LEVEL_ZERO ? 256 : 0;
    int poolrooms_target = g_level == LEVEL_POOLROOMS ? 256 : 0;
    const int fade_step = 3;

    if (g_ambient_level0_mix < level0_target) {
        g_ambient_level0_mix += fade_step;
        if (g_ambient_level0_mix > level0_target) g_ambient_level0_mix = level0_target;
    } else if (g_ambient_level0_mix > level0_target) {
        g_ambient_level0_mix -= fade_step;
        if (g_ambient_level0_mix < level0_target) g_ambient_level0_mix = level0_target;
    }
    if (g_ambient_poolrooms_mix < poolrooms_target) {
        g_ambient_poolrooms_mix += fade_step;
        if (g_ambient_poolrooms_mix > poolrooms_target) g_ambient_poolrooms_mix = poolrooms_target;
    } else if (g_ambient_poolrooms_mix > poolrooms_target) {
        g_ambient_poolrooms_mix -= fade_step;
        if (g_ambient_poolrooms_mix < poolrooms_target) g_ambient_poolrooms_mix = poolrooms_target;
    }
}

static void update_nextbot(float dt_seconds)
{
    float dx;
    float dz;
    float distance;
    float step;
    if (!g_nextbot.enabled || g_game_over) {
        return;
    }
    g_path_timer -= dt_seconds;
    if (g_path_timer <= 0.0f) {
        update_nextbot_path();
        g_path_timer = 0.20f;
    }
    dx = g_nextbot.target_x - g_nextbot.x;
    dz = g_nextbot.target_z - g_nextbot.z;
    distance = sqrtf(dx * dx + dz * dz);
    step = 1.15f * dt_seconds;
    if (distance > 0.001f) {
        if (step >= distance) {
            g_nextbot.x = g_nextbot.target_x;
            g_nextbot.z = g_nextbot.target_z;
            g_path_timer = 0.0f;
        } else {
            g_nextbot.x += dx * (step / distance);
            g_nextbot.z += dz * (step / distance);
        }
    }
    dx = g_nextbot.x - g_player.x;
    dz = g_nextbot.z - g_player.z;
    if (dx * dx + dz * dz < 0.36f * 0.36f) {
        g_game_over = 1;
        g_chase_target_mix = 0;
        log_message("INFO", "Game over: nextbot reached player");
    }
}

static void move_player(float forward, float turn, int sprint, float dt_seconds)
{
    float speed = sprint ? 3.15f : 1.75f;
    float next_x;
    float next_z;
    g_player.angle += turn * 2.20f * dt_seconds;
    while (g_player.angle > PI) {
        g_player.angle -= 2.0f * PI;
    }
    while (g_player.angle < -PI) {
        g_player.angle += 2.0f * PI;
    }
    next_x = g_player.x + cosf(g_player.angle) * forward * speed * dt_seconds;
    next_z = g_player.z + sinf(g_player.angle) * forward * speed * dt_seconds;
    if (can_stand(g_level, next_x, g_player.z)) {
        g_player.x = next_x;
    }
    if (can_stand(g_level, g_player.x, next_z)) {
        g_player.z = next_z;
    }
}

static void check_door(void)
{
    float dx;
    float dz;
    if (g_level != LEVEL_ZERO) {
        return;
    }
    dx = g_player.x - ((float)LEVEL0_DOOR_X + 0.5f);
    dz = g_player.z - ((float)LEVEL0_DOOR_Z + 0.5f);
    if (dx * dx + dz * dz < 0.82f * 0.82f) {
        reset_player(LEVEL_POOLROOMS);
        respawn_nextbot_far();
    }
}

#define SELECT_TEXTURE_MIP(symbol, lod) \
    ((lod) == 0 ? symbol##_PIXELS : ((lod) == 1 ? symbol##_MIP1_PIXELS : \
    ((lod) == 2 ? symbol##_MIP2_PIXELS : symbol##_MIP3_PIXELS)))

static const uint16_t *wall_texture(LevelId level, char cell, int map_x, int map_z, int lod)
{
    if (level == LEVEL_ZERO) {
        if (cell == 'D') {
            return SELECT_TEXTURE_MIP(TEXTURE_DOOR, lod);
        }
        if (((map_x * 13 + map_z * 7) & 15) == 0) {
            return SELECT_TEXTURE_MIP(TEXTURE_DIRTY_WALL, lod);
        }
        return SELECT_TEXTURE_MIP(TEXTURE_WALL, lod);
    }
    if (((map_x * 11 + map_z * 5) & 7) == 0) {
        return SELECT_TEXTURE_MIP(POOLS_DAMP_WALL, lod);
    }
    return SELECT_TEXTURE_MIP(POOLS_WALL, lod);
}

static const uint16_t *floor_texture(LevelId level, char cell, int lod)
{
    if (level == LEVEL_ZERO) {
        return SELECT_TEXTURE_MIP(TEXTURE_CARPET, lod);
    }
    if (cell == 'W') {
        return SELECT_TEXTURE_MIP(POOLS_WATER, lod);
    }
    if (cell == 'E') {
        return SELECT_TEXTURE_MIP(POOLS_EDGE, lod);
    }
    return SELECT_TEXTURE_MIP(POOLS_FLOOR, lod);
}

static const uint16_t *ceiling_texture(LevelId level, int map_x, int map_z, int lod)
{
    int is_light = ((map_x * 7 + map_z * 11) % 19) == 0;
    if (level == LEVEL_ZERO) {
        return is_light ? SELECT_TEXTURE_MIP(TEXTURE_LIGHT, lod) : SELECT_TEXTURE_MIP(TEXTURE_CEILING, lod);
    }
    return is_light ? SELECT_TEXTURE_MIP(POOLS_LIGHT, lod) : SELECT_TEXTURE_MIP(POOLS_CEILING, lod);
}

static void render_floor_and_ceiling(float dir_x, float dir_z, float plane_x, float plane_z)
{
    int y;
    float left_x = dir_x - plane_x;
    float left_z = dir_z - plane_z;
    float right_x = dir_x + plane_x;
    float right_z = dir_z + plane_z;
    for (y = BACKGROUND_H / 2; y < BACKGROUND_H; ++y) {
        float p = (float)y - (float)BACKGROUND_H * 0.5f + 0.5f;
        float floor_distance = (g_player.eye_height * (float)BACKGROUND_H) / p;
        float ceiling_distance = ((1.0f - g_player.eye_height) * (float)BACKGROUND_H) / p;
        float floor_step_x = floor_distance * (right_x - left_x) / (float)BACKGROUND_W;
        float floor_step_z = floor_distance * (right_z - left_z) / (float)BACKGROUND_W;
        float ceiling_step_x = ceiling_distance * (right_x - left_x) / (float)BACKGROUND_W;
        float ceiling_step_z = ceiling_distance * (right_z - left_z) / (float)BACKGROUND_W;
        float floor_x = g_player.x + floor_distance * left_x;
        float floor_z = g_player.z + floor_distance * left_z;
        float ceiling_x = g_player.x + ceiling_distance * left_x;
        float ceiling_z = g_player.z + ceiling_distance * left_z;
        int floor_lod = texture_lod(floor_distance);
        int ceiling_lod = texture_lod(ceiling_distance);
        int floor_size = 128 >> floor_lod;
        int ceiling_size = 128 >> ceiling_lod;
        int floor_mask = floor_size - 1;
        int ceiling_mask = ceiling_size - 1;
        int ceiling_y = BACKGROUND_H - 1 - y;
        int x;
        for (x = 0; x < BACKGROUND_W; ++x) {
            int floor_cell_x = (int)floorf(floor_x);
            int floor_cell_z = (int)floorf(floor_z);
            int ceiling_cell_x = (int)floorf(ceiling_x);
            int ceiling_cell_z = (int)floorf(ceiling_z);
            int floor_tex_x = ((int)(floor_x * (float)floor_size)) & floor_mask;
            int floor_tex_z = ((int)(floor_z * (float)floor_size)) & floor_mask;
            int ceiling_tex_x = ((int)(ceiling_x * (float)ceiling_size)) & ceiling_mask;
            int ceiling_tex_z = ((int)(ceiling_z * (float)ceiling_size)) & ceiling_mask;
            char floor_cell = map_cell(g_level, floor_cell_x, floor_cell_z);
            const uint16_t *floor_pixels = floor_texture(g_level, floor_cell, floor_lod);
            const uint16_t *ceiling_pixels = ceiling_texture(g_level, ceiling_cell_x, ceiling_cell_z, ceiling_lod);
            uint32_t floor_color = rgb565_to_8888(floor_pixels[floor_tex_z * floor_size + floor_tex_x]);
            uint32_t ceiling_color = rgb565_to_8888(ceiling_pixels[ceiling_tex_z * ceiling_size + ceiling_tex_x]);
            int floor_shade = level_brightness(252 - (int)fminf(floor_distance * 1.3f, 52.0f), 96);
            int ceiling_shade = level_brightness(256 - (int)fminf(ceiling_distance * 1.1f, 44.0f), 112);
            floor_color = blend_color(shade_color(floor_color, floor_shade), level_fog_color(), fog_amount(floor_distance));
            ceiling_color = blend_color(shade_color(ceiling_color, ceiling_shade), level_fog_color(), fog_amount(ceiling_distance));
            g_background[y * BACKGROUND_W + x] = floor_color;
            g_background[ceiling_y * BACKGROUND_W + x] = ceiling_color;
            floor_x += floor_step_x;
            floor_z += floor_step_z;
            ceiling_x += ceiling_step_x;
            ceiling_z += ceiling_step_z;
        }
    }
}

static void render_walls(float dir_x, float dir_z, float plane_x, float plane_z)
{
    int screen_x;
    for (screen_x = 0; screen_x < SCREEN_W; ++screen_x) {
        float camera_x = 2.0f * (float)screen_x / (float)SCREEN_W - 1.0f;
        float ray_x = dir_x + plane_x * camera_x;
        float ray_z = dir_z + plane_z * camera_x;
        int map_x = (int)g_player.x;
        int map_z = (int)g_player.z;
        float delta_x = ray_x == 0.0f ? 1.0e30f : fabsf(1.0f / ray_x);
        float delta_z = ray_z == 0.0f ? 1.0e30f : fabsf(1.0f / ray_z);
        float side_x;
        float side_z;
        int step_x;
        int step_z;
        int side = 0;
        char hit_cell = '#';
        int hit = 0;
        int safety = 0;
        float perpendicular;
        float wall_position;
        int texture_x;
        int raw_start;
        int raw_end;
        int draw_start;
        int draw_end;
        int line_height;
        int lod;
        int texture_size;
        int texture_mask;
        const uint16_t *texture;
        int y;

        if (ray_x < 0.0f) {
            step_x = -1;
            side_x = (g_player.x - (float)map_x) * delta_x;
        } else {
            step_x = 1;
            side_x = ((float)map_x + 1.0f - g_player.x) * delta_x;
        }
        if (ray_z < 0.0f) {
            step_z = -1;
            side_z = (g_player.z - (float)map_z) * delta_z;
        } else {
            step_z = 1;
            side_z = ((float)map_z + 1.0f - g_player.z) * delta_z;
        }
        while (!hit && safety++ < 128) {
            if (side_x < side_z) {
                side_x += delta_x;
                map_x += step_x;
                side = 0;
            } else {
                side_z += delta_z;
                map_z += step_z;
                side = 1;
            }
            hit_cell = map_cell(g_level, map_x, map_z);
            hit = !is_walkable(g_level, map_x, map_z);
        }
        if (!hit) {
            g_zbuffer[screen_x] = 1000.0f;
            continue;
        }
        if (side == 0) {
            perpendicular = ((float)map_x - g_player.x + (float)(1 - step_x) * 0.5f) / ray_x;
            wall_position = g_player.z + perpendicular * ray_z;
        } else {
            perpendicular = ((float)map_z - g_player.z + (float)(1 - step_z) * 0.5f) / ray_z;
            wall_position = g_player.x + perpendicular * ray_x;
        }
        if (perpendicular < 0.02f) {
            perpendicular = 0.02f;
        }
        g_zbuffer[screen_x] = perpendicular;
        line_height = (int)((float)WORLD_H / perpendicular);
        if (line_height < 1) {
            line_height = 1;
        }
        raw_start = -line_height / 2 + WORLD_H / 2;
        raw_end = line_height / 2 + WORLD_H / 2;
        draw_start = raw_start < 0 ? 0 : raw_start;
        draw_end = raw_end >= WORLD_H ? WORLD_H - 1 : raw_end;
        lod = texture_lod(perpendicular);
        texture_size = 128 >> lod;
        texture_mask = texture_size - 1;
        wall_position -= floorf(wall_position);
        texture_x = (int)(wall_position * (float)texture_size) & texture_mask;
        if ((side == 0 && ray_x > 0.0f) || (side == 1 && ray_z < 0.0f)) {
            texture_x = texture_mask - texture_x;
        }
        texture = wall_texture(g_level, hit_cell, map_x, map_z, lod);
        for (y = draw_start; y <= draw_end; ++y) {
            int texture_y = ((y - raw_start) * texture_size) / line_height;
            int shade = level_brightness(256 - (side ? 22 : 0) - (int)fminf(perpendicular * 2.0f, 48.0f), 100);
            uint32_t color = rgb565_to_8888(texture[(texture_y & texture_mask) * texture_size + texture_x]);
            color = blend_color(shade_color(color, shade), level_fog_color(), fog_amount(perpendicular));
            put_world_pixel(screen_x, y, color);
        }
    }
}

static void render_nextbot(float dir_x, float dir_z, float plane_x, float plane_z)
{
    float relative_x;
    float relative_z;
    float determinant;
    float inverse;
    float transform_x;
    float transform_y;
    int sprite_screen_x;
    int sprite_height;
    int sprite_width;
    int start_y;
    int end_y;
    int start_x;
    int end_x;
    int stripe;
    if (!g_nextbot.enabled) {
        return;
    }
    relative_x = g_nextbot.x - g_player.x;
    relative_z = g_nextbot.z - g_player.z;
    determinant = plane_x * dir_z - dir_x * plane_z;
    if (fabsf(determinant) < 0.0001f) {
        return;
    }
    inverse = 1.0f / determinant;
    transform_x = inverse * (dir_z * relative_x - dir_x * relative_z);
    transform_y = inverse * (-plane_z * relative_x + plane_x * relative_z);
    if (transform_y <= 0.05f) {
        return;
    }
    sprite_screen_x = (int)((float)SCREEN_W * 0.5f * (1.0f + transform_x / transform_y));
    sprite_height = abs((int)((float)WORLD_H / transform_y));
    sprite_width = sprite_height;
    start_y = -sprite_height / 2 + WORLD_H / 2;
    end_y = sprite_height / 2 + WORLD_H / 2;
    start_x = -sprite_width / 2 + sprite_screen_x;
    end_x = sprite_width / 2 + sprite_screen_x;
    if (start_y < 0) start_y = 0;
    if (end_y >= WORLD_H) end_y = WORLD_H - 1;
    if (start_x < 0) start_x = 0;
    if (end_x >= SCREEN_W) end_x = SCREEN_W - 1;
    for (stripe = start_x; stripe <= end_x; ++stripe) {
        int original_start = -sprite_width / 2 + sprite_screen_x;
        int texture_x = ((stripe - original_start) * 128) / (sprite_width > 0 ? sprite_width : 1);
        int y;
        if (transform_y >= g_zbuffer[stripe] || texture_x < 0 || texture_x >= 128) {
            continue;
        }
        for (y = start_y; y <= end_y; ++y) {
            int original_y = -sprite_height / 2 + WORLD_H / 2;
            int texture_y = ((y - original_y) * 128) / (sprite_height > 0 ? sprite_height : 1);
            int index;
            uint32_t color;
            if (texture_y < 0 || texture_y >= 128) {
                continue;
            }
            index = texture_y * 128 + texture_x;
            if (NEXTBOT_SPRITE_ALPHA[index] < 32) {
                continue;
            }
            color = rgb565_to_8888(NEXTBOT_SPRITE_PIXELS[index]);
            put_world_pixel(stripe, y, shade_color(color, g_level == LEVEL_POOLROOMS ? 196 : 246));
        }
    }
}

static uint8_t glyph_row(char character, int row)
{
    static const uint8_t A[7] = {14, 17, 17, 31, 17, 17, 17};
    static const uint8_t E[7] = {31, 16, 16, 30, 16, 16, 31};
    static const uint8_t G[7] = {14, 17, 16, 23, 17, 17, 14};
    static const uint8_t M[7] = {17, 27, 21, 21, 17, 17, 17};
    static const uint8_t O[7] = {14, 17, 17, 17, 17, 17, 14};
    static const uint8_t P[7] = {30, 17, 17, 30, 16, 16, 16};
    static const uint8_t R[7] = {30, 17, 17, 30, 20, 18, 17};
    static const uint8_t S[7] = {15, 16, 16, 14, 1, 1, 30};
    static const uint8_t T[7] = {31, 4, 4, 4, 4, 4, 4};
    static const uint8_t V[7] = {17, 17, 17, 17, 17, 10, 4};
    static const uint8_t X[7] = {17, 17, 10, 4, 10, 17, 17};
    const uint8_t *glyph = NULL;
    if (character >= 'a' && character <= 'z') {
        character = (char)(character - 'a' + 'A');
    }
    switch (character) {
        case 'A': glyph = A; break;
        case 'E': glyph = E; break;
        case 'G': glyph = G; break;
        case 'M': glyph = M; break;
        case 'O': glyph = O; break;
        case 'P': glyph = P; break;
        case 'R': glyph = R; break;
        case 'S': glyph = S; break;
        case 'T': glyph = T; break;
        case 'V': glyph = V; break;
        case 'X': glyph = X; break;
        default: return 0;
    }
    return glyph[row];
}

static void draw_text(const char *text, int x, int y, int scale, uint32_t color)
{
    int origin_x = x;
    while (*text != '\0') {
        int row;
        if (*text == '\n') {
            x = origin_x;
            y += 8 * scale;
            ++text;
            continue;
        }
        for (row = 0; row < 7; ++row) {
            uint8_t bits = glyph_row(*text, row);
            int column;
            for (column = 0; column < 5; ++column) {
                if ((bits & (1U << (4 - column))) != 0) {
                    int sy;
                    for (sy = 0; sy < scale; ++sy) {
                        int sx;
                        uint32_t *destination = g_back_fb + (y + row * scale + sy) * FRAME_STRIDE + x + column * scale;
                        for (sx = 0; sx < scale; ++sx) {
                            destination[sx] = color;
                        }
                    }
                }
            }
        }
        x += 6 * scale;
        ++text;
    }
}

static void render_game_over(void)
{
    int y;
    for (y = 0; y < SCREEN_H; ++y) {
        int x;
        uint32_t *row = g_back_fb + y * FRAME_STRIDE;
        for (x = 0; x < SCREEN_W; ++x) {
            row[x] = 0xFF08080CU;
        }
    }
    draw_text("GAME OVER", 132, 89, 4, 0xFFFFFFFFU);
    draw_text("Press X to restart", 129, 165, 2, 0xFFB8B8C8U);
}

static void render_frame(void)
{
    float dir_x = cosf(g_player.angle);
    float dir_z = sinf(g_player.angle);
    float plane_x = -dir_z * FOV_PLANE;
    float plane_z = dir_x * FOV_PLANE;
    if (g_game_over) {
        render_game_over();
        return;
    }
    render_floor_and_ceiling(dir_x, dir_z, plane_x, plane_z);
    present_background();
    render_walls(dir_x, dir_z, plane_x, plane_z);
    render_nextbot(dir_x, dir_z, plane_x, plane_z);
}

static int load_pcm_audio(const char *relative_path, const char *psp_path, const char *label,
                          int16_t **samples, int *sample_count)
{
    const char *paths[] = {relative_path, psp_path};
    FILE *file = NULL;
    long size;
    size_t bytes_read;
    int path_index;
    for (path_index = 0; path_index < 2 && file == NULL; ++path_index) {
        file = fopen(paths[path_index], "rb");
    }
    if (file == NULL) {
        log_message("WARN", "%s unavailable; this audio layer is disabled", label);
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) <= 0 || size > MAX_AUDIO_BYTES || (size & 1) != 0 || fseek(file, 0, SEEK_SET) != 0) {
        log_message("WARN", "Invalid %s size; this audio layer is disabled", label);
        fclose(file);
        return 0;
    }
    *samples = (int16_t *)memalign(64, (size_t)size);
    if (*samples == NULL) {
        log_message("WARN", "Not enough memory for %s; this audio layer is disabled", label);
        fclose(file);
        return 0;
    }
    bytes_read = fread(*samples, 1, (size_t)size, file);
    fclose(file);
    if (bytes_read != (size_t)size) {
        log_message("WARN", "Short read while loading %s; this audio layer is disabled", label);
        free(*samples);
        *samples = NULL;
        return 0;
    }
    *sample_count = (int)(size / 2);
    log_message("INFO", "Loaded %s: %ld bytes, %d Hz mono PCM", label, size, AUDIO_RATE);
    return 1;
}

static int load_audio_assets(void)
{
    int loaded = 0;
    loaded += load_pcm_audio("assets/chase.raw", "ms0:/PSP/GAME/BACKROOMS3D/assets/chase.raw", "chase.raw",
                             &g_chase_samples, &g_chase_sample_count);
    loaded += load_pcm_audio("assets/ambient_level0.raw", "ms0:/PSP/GAME/BACKROOMS3D/assets/ambient_level0.raw", "ambient_level0.raw",
                             &g_ambient_level0_samples, &g_ambient_level0_sample_count);
    loaded += load_pcm_audio("assets/ambient_poolrooms.raw", "ms0:/PSP/GAME/BACKROOMS3D/assets/ambient_poolrooms.raw", "ambient_poolrooms.raw",
                             &g_ambient_poolrooms_samples, &g_ambient_poolrooms_sample_count);
    if (loaded == 0) {
        log_message("WARN", "No audio assets available; game will continue silently");
    }
    return loaded > 0;
}

static int16_t clamp_audio_sample(int value)
{
    if (value < -32768) return -32768;
    if (value > 32767) return 32767;
    return (int16_t)value;
}

static int audio_thread(SceSize args, void *argp)
{
    int channel;
    int chase_index = 0;
    int level0_index = 0;
    int poolrooms_index = 0;
    int phase = 0;
    (void)args;
    (void)argp;
    channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, AUDIO_FRAMES, PSP_AUDIO_FORMAT_MONO);
    if (channel < 0) {
        log_message("WARN", "sceAudioChReserve failed (%d); audio disabled", channel);
        g_audio_running = 0;
        return 0;
    }
    log_message("INFO", "PSP audio channel reserved; ambient and chase layers use 11025 Hz with 4x upsampling");
    while (g_audio_running && g_running) {
        int i;
        int output_result;
        int chase_mix = g_chase_mix;
        int ambient_duck = 256 - (chase_mix * 3) / 4;
        int level0_mix = (g_ambient_level0_mix * ambient_duck) / 256;
        int poolrooms_mix = (g_ambient_poolrooms_mix * ambient_duck) / 256;
        for (i = 0; i < AUDIO_FRAMES; ++i) {
            int mixed = 0;
            if (g_chase_samples != NULL) {
                mixed += ((int)g_chase_samples[chase_index] * chase_mix) / 256;
            }
            if (g_ambient_level0_samples != NULL) {
                mixed += ((int)g_ambient_level0_samples[level0_index] * level0_mix) / 256;
            }
            if (g_ambient_poolrooms_samples != NULL) {
                mixed += ((int)g_ambient_poolrooms_samples[poolrooms_index] * poolrooms_mix) / 256;
            }
            g_audio_buffer[i] = clamp_audio_sample(mixed);
            if (++phase >= AUDIO_UPSAMPLE) {
                phase = 0;
                if (g_chase_sample_count > 0 && ++chase_index >= g_chase_sample_count) chase_index = 0;
                if (g_ambient_level0_sample_count > 0 && ++level0_index >= g_ambient_level0_sample_count) level0_index = 0;
                if (g_ambient_poolrooms_sample_count > 0 && ++poolrooms_index >= g_ambient_poolrooms_sample_count) poolrooms_index = 0;
            }
        }
        output_result = sceAudioOutputBlocking(channel, PSP_AUDIO_VOLUME_MAX, g_audio_buffer);
        if (output_result < 0) {
            log_message("WARN", "sceAudioOutputBlocking failed (%d); audio thread stopped safely", output_result);
            break;
        }
    }
    sceAudioChRelease(channel);
    return 0;
}

static void start_audio(void)
{
    int result;
    if (!load_audio_assets()) {
        return;
    }
    g_audio_running = 1;
    g_audio_thread = sceKernelCreateThread("chase_audio", audio_thread, 0x18, 0x4000, 0, NULL);
    if (g_audio_thread < 0) {
        log_message("WARN", "Cannot create audio thread (%d); audio disabled", g_audio_thread);
        g_audio_running = 0;
        free(g_chase_samples);
        g_chase_samples = NULL;
        free(g_ambient_level0_samples);
        g_ambient_level0_samples = NULL;
        free(g_ambient_poolrooms_samples);
        g_ambient_poolrooms_samples = NULL;
        return;
    }
    result = sceKernelStartThread(g_audio_thread, 0, NULL);
    if (result < 0) {
        log_message("WARN", "Cannot start audio thread (%d); audio disabled", result);
        g_audio_running = 0;
        sceKernelDeleteThread(g_audio_thread);
        g_audio_thread = -1;
        free(g_chase_samples);
        g_chase_samples = NULL;
        free(g_ambient_level0_samples);
        g_ambient_level0_samples = NULL;
        free(g_ambient_poolrooms_samples);
        g_ambient_poolrooms_samples = NULL;
    }
}

static void stop_audio(void)
{
    g_audio_running = 0;
    if (g_audio_thread >= 0) {
        sceKernelWaitThreadEnd(g_audio_thread, NULL);
        sceKernelDeleteThread(g_audio_thread);
        g_audio_thread = -1;
    }
    free(g_chase_samples);
    g_chase_samples = NULL;
    g_chase_sample_count = 0;
    free(g_ambient_level0_samples);
    g_ambient_level0_samples = NULL;
    g_ambient_level0_sample_count = 0;
    free(g_ambient_poolrooms_samples);
    g_ambient_poolrooms_samples = NULL;
    g_ambient_poolrooms_sample_count = 0;
}

static void restart_game(void)
{
    LevelId restart_level = g_level;
    g_game_over = 0;
    reset_player(restart_level);
    g_nextbot.enabled = 1;
    respawn_nextbot_far();
}

static void handle_input(const SceCtrlData *pad, float dt_seconds)
{
    uint32_t buttons = pad->Buttons;
    uint32_t pressed = buttons & ~g_previous_buttons;
    int analog_x = (int)pad->Lx - 128;
    int analog_y = (int)pad->Ly - 128;
    float turn = 0.0f;
    float forward = 0.0f;
    int left_trigger = (buttons & PSP_CTRL_LTRIGGER) != 0;
    int right_trigger = (buttons & PSP_CTRL_RTRIGGER) != 0;

    if (g_game_over) {
        if ((pressed & PSP_CTRL_CROSS) != 0) {
            restart_game();
        }
        g_previous_buttons = buttons;
        return;
    }
    if (abs(analog_x) > 24) {
        turn = (float)analog_x / 127.0f;
    } else if ((buttons & PSP_CTRL_LEFT) != 0) {
        turn = -1.0f;
    } else if ((buttons & PSP_CTRL_RIGHT) != 0) {
        turn = 1.0f;
    }
    if (abs(analog_y) > 24) {
        forward = -(float)analog_y / 127.0f;
    } else if ((buttons & PSP_CTRL_UP) != 0) {
        forward = 1.0f;
    } else if ((buttons & PSP_CTRL_DOWN) != 0) {
        forward = -1.0f;
    }
    if (!g_input_logged && (forward != 0.0f || turn != 0.0f)) {
        g_input_logged = 1;
        log_message("INFO", "Movement input accepted: analog=(%d,%d), dpad=0x%X",
                    analog_x, analog_y, (unsigned int)(buttons & (PSP_CTRL_UP | PSP_CTRL_DOWN | PSP_CTRL_LEFT | PSP_CTRL_RIGHT)));
    }
    move_player(forward, turn, (buttons & PSP_CTRL_CROSS) != 0, dt_seconds);

    if (left_trigger && right_trigger && (pressed & PSP_CTRL_TRIANGLE) != 0) {
        reset_player(LEVEL_POOLROOMS);
        respawn_nextbot_far();
    }
    if (left_trigger && right_trigger && (pressed & PSP_CTRL_CIRCLE) != 0) {
        reset_player(LEVEL_ZERO);
        respawn_nextbot_far();
    }
    if ((pressed & PSP_CTRL_SQUARE) != 0) {
        if (left_trigger && right_trigger) {
            g_nextbot.enabled = !g_nextbot.enabled;
            g_chase_target_mix = 0;
            log_message("INFO", "Nextbot %s", g_nextbot.enabled ? "enabled" : "disabled");
            if (g_nextbot.enabled) {
                respawn_nextbot_far();
            }
        } else if (left_trigger) {
            respawn_nextbot_far();
        }
    }
    g_previous_buttons = buttons;
}

int main(int argc, char *argv[])
{
    const size_t framebuffer_bytes = FRAME_STRIDE * SCREEN_H * sizeof(uint32_t);
    uint64_t previous_time;
    int result;
    (void)argc;
    (void)argv;

    open_session_log();
    result = setup_callbacks();
    if (result < 0) {
        log_message("FATAL", "Unable to register PSP exit callback (%d)", result);
        if (g_log_file != NULL) fclose(g_log_file);
        sceKernelExitGame();
        return 1;
    }
    log_message("INFO", "Backrooms PSP starting");
    if (sceGeEdramGetAddr() == NULL || sceGeEdramGetSize() < (int)(framebuffer_bytes * 2U)) {
        log_message("FATAL", "Not enough EDRAM for double framebuffers (%u bytes each)", (unsigned int)framebuffer_bytes);
        if (g_log_file != NULL) fclose(g_log_file);
        sceKernelExitGame();
        return 1;
    }
    /* The 0x40000000 alias is uncached, avoiding stale CPU writes in EDRAM. */
    g_front_fb = (uint32_t *)((uintptr_t)sceGeEdramGetAddr() | 0x40000000U);
    g_back_fb = (uint32_t *)((uintptr_t)g_front_fb + framebuffer_bytes);
    memset(g_front_fb, 0, framebuffer_bytes);
    memset(g_back_fb, 0, framebuffer_bytes);
    sceKernelDcacheWritebackAll();
    sceDisplaySetMode(0, SCREEN_W, SCREEN_H);
    result = sceDisplaySetFrameBuf(g_front_fb, FRAME_STRIDE, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_IMMEDIATE);
    if (result < 0) {
        log_message("FATAL", "sceDisplaySetFrameBuf failed (%d)", result);
        if (g_log_file != NULL) fclose(g_log_file);
        sceKernelExitGame();
        return 1;
    }
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    reset_player(LEVEL_ZERO);
    g_nextbot.enabled = 1;
    g_nextbot.x = NEXTBOT_SPAWN_X;
    g_nextbot.z = NEXTBOT_SPAWN_Z;
    g_nextbot.target_x = g_nextbot.x;
    g_nextbot.target_z = g_nextbot.z;
    log_message("INFO", "Verified door=(%d,%d), distance=%d; nextbot initial distance=%d",
                LEVEL0_DOOR_X, LEVEL0_DOOR_Z, LEVEL0_DOOR_DISTANCE, NEXTBOT_INITIAL_DISTANCE);
    start_audio();
    previous_time = sceKernelGetSystemTimeWide();

    while (g_running) {
        SceCtrlData pad;
        uint64_t now = sceKernelGetSystemTimeWide();
        float dt_seconds = (float)(now - previous_time) / 1000000.0f;
        float target_eye;
        uint32_t *temporary;
        previous_time = now;
        if (dt_seconds < 0.001f) dt_seconds = 0.001f;
        if (dt_seconds > 0.05f) dt_seconds = 0.05f;
        sceCtrlPeekBufferPositive(&pad, 1);
        handle_input(&pad, dt_seconds);
        check_door();
        target_eye = (g_level == LEVEL_POOLROOMS && map_cell(g_level, (int)g_player.x, (int)g_player.z) == 'W') ? 0.42f : 0.50f;
        g_player.eye_height += (target_eye - g_player.eye_height) * fminf(dt_seconds * 4.0f, 1.0f);
        update_nextbot(dt_seconds);
        update_chase_mix();
        update_ambient_mix();
        render_frame();
        sceKernelDcacheWritebackRange(g_back_fb, framebuffer_bytes);
        result = sceDisplaySetFrameBuf(g_back_fb, FRAME_STRIDE, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
        if (result < 0) {
            log_message("ERROR", "Framebuffer swap failed (%d); exiting safely", result);
            g_running = 0;
            break;
        }
        sceDisplayWaitVblankStart();
        temporary = g_front_fb;
        g_front_fb = g_back_fb;
        g_back_fb = temporary;
    }

    stop_audio();
    log_message("INFO", "Backrooms PSP shut down cleanly");
    if (g_log_file != NULL) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    sceKernelExitGame();
    return 0;
}
