#include "world.h"
#include <math.h>
#include <string.h>

float vec_distance(Vec2 a, Vec2 b) {
    float x = a.x - b.x, z = a.z - b.z;
    return sqrtf(x*x + z*z);
}

Tile world_tile(const World *w, int x, int z) {
    if (x < 0 || z < 0 || x >= WORLD_SIZE || z >= WORLD_SIZE) return TILE_WALL;
    return (Tile)w->tiles[z][x];
}

static void carve(World *w, int x, int z, int width, int depth, Tile type) {
    for (int j = z; j < z + depth; ++j)
        for (int i = x; i < x + width; ++i)
            if (i > 0 && j > 0 && i < WORLD_SIZE-1 && j < WORLD_SIZE-1)
                w->tiles[j][i] = (uint8_t)type;
}

void world_build(World *w, int level) {
    memset(w, 0, sizeof(*w));
    w->level = level == 1 ? 1 : 0;
    int rooms = w->level ? 3 : 4;
    int width = w->level ? 7 : 5;
    int stride = width + 2;
    for (int rz = 0; rz < rooms; ++rz) {
        for (int rx = 0; rx < rooms; ++rx) {
            int x = 2 + rx*stride, z = 2 + rz*stride;
            carve(w, x, z, width, width, TILE_FLOOR);
            if (w->level) {
                carve(w, x+2, z+2, 3, 3, TILE_WATER);
                if ((rx+rz)%2) carve(w, x+3, z+3, 1, 1, TILE_WALL);
            } else if ((rx+rz)%3 == 1) {
                carve(w, x+2, z+2, 1, 1, TILE_WALL);
            }
            /* Every row is connected. Alternating vertical links form loops
               and long sight lines while leaving no unreachable rooms. */
            if (rx < rooms-1) carve(w, x+width, z+width/2, 2, 2, TILE_FLOOR);
            if (rz < rooms-1 && (rx == (rz%2 ? 0 : rooms-1) || rx == 1))
                carve(w, x+width/2, z+width, w->level ? 3 : 2, 2, TILE_FLOOR);
        }
    }
    w->spawn = (Vec2){3.5f, 3.5f};
    w->enemy_spawn = (Vec2){2.5f, 2.5f + (rooms-1)*stride};
    w->relays[0] = (Vec2){3.5f + (rooms-1)*stride, 2.5f};
    w->relays[1] = (Vec2){2.5f, 3.5f + (rooms-1)*stride};
    w->relays[2] = (Vec2){3.5f + stride, 2.5f + (rooms-2)*stride};
    w->exit = (Vec2){1.5f + (rooms-1)*stride + width, 2.5f + (rooms-1)*stride};
}

bool world_fits(const World *w, Vec2 p, float radius) {
    if (!isfinite(p.x) || !isfinite(p.z) || !isfinite(radius) || radius < 0 ||
        radius > 1 || p.x < 0 || p.z < 0 || p.x >= WORLD_SIZE || p.z >= WORLD_SIZE)
        return false;
    for (int z = (int)floorf(p.z-radius); z <= (int)floorf(p.z+radius); ++z) {
        for (int x = (int)floorf(p.x-radius); x <= (int)floorf(p.x+radius); ++x) {
            if (world_tile(w, x, z) != TILE_WALL) continue;
            float nx = fmaxf((float)x, fminf(p.x, x+1.0f));
            float nz = fmaxf((float)z, fminf(p.z, z+1.0f));
            float dx = p.x-nx, dz = p.z-nz;
            if (dx*dx + dz*dz <= radius*radius) return false;
        }
    }
    return true;
}

void world_move(const World *w, Vec2 *p, Vec2 delta, float radius) {
    if (!isfinite(delta.x) || !isfinite(delta.z)) return;
    float length = sqrtf(delta.x*delta.x + delta.z*delta.z);
    if (length > 8.0f) return; /* Reject corrupted input, never teleport. */
    int steps = (int)ceilf(length/0.08f);
    if (steps < 1) return;
    delta.x /= steps; delta.z /= steps;
    for (int i = 0; i < steps; ++i) {
        Vec2 next = {p->x+delta.x, p->z};
        if (world_fits(w, next, radius)) p->x = next.x;
        next = (Vec2){p->x, p->z+delta.z};
        if (world_fits(w, next, radius)) p->z = next.z;
    }
}

void world_distances(const World *w, Vec2 target, int16_t out[WORLD_CELLS]) {
    for (int i = 0; i < WORLD_CELLS; ++i) out[i] = -1;
    if (!world_fits(w, target, 0)) return;
    int queue[WORLD_CELLS], read = 0, write = 0;
    int start = (int)target.z*WORLD_SIZE + (int)target.x;
    queue[write++] = start; out[start] = 0;
    static const int dx[] = {1,-1,0,0}, dz[] = {0,0,1,-1};
    while (read < write) {
        int cell = queue[read++], x = cell%WORLD_SIZE, z = cell/WORLD_SIZE;
        for (int d = 0; d < 4; ++d) {
            int nx = x+dx[d], nz = z+dz[d], n = nz*WORLD_SIZE+nx;
            if (world_tile(w, nx, nz) == TILE_WALL) continue;
            if (out[n] < 0) { out[n] = out[cell]+1; queue[write++] = n; }
        }
    }
}

bool world_visible(const World *w, Vec2 a, Vec2 b) {
    if (!world_fits(w, a, 0) || !world_fits(w, b, 0)) return false;
    int steps = (int)ceilf(vec_distance(a,b)*16.0f);
    for (int i = 1; i < steps; ++i) {
        float t = (float)i/steps;
        if (world_tile(w, (int)(a.x+(b.x-a.x)*t), (int)(a.z+(b.z-a.z)*t)) == TILE_WALL)
            return false;
    }
    return true;
}
