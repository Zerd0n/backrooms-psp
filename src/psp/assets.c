#include "assets.h"
#include <pspgu.h>
#include <pspkernel.h>
#include <malloc.h>
#include <string.h>
#include "../generated/texture_wall.h"
#include "../generated/texture_dirty_wall.h"
#include "../generated/texture_carpet.h"
#include "../generated/texture_ceiling.h"
#include "../generated/texture_light.h"
#include "../generated/texture_door.h"
#include "../generated/pools_wall.h"
#include "../generated/pools_damp_wall.h"
#include "../generated/pools_floor.h"
#include "../generated/pools_ceiling.h"
#include "../generated/pools_light.h"
#include "../generated/pools_water.h"
#include "../generated/pools_edge.h"
#include "../generated/nextbot_sprite.h"

#define LEVELS(name) {name##_PIXELS,name##_MIP1_PIXELS,name##_MIP2_PIXELS,name##_MIP3_PIXELS}
static const uint16_t *const sources[MAT_BOT][4] = {
    LEVELS(TEXTURE_WALL), LEVELS(TEXTURE_DIRTY_WALL), LEVELS(TEXTURE_CARPET),
    LEVELS(TEXTURE_CEILING), LEVELS(TEXTURE_LIGHT), LEVELS(TEXTURE_DOOR),
    LEVELS(POOLS_WALL), LEVELS(POOLS_DAMP_WALL), LEVELS(POOLS_FLOOR),
    LEVELS(POOLS_CEILING), LEVELS(POOLS_LIGHT), LEVELS(POOLS_WATER), LEVELS(POOLS_EDGE)
};
static void *textures[MAT_COUNT][4];

static void swizzle(uint8_t *dst, const uint8_t *src, int row_bytes, int height) {
    for (int by=0; by<height; by+=8)
        for (int bx=0; bx<row_bytes; bx+=16)
            for (int y=0; y<8; ++y) {
                memcpy(dst,src+(by+y)*row_bytes+bx,16); dst+=16;
            }
}

bool assets_init(void) {
    uint16_t *scratch=memalign(16,128*128*2);
    if (!scratch) return false;
    for (int m=0; m<MAT_BOT; ++m) {
        for (int level=0; level<4; ++level) {
            int size=128>>level, bytes=size*size*2;
            textures[m][level]=memalign(16,bytes);
            if (!textures[m][level]) { free(scratch); assets_shutdown(); return false; }
            for (int i=0; i<size*size; ++i) {
                uint16_t c=sources[m][level][i];
                /* Existing assets use RGB565; PSP puts red in the low bits.
                   This changes storage order only, never the artwork. */
                scratch[i]=(uint16_t)((c>>11)|(c&0x07e0)|((c&31)<<11));
            }
            swizzle(textures[m][level],(uint8_t*)scratch,size*2,size);
        }
    }
    free(scratch);
    uint32_t *sprite=memalign(16,128*128*4);
    textures[MAT_BOT][0]=memalign(16,128*128*4);
    if (!sprite || !textures[MAT_BOT][0]) { free(sprite); assets_shutdown(); return false; }
    for (int i=0; i<128*128; ++i) {
        uint16_t c=NEXTBOT_SPRITE_PIXELS[i];
        uint32_t r=(c>>11)*255/31, g=((c>>5)&63)*255/63, b=(c&31)*255/31;
        sprite[i]=r|(g<<8)|(b<<16)|((uint32_t)NEXTBOT_SPRITE_ALPHA[i]<<24);
    }
    swizzle(textures[MAT_BOT][0],(uint8_t*)sprite,128*4,128);
    free(sprite);
    sceKernelDcacheWritebackAll();
    return true;
}

void assets_bind(Material m) {
    int max_level=m==MAT_BOT ? 0 : 3;
    sceGuTexMode(m==MAT_BOT ? GU_PSM_8888 : GU_PSM_5650,max_level,0,1);
    for (int level=0; level<=max_level; ++level) {
        int size=128>>level;
        sceGuTexImage(level,size,size,size,textures[m][level]);
    }
    sceGuTexFilter(max_level ? GU_LINEAR_MIPMAP_LINEAR : GU_LINEAR,GU_LINEAR);
    sceGuTexLevelMode(GU_TEXTURE_AUTO,0);
    sceGuTexWrap(m==MAT_BOT ? GU_CLAMP : GU_REPEAT,m==MAT_BOT ? GU_CLAMP : GU_REPEAT);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    sceGuTexScale(1,1);
    sceGuTexOffset(0,0);
}

void assets_shutdown(void) {
    for (int m=0; m<MAT_COUNT; ++m)
        for (int level=0; level<4; ++level) { free(textures[m][level]); textures[m][level]=NULL; }
}
