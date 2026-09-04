#include "../src/core/game.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void advance(Game *g,Input in,int frames) {
    for(int i=0;i<frames;++i){game_step(g,in,1.0f/60);in.pressed=0;}
}
static Game fresh(int level,bool practice) {
    Settings s;settings_defaults(&s);Game g;game_init(&g,&s);game_start(&g,level,practice);return g;
}
static void test_maps(void) {
    for(int level=0;level<2;++level) {
        World w;world_build(&w,level);int16_t d[WORLD_CELLS];world_distances(&w,w.spawn,d);
        assert(world_fits(&w,w.spawn,0.2f));assert(world_fits(&w,w.enemy_spawn,0.2f));
        assert(world_fits(&w,w.exit,0.2f));
        for(int z=0;z<WORLD_SIZE;++z)for(int x=0;x<WORLD_SIZE;++x) {
            if(!x || !z || x==WORLD_SIZE-1 || z==WORLD_SIZE-1)assert(world_tile(&w,x,z)==TILE_WALL);
            if(world_tile(&w,x,z)!=TILE_WALL)assert(d[z*WORLD_SIZE+x]>=0);
        }
        for(int i=0;i<3;++i)assert(d[(int)w.relays[i].z*WORLD_SIZE+(int)w.relays[i].x]>0);
        assert(world_tile(&w,(int)w.exit.x+1,(int)w.exit.z)==TILE_WALL);
        assert(!world_fits(&w,(Vec2){NAN,0},0.2f));
        assert(!world_fits(&w,(Vec2){1e30f,0},0.2f));
        Vec2 p=w.spawn;world_move(&w,&p,(Vec2){-7,0},0.2f);
        assert(p.x>=2.2f-0.001f);assert(world_fits(&w,p,0.2f));
        world_distances(&w,(Vec2){-1,-1},d);
        for(int i=0;i<WORLD_CELLS;++i)assert(d[i]==-1);
    }
}
static void walk_to(Game *g,Vec2 target) {
    int16_t distances[WORLD_CELLS];world_distances(&g->world,target,distances);
    static const int dx[]={1,-1,0,0},dz[]={0,0,1,-1};
    for(int frames=0;frames<30000;++frames) {
        if(vec_distance(g->player,target)<0.08f)return;
        int x=(int)g->player.x,z=(int)g->player.z;
        Vec2 next={x+0.5f,z+0.5f};
        /* Navigate from center to center so the test exercises collision
           and actual input, rather than teleporting across the level. */
        if(vec_distance(g->player,next)<0.07f) {
            int best=distances[z*WORLD_SIZE+x];
            for(int i=0;i<4;++i) {
                int nx=x+dx[i],nz=z+dz[i];
                if(world_tile(&g->world,nx,nz)==TILE_WALL)continue;
                int d=distances[nz*WORLD_SIZE+nx];
                if(d>=0 && d<best){best=d;next=(Vec2){nx+0.5f,nz+0.5f};}
            }
        } else {
            /* Once moving, retain the waypoint until it is reached. */
            float vx=sinf(g->yaw),vz=cosf(g->yaw);
            next=(Vec2){g->player.x+vx*0.15f,g->player.z+vz*0.15f};
        }
        g->yaw=atan2f(next.x-g->player.x,next.z-g->player.z);
        game_step(g,(Input){.forward=1},1.0f/120);
        assert(world_fits(&g->world,g->player,0.2f));
    }
    assert(!"Navigation did not converge");
}
static void test_campaign(void) {
    Game g=fresh(0,true);
    for(int level=0;level<2;++level) {
        walk_to(&g,g.world.exit);
        game_step(&g,(Input){.pressed=BTN_USE},1.0f/60);assert(g.screen==SCREEN_PLAY);
        for(int i=0;i<3;++i) {
            walk_to(&g,g.world.relays[i]);
            game_step(&g,(Input){.pressed=BTN_USE},1.0f/60);assert(g.relays[i]);
            int total=g.relay_total;
            game_step(&g,(Input){.pressed=BTN_USE},1.0f/60);assert(g.relay_total==total);
        }
        walk_to(&g,g.world.exit);game_step(&g,(Input){.pressed=BTN_USE},1.0f/60);
        if(!level){assert(g.screen==SCREEN_TRANSITION);advance(&g,(Input){0},160);assert(g.world.level==1);}
        else assert(g.screen==SCREEN_WIN);
    }
    assert(g.settings.checkpoint==0); /* Exploration never changes progress. */
}
static void test_enemy(void) {
    for(int level=0;level<2;++level) {
        Game g=fresh(level,false);bool moved=false;
        for(int frame=0;frame<24000 && g.screen==SCREEN_PLAY;++frame) {
            Vec2 before=g.enemy;game_step(&g,(Input){0},1.0f/60);
            assert(world_fits(&g.world,g.enemy,0.18f));
            assert(vec_distance(before,g.enemy)<0.04f);
            if(vec_distance(before,g.enemy)>0)moved=true;
            if(g.level_time<14)assert(!moved);
        }
        assert(moved);assert(g.screen==SCREEN_DEAD);
        advance(&g,(Input){.pressed=BTN_ACCEPT},1);assert(g.screen==SCREEN_PLAY);
    }
}
static void test_controls(void) {
    Game g=fresh(0,true);
    game_step(&g,(Input){.pressed=BTN_PAUSE},1.0f/60);Vec2 p=g.player;
    advance(&g,(Input){.forward=1},120);assert(vec_distance(p,g.player)==0);
    game_step(&g,(Input){.pressed=BTN_PAUSE},1.0f/60);assert(g.screen==SCREEN_PLAY);
    game_step(&g,(Input){.turn=NAN,.forward=INFINITY},1.0f/60);assert(isfinite(g.yaw));
    float time=g.level_time;game_step(&g,(Input){0},NAN);assert(g.level_time==time);
    g.yaw=1.5707963f;
    advance(&g,(Input){.forward=1,.held=BTN_ACCEPT},400);assert(g.exhausted || g.stamina<0.3f);
    advance(&g,(Input){0},500);assert(!g.exhausted);assert(g.stamina==1);
    g=fresh(0,false);g.relay_total=3;g.player=g.world.exit;
    game_step(&g,(Input){.pressed=BTN_USE},1.0f/60);advance(&g,(Input){0},160);
    assert(g.settings.checkpoint==1);assert(g.events&EVENT_SAVE);
    puts("Controls, pause, stamina, checkpoint and invalid inputs: OK");
}
int main(void) {
    test_maps();puts("Map reachability and swept collision: OK");
    test_enemy();puts("Enemy navigation and death/retry on both levels: OK");
    test_controls();test_campaign();puts("Full two-level input-driven campaign: OK");return 0;
}
