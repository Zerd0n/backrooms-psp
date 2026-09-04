/* Optional integration driver. Compiled only with make SMOKE_TEST=1.
   Runs the real PSP renderer/audio and captures the displayed framebuffer. */
#include "../src/psp/platform.h"
#include <pspdisplay.h>
#include <pspkernel.h>
#include <pspgu.h>
#include <stdio.h>
#include <string.h>

static unsigned transfer_list[4096] __attribute__((aligned(16)));
static unsigned readback[480*272] __attribute__((aligned(64)));

static void capture(int frame,float fps) {
    void *address=NULL;int stride=0,format=0;
    if(sceDisplayGetFrameBuf(&address,&stride,&format,PSP_DISPLAY_SETBUF_IMMEDIATE)<0 || !address)return;
    if(format!=PSP_DISPLAY_PIXEL_FORMAT_8888)return;
    char name[48];snprintf(name,sizeof(name),"smoke-%04d.ppm",frame);
    FILE *f=fopen(name,"wb");if(!f)return;
    fprintf(f,"P6\n480 272\n255\n");
    /* A GE transfer forces PPSSPP to read back the rendered target. Direct
       CPU reads of VRAM can otherwise observe a stale emulated RAM copy. */
    sceGuStart(GU_DIRECT,transfer_list);
    sceGuCopyImage(GU_PSM_8888,0,0,480,272,stride,address,0,0,480,readback);
    sceGuTexSync();sceGuFinish();sceGuSync(0,0);
    sceKernelDcacheInvalidateRange(readback,sizeof(readback));
    const unsigned *pixels=readback;
    unsigned char row[480*3];
    for(int y=0;y<272;++y) {
        for(int x=0;x<480;++x) {
            unsigned c=pixels[y*480+x];
            row[x*3]=c&255;row[x*3+1]=(c>>8)&255;row[x*3+2]=(c>>16)&255;
        }
        fwrite(row,1,sizeof(row),f);
    }
    fclose(f);log_write("SMOKE screenshot=%s fps=%.1f",name,fps);
}

void smoke_before(Game *g,int frame) {
    if(frame==1)g->settings.show_fps=1;
    if(frame==120)game_start(g,0,true);
    if(frame==240){g->player=(Vec2){10.5f,4.5f};g->yaw=1.5707963f;}
    if(frame==360){g->map_open=true;}
    if(frame==480){g->map_open=false;g->screen=SCREEN_PAUSE;}
    if(frame==600){g->screen=SCREEN_SETTINGS;g->settings_return=SCREEN_PAUSE;}
    if(frame==720)game_start(g,1,true);
    if(frame==840){g->player=(Vec2){5.5f,3.5f};g->yaw=0;}
    if(frame>=960 && frame<1080){g->practice=false;g->level_time=20;g->enemy=(Vec2){5.5f,5.5f};g->enemy_goal=g->enemy;g->screen=SCREEN_PLAY;}
    if(frame==1080)g->screen=SCREEN_DEAD;
    if(frame==1200)g->screen=SCREEN_WIN;
}
bool smoke_after(int frame,float fps) {
    if(frame%120==90)capture(frame,fps);
    return frame<1320;
}
