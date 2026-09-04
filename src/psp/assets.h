#ifndef BR_ASSETS_H
#define BR_ASSETS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MAT_WALL, MAT_DIRTY, MAT_CARPET, MAT_CEILING, MAT_LIGHT, MAT_DOOR,
    MAT_POOL_WALL, MAT_POOL_DAMP, MAT_POOL_FLOOR, MAT_POOL_CEILING,
    MAT_POOL_LIGHT, MAT_WATER, MAT_EDGE, MAT_BOT, MAT_COUNT
} Material;

bool assets_init(void);
void assets_bind(Material material);
void assets_shutdown(void);

#endif
