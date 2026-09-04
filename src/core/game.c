#include "game.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static float clamp(float v, float lo, float hi) { return fmaxf(lo, fminf(v, hi)); }

void settings_defaults(Settings *s) {
    *s = (Settings){1.0f,1.0f,70,1,0,0};
}

void game_notice(Game *g, const char *text) {
    snprintf(g->notice, sizeof(g->notice), "%s", text);
    g->notice_time = 3.0f;
}

void game_start(Game *g, int level, bool practice) {
    world_build(&g->world, level);
    g->player = g->world.spawn;
    g->enemy = g->world.enemy_spawn;
    g->enemy_goal = g->enemy;
    g->yaw = 1.15f;
    g->stamina = 1;
    g->exhausted = false;
    g->step_phase = g->level_time = g->path_timer = g->threat = 0;
    g->relay_total = 0;
    g->practice = practice;
    g->screen = SCREEN_PLAY;
    g->map_open = false;
    memset(g->relays, 0, sizeof(g->relays));
    memset(g->visited, 0, sizeof(g->visited));
    world_distances(&g->world, g->player, g->distances);
    ++g->revision;
    game_notice(g, practice ? "EXPLORE / NO PURSUER" : "RESTORE 3 RELAYS. FIND THE EXIT.");
}

void game_init(Game *g, const Settings *s) {
    memset(g, 0, sizeof(*g));
    g->settings = *s;
    game_start(g, 0, false);
    g->screen = SCREEN_TITLE;
}

int game_near_relay(const Game *g) {
    for (int i = 0; i < RELAY_COUNT; ++i)
        if (!g->relays[i] && vec_distance(g->player,g->world.relays[i]) < 1.05f &&
            world_visible(&g->world,g->player,g->world.relays[i])) return i;
    return -1;
}

bool game_near_exit(const Game *g) {
    return vec_distance(g->player,g->world.exit) < 1.05f &&
           world_visible(&g->world,g->player,g->world.exit);
}

static void enemy_step(Game *g, float dt) {
    if (g->practice || g->level_time < 14) { g->threat = 0; return; }
    g->path_timer -= dt;
    if (g->path_timer <= 0) {
        world_distances(&g->world, g->player, g->distances);
        g->path_timer = 0.25f;
    }
    if (vec_distance(g->enemy,g->enemy_goal) < 0.025f) {
        int x = (int)g->enemy.x, z = (int)g->enemy.z;
        int best = g->distances[z*WORLD_SIZE+x];
        static const int dx[] = {1,-1,0,0}, dz[] = {0,0,1,-1};
        g->enemy_goal = (Vec2){x+0.5f,z+0.5f};
        for (int d = 0; d < 4; ++d) {
            int nx=x+dx[d], nz=z+dz[d];
            if (world_tile(&g->world,nx,nz) == TILE_WALL) continue;
            int distance = g->distances[nz*WORLD_SIZE+nx];
            if (distance >= 0 && (best < 0 || distance < best)) {
                best = distance;
                g->enemy_goal = (Vec2){nx+0.5f,nz+0.5f};
            }
        }
        if ((int)g->player.x == x && (int)g->player.z == z) g->enemy_goal = g->player;
    }
    float distance = vec_distance(g->enemy,g->enemy_goal);
    float speed = 1.32f + 0.07f*g->relay_total + 0.08f*g->world.level;
    if (distance > 0.001f) {
        float fraction = fminf(1, speed*dt/distance);
        world_move(&g->world,&g->enemy,(Vec2){(g->enemy_goal.x-g->enemy.x)*fraction,
                   (g->enemy_goal.z-g->enemy.z)*fraction},0.18f);
    }
    distance = vec_distance(g->enemy,g->player);
    int path = g->distances[(int)g->enemy.z*WORLD_SIZE+(int)g->enemy.x];
    float intensity = path < 0 ? 0 : clamp(1.0f-path/15.0f,0,1);
    g->threat += (intensity-g->threat)*fminf(1,dt*4);
    if (distance < 0.43f && world_visible(&g->world,g->enemy,g->player)) {
        g->screen = SCREEN_DEAD;
        g->map_open = false;
    }
}

static void menu_step(Game *g, Input in) {
    if (g->screen == SCREEN_SETTINGS) {
        if (in.pressed & BTN_BACK) {
            g->screen = g->settings_return; g->menu = 0; g->events |= EVENT_SAVE; return;
        }
        if (in.pressed & BTN_UP) g->menu = (g->menu+4)%5;
        if (in.pressed & BTN_DOWN) g->menu = (g->menu+1)%5;
        int change = !!(in.pressed & BTN_RIGHT) - !!(in.pressed & BTN_LEFT);
        if (in.pressed & BTN_ACCEPT) change = 1;
        if (change) {
            Settings *s=&g->settings;
            switch (g->menu) {
            case 0: s->sensitivity=clamp(s->sensitivity+change*0.1f,0.5f,2); break;
            case 1: s->brightness=clamp(s->brightness+change*0.1f,0.6f,1.4f); break;
            case 2: s->volume=(int)clamp(s->volume+change*10,0,100); break;
            case 3: s->head_bob=!s->head_bob; break;
            case 4: s->show_fps=!s->show_fps; break;
            }
        }
        return;
    }
    int count = g->screen == SCREEN_TITLE ? 5 : 4;
    if (in.pressed & BTN_UP) g->menu=(g->menu+count-1)%count;
    if (in.pressed & BTN_DOWN) g->menu=(g->menu+1)%count;
    if (g->screen == SCREEN_PAUSE && (in.pressed & (BTN_BACK|BTN_PAUSE))) {
        g->screen=SCREEN_PLAY; return;
    }
    if (!(in.pressed & BTN_ACCEPT)) return;
    if (g->screen == SCREEN_TITLE) {
        switch (g->menu) {
        case 0: g->run_time=0; game_start(g,0,false); break;
        case 1: g->run_time=0; game_start(g,g->settings.checkpoint,false); break;
        case 2: g->run_time=0; game_start(g,0,true); break;
        case 3: g->settings_return=SCREEN_TITLE; g->screen=SCREEN_SETTINGS; g->menu=0; break;
        case 4: g->events |= EVENT_QUIT; break;
        }
    } else {
        switch (g->menu) {
        case 0: g->screen=SCREEN_PLAY; break;
        case 1: g->settings_return=SCREEN_PAUSE; g->screen=SCREEN_SETTINGS; g->menu=0; break;
        case 2: game_start(g,g->world.level,g->practice); break;
        case 3: g->screen=SCREEN_TITLE; g->menu=0; break;
        }
    }
}

void game_step(Game *g, Input in, float dt) {
    if (!isfinite(dt) || dt <= 0) return;
    dt = fminf(dt,0.05f);
    if (g->screen==SCREEN_TITLE || g->screen==SCREEN_PAUSE || g->screen==SCREEN_SETTINGS) {
        menu_step(g,in); return;
    }
    if (g->screen==SCREEN_DEAD || g->screen==SCREEN_WIN) {
        if (in.pressed & BTN_ACCEPT) {
            if (g->screen==SCREEN_WIN) { g->screen=SCREEN_TITLE; g->menu=0; }
            else game_start(g,g->world.level,g->practice);
        } else if (in.pressed & BTN_BACK) { g->screen=SCREEN_TITLE; g->menu=0; }
        return;
    }
    if (g->screen==SCREEN_TRANSITION) {
        g->transition_time -= dt;
        if (g->transition_time <= 0) {
            game_start(g,1,g->practice);
            if (!g->practice) { g->settings.checkpoint=1; g->events |= EVENT_SAVE; }
        }
        return;
    }
    if (in.pressed & BTN_PAUSE) { g->screen=SCREEN_PAUSE; g->menu=0; return; }
    if (in.pressed & BTN_MAP) g->map_open=!g->map_open;
    g->level_time += dt; g->run_time += dt;
    g->notice_time = fmaxf(0,g->notice_time-dt);
    float turn=isfinite(in.turn) ? clamp(in.turn,-1,1) : 0;
    float forward=isfinite(in.forward) ? clamp(in.forward,-1,1) : 0;
    turn += !!(in.held & BTN_RIGHT) - !!(in.held & BTN_LEFT);
    forward += !!(in.held & BTN_UP) - !!(in.held & BTN_DOWN);
    turn=clamp(turn,-1,1); forward=clamp(forward,-1,1);
    g->yaw += turn*2.1f*g->settings.sensitivity*dt;
    if (g->yaw > 3.14159265f) g->yaw-=6.2831853f;
    if (g->yaw < -3.14159265f) g->yaw+=6.2831853f;
    float strafe=!!(in.held & BTN_R)-!!(in.held & BTN_L);
    float magnitude=sqrtf(forward*forward+strafe*strafe);
    bool sprint=(in.held & BTN_ACCEPT) && magnitude>0.1f && !g->exhausted;
    if (sprint) {
        g->stamina=fmaxf(0,g->stamina-dt/5.0f);
        if (g->stamina<=0) g->exhausted=true;
    } else {
        g->stamina=fminf(1,g->stamina+dt/7.0f);
        if (g->stamina>=0.3f) g->exhausted=false;
    }
    float speed=sprint ? 2.85f : 1.85f;
    if (world_tile(&g->world,(int)g->player.x,(int)g->player.z)==TILE_WATER) speed*=0.72f;
    if (magnitude>1) { forward/=magnitude; strafe/=magnitude; }
    Vec2 before=g->player;
    world_move(&g->world,&g->player,(Vec2){(sinf(g->yaw)*forward+cosf(g->yaw)*strafe)*speed*dt,
        (cosf(g->yaw)*forward-sinf(g->yaw)*strafe)*speed*dt},0.2f);
    g->step_phase += vec_distance(before,g->player)*8;
    for (int z=(int)g->player.z-2; z<=(int)g->player.z+2; ++z)
        for (int x=(int)g->player.x-2; x<=(int)g->player.x+2; ++x)
            if (x>=0 && z>=0 && x<WORLD_SIZE && z<WORLD_SIZE)
                g->visited[z*WORLD_SIZE+x]=1;
    if (in.pressed & BTN_USE) {
        int relay=game_near_relay(g);
        if (relay>=0) {
            g->relays[relay]=true; ++g->relay_total;
            game_notice(g,g->relay_total==3 ? "POWER RESTORED. FIND THE EXIT." : "RELAY ONLINE. KEEP MOVING.");
        } else if (game_near_exit(g)) {
            if (g->relay_total<3) game_notice(g,"EXIT LOCKED / RESTORE ALL 3 RELAYS");
            else if (g->world.level==0) { g->screen=SCREEN_TRANSITION; g->transition_time=2.5f; }
            else {
                g->screen=SCREEN_WIN;
            }
        }
    }
    if (g->screen==SCREEN_PLAY) enemy_step(g,dt);
}
