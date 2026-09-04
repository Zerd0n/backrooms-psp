#ifndef BR_WORLD_H
#define BR_WORLD_H

#include <stdbool.h>
#include <stdint.h>

#define WORLD_SIZE 31
#define WORLD_CELLS (WORLD_SIZE * WORLD_SIZE)
#define RELAY_COUNT 3

typedef struct { float x, z; } Vec2;
typedef enum { TILE_WALL, TILE_FLOOR, TILE_WATER } Tile;
typedef struct {
    uint8_t tiles[WORLD_SIZE][WORLD_SIZE];
    Vec2 spawn, enemy_spawn, exit, relays[RELAY_COUNT];
    int level;
} World;

void world_build(World *world, int level);
Tile world_tile(const World *world, int x, int z);
bool world_fits(const World *world, Vec2 position, float radius);
void world_move(const World *world, Vec2 *position, Vec2 delta, float radius);
void world_distances(const World *world, Vec2 target, int16_t out[WORLD_CELLS]);
bool world_visible(const World *world, Vec2 a, Vec2 b);
float vec_distance(Vec2 a, Vec2 b);

#endif
