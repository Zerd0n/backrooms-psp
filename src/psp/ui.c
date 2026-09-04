#include "ui.h"
#include <pspgu.h>
#include <pspkernel.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct { unsigned color; short x,y,z,pad; } UiVertex;
static UiVertex vertices[24000] __attribute__((aligned(16)));
static int count;
#define INK 0xffe9ecee
#define MUTED 0xffa4aaa9
#define GOLD 0xff76d7e8
#define PANEL 0xed141716

/* Original 5x7 bitmap glyphs, encoded as seven rows. No font dependency. */
static const unsigned char alphabet[][7] = {
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{31,4,4,4,4,4,31},
 {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14}
};

static void rect(int x,int y,int w,int h,unsigned c) {
    if (count+2>24000 || w<=0 || h<=0) return;
    vertices[count++]=(UiVertex){c,(short)x,(short)y,0,0};
    vertices[count++]=(UiVertex){c,(short)(x+w),(short)(y+h),0,0};
}

static void text(int x,int y,int scale,unsigned color,const char *str) {
    int origin=x;
    for (; *str; ++str,x+=6*scale) {
        unsigned char ch=(unsigned char)*str;
        if (ch=='\n') { x=origin-6*scale; y+=10*scale; continue; }
        if (ch>='a' && ch<='z') ch-=32;
        unsigned char rows[7]={0};
        if (ch>='A' && ch<='Z') memcpy(rows,alphabet[ch-'A'],7);
        else if (ch>='0' && ch<='9') memcpy(rows,alphabet[ch-'0'+26],7);
        else switch(ch) {
        case '.': rows[6]=4; break;
        case ':': rows[2]=4; rows[5]=4; break;
        case '/': for(int r=0;r<7;++r) rows[r]=1<<(r*4/6); break;
        case '-': rows[3]=14; break;
        case '+': rows[2]=4; rows[3]=14; rows[4]=4; break;
        case '>': rows[1]=8; rows[2]=4; rows[3]=2; rows[4]=4; rows[5]=8; break;
        case '<': rows[1]=2; rows[2]=4; rows[3]=8; rows[4]=4; rows[5]=2; break;
        case '[': rows[0]=6; rows[6]=6; for(int r=1;r<6;++r)rows[r]=4; break;
        case ']': rows[0]=12; rows[6]=12; for(int r=1;r<6;++r)rows[r]=4; break;
        case '!': for(int r=0;r<5;++r)rows[r]=4; rows[6]=4; break;
        default: break;
        }
        for(int r=0;r<7;++r) {
            int col=0;
            while(col<5) {
                if (!(rows[r]&(1<<(4-col)))) { ++col; continue; }
                int start=col;
                while(col<5 && (rows[r]&(1<<(4-col)))) ++col;
                rect(x+start*scale,y+r*scale,(col-start)*scale,scale,color);
            }
        }
    }
}

static void centered(int y,int scale,unsigned color,const char *s) {
    text((480-(int)strlen(s)*6*scale)/2,y,scale,color,s);
}

static void menu_item(int y,int selected,const char *label) {
    if(selected) { rect(28,y-5,290,20,0x90282f31); rect(28,y-5,2,20,GOLD); }
    text(40,y,1,selected ? GOLD : INK,label);
    if(selected) text(302,y,1,GOLD,">");
}

static void map_draw(const Game *g) {
    rect(126,22,228,228,PANEL);
    text(145,35,1,GOLD,"SURVEY / TRIANGLE TO CLOSE");
    int ox=147,oy=53,scale=6;
    for(int z=0;z<WORLD_SIZE;++z) for(int x=0;x<WORLD_SIZE;++x) {
        if(!g->visited[z*WORLD_SIZE+x])continue;
        Tile t=world_tile(&g->world,x,z);
        rect(ox+x*scale,oy+z*scale,scale-1,scale-1,
             t==TILE_WALL ? 0xff3d4243 : (t==TILE_WATER ? 0xff8b7760 : 0xff848c8a));
    }
    for(int i=0;i<RELAY_COUNT;++i) {
        Vec2 p=g->world.relays[i];
        if(g->visited[(int)p.z*WORLD_SIZE+(int)p.x])
            rect(ox+(int)p.x*scale,oy+(int)p.z*scale,5,5,g->relays[i]?0xffa2dd85:GOLD);
    }
    Vec2 p=g->world.exit;
    if(g->visited[(int)p.z*WORLD_SIZE+(int)p.x])
        rect(ox+(int)p.x*scale,oy+(int)p.z*scale,5,5,0xffcaa8f1);
    int px=ox+(int)(g->player.x*scale),py=oy+(int)(g->player.z*scale);
    rect(px-2,py-2,4,4,INK);
    rect(px+(int)(sinf(g->yaw)*5)-1,py+(int)(cosf(g->yaw)*5)-1,2,2,GOLD);
}

void ui_draw(const Game *g,float fps,bool audio_available) {
    count=0;
    char line[96];
    sceGuDisable(GU_TEXTURE_2D); sceGuDisable(GU_DEPTH_TEST); sceGuDisable(GU_FOG);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    if(g->screen==SCREEN_PLAY) {
        rect(0,0,480,38,0x95141716);
        text(16,12,1,GOLD,g->world.level ? "02 / POOLROOMS" : "01 / LEVEL ZERO");
        snprintf(line,sizeof(line),"RELAYS %d / 3",g->relay_total);
        text(380,12,1,INK,line);
        if(g->threat>0.35f) {
            unsigned a=(unsigned)(g->threat*115);
            rect(0,0,4,272,(a<<24)|0x3030dd); rect(476,0,4,272,(a<<24)|0x3030dd);
            text(16,26,1,0xff9898f2,"SOMETHING HEARD YOU");
        } else text(16,26,1,MUTED,g->practice ? "EXPLORATION" : "RESTORE POWER. KEEP MOVING.");
        rect(237,135,6,1,0xaaffffff); rect(240,132,1,7,0xaaffffff);
        rect(16,249,94,3,0xff323a3b);
        rect(16,249,(int)(94*g->stamina),3,g->exhausted?0xff8080e0:GOLD);
        text(16,259,1,MUTED,"X RUN / L R STRAFE");
        text(302,259,1,MUTED,"TRIANGLE MAP / START PAUSE");
        const char *prompt=NULL;
        if(game_near_relay(g)>=0)prompt="SQUARE / ACTIVATE RELAY";
        else if(game_near_exit(g))prompt=g->relay_total==3 ? "SQUARE / LEAVE" : "EXIT LOCKED / 3 RELAYS REQUIRED";
        else if(g->notice_time>0)prompt=g->notice;
        if(prompt) { rect(0,220,480,20,0xb0141716); centered(226,1,GOLD,prompt); }
        if(g->map_open)map_draw(g);
    } else if(g->screen==SCREEN_TITLE) {
        rect(0,0,350,272,0xcf111514);
        text(30,25,1,GOLD,"RECOVERED FOOTAGE / 1998");
        text(28,51,4,INK,"BACKROOMS");
        text(30,91,1,MUTED,"NO ONE IS SUPPOSED TO BE HERE.");
        const char *items[]={"NEW GAME","CONTINUE FROM CHECKPOINT","EXPLORE WITHOUT PURSUER","SETTINGS","EXIT GAME"};
        for(int i=0;i<5;++i)menu_item(128+i*23,g->menu==i,items[i]);
        text(30,253,1,MUTED,"D-PAD SELECT / X CONFIRM");
        text(398,253,1,GOLD,"PSP / 02");
    } else if(g->screen==SCREEN_PAUSE) {
        rect(0,0,480,272,0xb9111514); text(30,35,3,INK,"PAUSED");
        text(30,75,1,MUTED,"TAKE A BREATH.");
        const char *items[]={"RESUME","SETTINGS","RESTART LEVEL","MAIN MENU"};
        for(int i=0;i<4;++i)menu_item(113+i*26,g->menu==i,items[i]);
        text(30,249,1,MUTED,"X CONFIRM / CIRCLE BACK");
    } else if(g->screen==SCREEN_SETTINGS) {
        rect(0,0,480,272,0xed111514); text(30,30,3,INK,"SETTINGS");
        const char *labels[]={"TURN SENSITIVITY","BRIGHTNESS","VOLUME","HEAD BOB","SHOW FPS"};
        for(int i=0;i<5;++i) {
            menu_item(90+i*27,g->menu==i,labels[i]);
            if(i==0)snprintf(line,sizeof(line),"%.1f",g->settings.sensitivity);
            if(i==1)snprintf(line,sizeof(line),"%.1f",g->settings.brightness);
            if(i==2)snprintf(line,sizeof(line),"%d",g->settings.volume);
            if(i==3)snprintf(line,sizeof(line),"%s",g->settings.head_bob?"ON":"OFF");
            if(i==4)snprintf(line,sizeof(line),"%s",g->settings.show_fps?"ON":"OFF");
            text(351,90+i*27,1,GOLD,line);
        }
        text(30,249,1,MUTED,"LEFT RIGHT CHANGE / CIRCLE SAVE AND BACK");
    } else {
        rect(0,0,480,272,g->screen==SCREEN_DEAD?0xe319162b:0xf0161d1c);
        if(g->screen==SCREEN_DEAD) {
            centered(66,3,INK,"SIGNAL LOST");
            centered(111,1,MUTED,"YOU WERE NOT ALONE.");
            centered(174,1,GOLD,"X / RETRY THIS LEVEL");
            centered(195,1,MUTED,"CIRCLE / MAIN MENU");
        } else if(g->screen==SCREEN_TRANSITION) {
            centered(80,1,GOLD,"TAPE 02"); centered(109,3,INK,"POOLROOMS");
            centered(159,1,MUTED,"THE WATER IS WARM. THE AIR IS STILL.");
        } else {
            centered(71,3,INK,"YOU GOT OUT");
            centered(116,1,MUTED,"FOR NOW.");
            snprintf(line,sizeof(line),"TIME %02d:%02d",(int)g->run_time/60,(int)g->run_time%60);
            centered(150,1,GOLD,line); centered(196,1,MUTED,"X / MAIN MENU");
        }
    }
    if(!audio_available)text(326,45,1,MUTED,"AUDIO UNAVAILABLE");
    if(g->save_failed) {
        rect(0,0,480,19,0xff25254d);
        centered(6,1,INK,"SAVE FAILED / CHECK MEMORY STICK SPACE");
    }
    if(g->settings.show_fps) {
        snprintf(line,sizeof(line),"%.0f FPS",fps); text(422,29,1,GOLD,line);
    }
    sceKernelDcacheWritebackRange(vertices,(unsigned)count*sizeof(UiVertex));
    sceGuDrawArray(GU_SPRITES,GU_COLOR_8888|GU_VERTEX_16BIT|GU_TRANSFORM_2D,count,0,vertices);
    sceGuDisable(GU_BLEND);
}
