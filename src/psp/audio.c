#include "platform.h"
#include <pspaudio.h>
#include <pspkernel.h>
#include <pspthreadman.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#define AUDIO_FRAMES 1024
typedef struct { int16_t *samples; unsigned count,position; } Clip;
static Clip clips[3];
static int16_t buffers[2][AUDIO_FRAMES*2] __attribute__((aligned(64)));
static int channel=-1;
static SceUID thread=-1,semaphore=-1;
static volatile int running;
typedef struct { int level; float ambient,chase,left,right; } Mix;
static Mix requested;
static bool load_clip(Clip *clip,const char *path) {
    FILE *f=fopen(path,"rb");if(!f){log_write("Missing audio: %s",path);return false;}
    bool ok=fseek(f,0,SEEK_END)==0;long size=ok?ftell(f):-1;
    if(size<=0 || size>4*1024*1024 || size%2 || fseek(f,0,SEEK_SET)!=0){fclose(f);return false;}
    clip->samples=malloc((size_t)size);
    if(!clip->samples){fclose(f);return false;}
    ok=fread(clip->samples,1,(size_t)size,f)==(size_t)size;fclose(f);
    if(!ok){free(clip->samples);clip->samples=NULL;return false;}
    clip->count=(unsigned)size/2;return true;
}
static int16_t sample(Clip *clip) {
    /* Exact 11025 -> 44100 ratio, interpolated and looped. */
    unsigned i=clip->position>>2,next=(i+1)%clip->count,f=clip->position&3;
    int value=(clip->samples[i]*(int)(4-f)+clip->samples[next]*(int)f)/4;
    clip->position=(clip->position+1)%(clip->count*4);return (int16_t)value;
}
static int16_t saturate(float value) {
    if(value>32767)return 32767;
    if(value<-32768)return -32768;
    return (int16_t)value;
}
static int audio_thread(SceSize args,void *argp) {
    (void)args;(void)argp;
    int buffer=0;float volumes[3]={0,0,0};Mix mix={0,0,0,1,1};
    while(running) {
        if(sceKernelWaitSema(semaphore,1,NULL)<0)break;
        mix=requested;sceKernelSignalSema(semaphore,1);
        for(int frame=0;frame<AUDIO_FRAMES;++frame) {
            float targets[]={mix.level==0?mix.ambient:0,mix.level==1?mix.ambient:0,mix.chase};
            float ambient=0,chase=0;
            for(int c=0;c<3;++c) {
                volumes[c]+=(targets[c]-volumes[c])*0.00015f;
                float value=sample(&clips[c])*volumes[c];
                if(c==2)chase=value;else ambient+=value;
            }
            buffers[buffer][frame*2]=saturate(ambient+chase*mix.left);
            buffers[buffer][frame*2+1]=saturate(ambient+chase*mix.right);
        }
        if(sceAudioOutputPannedBlocking(channel,PSP_AUDIO_VOLUME_MAX,PSP_AUDIO_VOLUME_MAX,buffers[buffer])<0)break;
        buffer^=1;
    }
    running=0;return 0;
}
bool audio_init(void) {
    const char *paths[]={"assets/ambient_level0.raw","assets/ambient_poolrooms.raw","assets/chase.raw"};
    for(int i=0;i<3;++i)if(!load_clip(&clips[i],paths[i])){audio_shutdown();return false;}
    channel=sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL,AUDIO_FRAMES,PSP_AUDIO_FORMAT_STEREO);
    if(channel<0){audio_shutdown();return false;}
    semaphore=sceKernelCreateSema("br_audio_mix",0,1,1,NULL);
    if(semaphore<0){audio_shutdown();return false;}
    thread=sceKernelCreateThread("br_audio",audio_thread,0x12,0x10000,PSP_THREAD_ATTR_USER,NULL);
    if(thread<0){audio_shutdown();return false;}
    running=1;
    if(sceKernelStartThread(thread,0,NULL)<0){running=0;audio_shutdown();return false;}
    log_write("Audio: 3 preserved PCM loops, 44100 Hz stereo output");return true;
}
void audio_update(const Game *g) {
    if(!running)return;
    float volume=g->settings.volume/100.0f;bool playing=g->screen==SCREEN_PLAY;
    float pan=sinf(g->yaw)*(g->enemy.z-g->player.z)-cosf(g->yaw)*(g->enemy.x-g->player.x);
    pan=fmaxf(-1,fminf(1,-pan/5));
    Mix next={g->world.level,volume*(playing?0.48f:0.20f),playing?g->threat*volume*0.75f:0,
              1-fmaxf(0,pan)*0.55f,1+fminf(0,pan)*0.55f};
    if(sceKernelWaitSema(semaphore,1,NULL)>=0){requested=next;sceKernelSignalSema(semaphore,1);}
}
void audio_shutdown(void) {
    running=0;
    if(thread>=0){sceKernelWaitThreadEnd(thread,NULL);sceKernelDeleteThread(thread);thread=-1;}
    if(channel>=0){sceAudioChRelease(channel);channel=-1;}
    if(semaphore>=0){sceKernelDeleteSema(semaphore);semaphore=-1;}
    for(int i=0;i<3;++i){free(clips[i].samples);clips[i]=(Clip){0};}
}
