#include "platform.h"
#include "assets.h"
#include "ui.h"
#include <pspgu.h>
#include <pspgum.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <math.h>

#define MAX_VERTICES 24000
#define CHUNKS 4
#define CHUNK_SIZE 8
typedef struct { float u,v; unsigned color; float x,y,z; } Vertex;
typedef struct { int start,count; } Batch;
static Vertex geometry[MAX_VERTICES] __attribute__((aligned(16)));
static Batch batches[CHUNKS][CHUNKS][MAT_BOT];
static unsigned display_list[262144] __attribute__((aligned(16)));
static int used;
static bool overflow,initialized;

static unsigned shade(float value) {
    unsigned c=(unsigned)fminf(255,fmaxf(0,value*255));
    return 0xff000000|(c<<16)|(c<<8)|c;
}
static void quad(Vertex *out,Vec2 a,Vec2 b,float low,float high,unsigned color) {
    Vertex v[4]={{0,1,color,a.x,low,a.z},{1,1,color,b.x,low,b.z},
                 {1,0,color,b.x,high,b.z},{0,0,color,a.x,high,a.z}};
    static const int indices[]={0,1,2,0,2,3};
    for(int i=0;i<6;++i)out[i]=v[indices[i]];
}
static void wall(Vec2 a,Vec2 b,float low,float high,unsigned color) {
    if(used+6>MAX_VERTICES){overflow=true;return;}
    quad(geometry+used,a,b,low,high,color);used+=6;
}
static void horizontal(int x,int z,float y,unsigned color) {
    if(used+6>MAX_VERTICES){overflow=true;return;}
    Vertex v[4]={{0,0,color,x,y,z},{1,0,color,x+1,y,z},
                 {1,1,color,x+1,y,z+1},{0,1,color,x,y,z+1}};
    static const int indices[]={0,1,2,0,2,3};
    for(int i=0;i<6;++i)geometry[used++]=v[indices[i]];
}
bool renderer_world(const Game *g) {
    used=0;overflow=false;
    const World *w=&g->world;float height=w->level?1.9f:1.35f;
    for(int cz=0;cz<CHUNKS;++cz)for(int cx=0;cx<CHUNKS;++cx) {
        for(int material=0;material<MAT_BOT;++material) {
            Batch *batch=&batches[cz][cx][material];batch->start=used;
            for(int z=cz*CHUNK_SIZE;z<(cz+1)*CHUNK_SIZE && z<WORLD_SIZE;++z) {
                for(int x=cx*CHUNK_SIZE;x<(cx+1)*CHUNK_SIZE && x<WORLD_SIZE;++x) {
                    Tile tile=world_tile(w,x,z);if(tile==TILE_WALL)continue;
                    int hash=(x*13+z*7)%11;bool light=(x%3==1 && z%4==0);
                    int floor=w->level?(tile==TILE_WATER?MAT_WATER:MAT_POOL_FLOOR):MAT_CARPET;
                    int ceiling=w->level?(light?MAT_POOL_LIGHT:MAT_POOL_CEILING):(light?MAT_LIGHT:MAT_CEILING);
                    int wm=w->level?(hash<2?MAT_POOL_DAMP:MAT_POOL_WALL):(hash<2?MAT_DIRTY:MAT_WALL);
                    float brightness=g->settings.brightness,ambient=w->level?0.78f:0.97f;
                    if(material==floor)horizontal(x,z,tile==TILE_WATER?-0.055f:0,shade(brightness*(w->level?0.70f:0.78f)));
                    if(material==ceiling)horizontal(x,z,height,shade(brightness*(light?1.0f:0.67f)));
                    const int dx[]={0,1,0,-1},dz[]={-1,0,1,0};
                    Vec2 a[]={{x,z},{x+1,z},{x+1,z+1},{x,z+1}};
                    Vec2 b[]={{x+1,z},{x+1,z+1},{x,z+1},{x,z}};
                    for(int side=0;side<4;++side) {
                        Tile neighbor=world_tile(w,x+dx[side],z+dz[side]);
                        bool door=side==1 && x==(int)w->exit.x && z==(int)w->exit.z;
                        if(neighbor==TILE_WALL && material==(door?MAT_DOOR:wm))
                            wall(a[side],b[side],0,height,shade(brightness*ambient*(side%2?0.86f:1.0f)));
                        if(w->level && tile!=TILE_WATER && neighbor==TILE_WATER && material==MAT_EDGE)
                            wall(a[side],b[side],-0.055f,0,shade(brightness*0.85f));
                    }
                }
            }
            batch->count=used-batch->start;
        }
    }
    sceKernelDcacheWritebackRange(geometry,(unsigned)used*sizeof(Vertex));
    log_write("Geometry: level=%d vertices=%d bytes=%u",w->level,used,(unsigned)(used*sizeof(Vertex)));
    return !overflow;
}
bool renderer_init(void) {
    if(!assets_init())return false;
    if(sceGuInit()<0){assets_shutdown();return false;}
    initialized=true;sceGuStart(GU_DIRECT,display_list);
    /* 1,392,640 bytes: two RGBA buffers plus 16-bit depth, within 2 MiB VRAM. */
    sceGuDrawBuffer(GU_PSM_8888,(void*)0,512);
    sceGuDispBuffer(480,272,(void*)(512*272*4),512);
    sceGuDepthBuffer((void*)(512*272*8),512);
    sceGuOffset(2048-240,2048-136);sceGuViewport(2048,2048,480,272);
    sceGuDepthRange(65535,0);sceGuDepthFunc(GU_GEQUAL);
    sceGuScissor(0,0,480,272);sceGuEnable(GU_SCISSOR_TEST);sceGuEnable(GU_CLIP_PLANES);
    sceGuDisable(GU_CULL_FACE);sceGuShadeModel(GU_SMOOTH);
    sceGuFinish();sceGuSync(0,0);sceDisplayWaitVblankStart();sceGuDisplay(GU_TRUE);
    return true;
}
static void draw_dynamic(const Game *g) {
    sceGuDisable(GU_TEXTURE_2D);
    for(int i=0;i<RELAY_COUNT+1;++i) {
        Vec2 p=i==RELAY_COUNT?g->world.exit:g->world.relays[i];
        bool on=i==RELAY_COUNT?g->relay_total==3:g->relays[i];
        unsigned color=on?0xff8aee91:0xff56c9f5;
        Vertex *v=sceGuGetMemory(6*sizeof(Vertex));
        if(i==RELAY_COUNT)
            quad(v,(Vec2){p.x+0.485f,p.z-0.21f},(Vec2){p.x+0.485f,p.z+0.21f},1.04f,1.14f,color);
        else if(i==1)quad(v,(Vec2){p.x-0.485f,p.z+0.12f},(Vec2){p.x-0.485f,p.z-0.12f},0.48f,0.77f,color);
        else quad(v,(Vec2){p.x-0.12f,p.z-0.485f},(Vec2){p.x+0.12f,p.z-0.485f},0.48f,0.77f,color);
        sceGumDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,6,0,v);
    }
    if(g->practice || g->level_time<14)return;
    sceGuEnable(GU_TEXTURE_2D);assets_bind(MAT_BOT);
    sceGuEnable(GU_ALPHA_TEST);sceGuAlphaFunc(GU_GREATER,127,255);
    sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    float rx=cosf(g->yaw)*0.47f,rz=-sinf(g->yaw)*0.47f;
    Vertex *v=sceGuGetMemory(6*sizeof(Vertex));
    quad(v,(Vec2){g->enemy.x-rx,g->enemy.z-rz},(Vec2){g->enemy.x+rx,g->enemy.z+rz},0.025f,1.06f,0xffffffff);
    sceGumDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,6,0,v);
    sceGuDisable(GU_ALPHA_TEST);sceGuDisable(GU_BLEND);
}
void renderer_draw(const Game *g,float fps,bool audio_available) {
    sceGuStart(GU_DIRECT,display_list);
    unsigned fog=g->world.level?0xff28281e:0xff22292e;
    sceGuClearColor(fog);sceGuClearDepth(0);sceGuClear(GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT);
    sceGuEnable(GU_DEPTH_TEST);sceGuDepthMask(GU_FALSE);sceGuEnable(GU_TEXTURE_2D);sceGuEnable(GU_FOG);
    sceGuFog(g->world.level?4.0f:6.0f,g->world.level?16.0f:21.0f,fog);
    sceGumMatrixMode(GU_PROJECTION);sceGumLoadIdentity();sceGumPerspective(58,480.0f/272,0.06f,28);
    float bob=g->settings.head_bob?sinf(g->step_phase)*0.018f:0;
    ScePspFVector3 eye={g->player.x,0.68f+bob,-g->player.z};
    ScePspFVector3 target={eye.x+sinf(g->yaw),eye.y,eye.z-cosf(g->yaw)},up={0,1,0};
    sceGumMatrixMode(GU_VIEW);sceGumLoadIdentity();sceGumLookAt(&eye,&target,&up);
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();
    /* Game/map coordinates increase southward. Flip Z once to preserve
       right-handed GU camera controls and the sprite's original orientation. */
    ScePspFVector3 axes={1,1,-1};sceGumScale(&axes);
    for(int material=0;material<MAT_BOT;++material) {
        assets_bind((Material)material);
        if(material==MAT_WATER)sceGuTexOffset(sinf(g->level_time*0.4f)*0.025f,cosf(g->level_time*0.3f)*0.02f);
        for(int z=0;z<CHUNKS;++z)for(int x=0;x<CHUNKS;++x) {
            float dx=x*CHUNK_SIZE+4-g->player.x,dz=z*CHUNK_SIZE+4-g->player.z;
            if(dx*dx+dz*dz>26*26 || dx*sinf(g->yaw)+dz*cosf(g->yaw)<-6)continue;
            Batch b=batches[z][x][material];
            if(b.count)sceGumDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,b.count,0,geometry+b.start);
        }
    }
    draw_dynamic(g);ui_draw(g,fps,audio_available);
    sceGuFinish();sceGuSync(0,0);sceDisplayWaitVblankStart();sceGuSwapBuffers();
}
void renderer_shutdown(void) { if(initialized)sceGuTerm();initialized=false;assets_shutdown(); }
