#include "psp/platform.h"
#include <pspkernel.h>
#include <pspctrl.h>
#include <psppower.h>
#include <pspdebug.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

PSP_MODULE_INFO("Backrooms Rebuilt",0,2,0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(12*1024);
static volatile int running=1,resumed=0;
static Game game;
#ifdef BR_SMOKE_TEST
void smoke_before(Game *g,int frame);
bool smoke_after(int frame,float fps);
#endif
static int exit_callback(int a,int b,void *common) {
    (void)a;(void)b;(void)common;running=0;return 0;
}
static int power_callback(int unknown,int flags,void *common) {
    (void)unknown;(void)common;
    if(flags & (PSP_POWER_CB_RESUME_COMPLETE|PSP_POWER_CB_SUSPENDING))resumed=1;
    return 0;
}
static int callback_thread(SceSize args,void *argp) {
    (void)args;(void)argp;
    int id=sceKernelCreateCallback("br_exit",exit_callback,NULL);
    if(id>=0)sceKernelRegisterExitCallback(id);
    id=sceKernelCreateCallback("br_power",power_callback,NULL);
    if(id>=0)scePowerRegisterCallback(-1,id);
    sceKernelSleepThreadCB();return 0;
}
static float axis(unsigned char v) {
    float f=((int)v-128)/127.0f;
    if(f>-0.18f && f<0.18f)return 0;
    return f>0?(f-0.18f)/0.82f:(f+0.18f)/0.82f;
}
static Input read_input(uint32_t *previous) {
    SceCtrlData pad;memset(&pad,0,sizeof(pad));pad.Lx=pad.Ly=128;
    sceCtrlPeekBufferPositive(&pad,1);
    SceCtrlLatch latch;memset(&latch,0,sizeof(latch));sceCtrlReadLatch(&latch);
    const unsigned psp[]={PSP_CTRL_UP,PSP_CTRL_DOWN,PSP_CTRL_LEFT,PSP_CTRL_RIGHT,
        PSP_CTRL_CROSS,PSP_CTRL_CIRCLE,PSP_CTRL_SQUARE,PSP_CTRL_TRIANGLE,
        PSP_CTRL_START,PSP_CTRL_LTRIGGER,PSP_CTRL_RTRIGGER};
    uint32_t held=0,pressed=0;
    for(unsigned i=0;i<sizeof(psp)/sizeof(psp[0]);++i) {
        if(pad.Buttons&psp[i])held|=1u<<i;
        if(latch.uiMake&psp[i])pressed|=1u<<i;
    }
    Input in={-axis(pad.Ly),axis(pad.Lx),held,pressed|(held&~*previous)};*previous=held;return in;
}
static void fatal(const char *message) {
    log_write("FATAL: %s",message);
    pspDebugScreenInit();pspDebugScreenPrintf("Backrooms PSP\n\n%s\n\nPress HOME to exit.\n",message);
    while(running)sceKernelDelayThread(20000);
}
int main(int argc,char **argv) {
    if(argc>0 && argv[0]) {
        char path[512];
        if(strlen(argv[0])<sizeof(path)) {
            snprintf(path,sizeof(path),"%s",argv[0]);char *slash=strrchr(path,'/');
            if(slash){*slash='\0';chdir(path);}
        }
    }
    int callbacks=sceKernelCreateThread("br_callbacks",callback_thread,0x11,0x1000,PSP_THREAD_ATTR_USER,NULL);
    if(callbacks>=0)sceKernelStartThread(callbacks,0,NULL);
    scePowerSetClockFrequency(333,333,166);
    sceCtrlSetSamplingCycle(0);sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    log_open();Settings settings;settings_load(&settings);game_init(&game,&settings);
    if(!renderer_init()){fatal("Cannot initialize graphics. See backrooms.log.");log_close();sceKernelExitGame();return 1;}
    if(!renderer_world(&game)){renderer_shutdown();fatal("Level exceeds geometry budget.");log_close();sceKernelExitGame();return 1;}
    bool audio=audio_init();if(!audio)log_write("Continuing without audio");
    uint32_t previous=0;int revision=game.revision;float brightness=game.settings.brightness;
    unsigned long long previous_time=sceKernelGetSystemTimeWide();
    float fps=60;int last_screen=game.screen;
#ifdef BR_SMOKE_TEST
    int frame=0;
#endif
    while(running) {
#ifdef BR_SMOKE_TEST
        ++frame;
        smoke_before(&game,frame);
#endif
        unsigned long long now=sceKernelGetSystemTimeWide();
        float elapsed=(float)(now-previous_time)/1000000.0f;previous_time=now;
        if(elapsed>0.25f)elapsed=0.25f;
        if(resumed){resumed=0;elapsed=0;if(game.screen==SCREEN_PLAY)game.screen=SCREEN_PAUSE;}
        Input input=read_input(&previous);
        /* Bounded semi-fixed steps: movement remains stable after a slow frame.
           Button edges are consumed only by the first substep. */
        float remaining=elapsed;
        while(remaining>0.0001f) {
            float dt=remaining>1.0f/60?1.0f/60:remaining;
            game_step(&game,input,dt);remaining-=dt;input.pressed=0;
        }
        if(game.events&EVENT_SAVE) {
            game.save_failed=!settings_save(&game.settings);
        }
        if(game.events&EVENT_QUIT)running=0;
        game.events=0;
        if(last_screen!=(int)game.screen){log_write("Screen %d -> %d; level %d; relays %d",last_screen,game.screen,game.world.level,game.relay_total);last_screen=game.screen;}
        if(revision!=game.revision || brightness!=game.settings.brightness) {
            if(!renderer_world(&game)){running=0;log_write("FATAL: geometry budget exceeded");break;}
            revision=game.revision;brightness=game.settings.brightness;
        }
        audio_update(&game);
        if(elapsed>0.001f)fps+=(1.0f/elapsed-fps)*0.05f;
        renderer_draw(&game,fps,audio);
#ifdef BR_SMOKE_TEST
        if(!smoke_after(frame,fps))running=0;
#endif
    }
    settings_save(&game.settings);audio_shutdown();renderer_shutdown();
    log_write("Clean shutdown");log_close();sceKernelExitGame();return 0;
}
